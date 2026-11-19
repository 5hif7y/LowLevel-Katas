/*
 * pty.c - forkpty() replacement on top of ConPTY (Windows 10 1809+).
 *
 * Unix:   forkpty() = openpty() + fork() + setsid() + TIOCSCTTY + dup2(slave)
 *         The parent gets ONE fd: read = child output, write = child input,
 *         ioctl(TIOCSWINSZ) = resize, and the child gets SIGWINCH.
 *
 * ConPTY: CreatePseudoConsole(size, input_pipe, output_pipe) +
 *         CreateProcess(... PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE ...)
 *         TWO anonymous pipes, not waitable, not selectable.
 *
 * To give the program its single selectable fd back we use the pump pattern
 * of module 11 of the porter guide:
 *
 *   child output pipe --ReadFile--> [out thread] --send--> sock[1]
 *   sock[1] --recv--> [in thread] --WriteFile--> child input pipe
 *   sock[0] is the "pty master" handed to the program as a vfd
 *
 * Lifetime (the hard part): when the child exits, ConPTY does NOT close the
 * output pipe. conhost keeps running until ClosePseudoConsole(). So a waiter
 * thread watches the process, posts SIGCHLD, lets the output drain, and only
 * then closes the pseudoconsole, which finally gives the pump EOF, which
 * gives the program read() == 0, like a Unix master returning EIO.
 */
#include "compat_internal.h"

typedef HRESULT (WINAPI *CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef HRESULT (WINAPI *ResizePseudoConsole_t)(HPCON, COORD);
typedef void    (WINAPI *ClosePseudoConsole_t)(HPCON);

static CreatePseudoConsole_t pCreatePseudoConsole;
static ResizePseudoConsole_t pResizePseudoConsole;
static ClosePseudoConsole_t  pClosePseudoConsole;

static int load_conpty(void)
{
	static int loaded = -1;
	if (loaded >= 0)
		return loaded;
	/* Resolved at run time so that the binary still starts (and prints a
	 * clear error) on Windows versions without ConPTY. */
	HMODULE k = GetModuleHandleW(L"kernel32.dll");
	pCreatePseudoConsole = (CreatePseudoConsole_t)(void *)GetProcAddress(k, "CreatePseudoConsole");
	pResizePseudoConsole = (ResizePseudoConsole_t)(void *)GetProcAddress(k, "ResizePseudoConsole");
	pClosePseudoConsole  = (ClosePseudoConsole_t)(void *)GetProcAddress(k, "ClosePseudoConsole");
	loaded = pCreatePseudoConsole && pResizePseudoConsole && pClosePseudoConsole;
	return loaded;
}

struct compat_pty {
	HPCON hpc;
	HANDLE in_w;            /* we write keystrokes here */
	HANDLE out_r;           /* we read rendered VT output here */
	SOCKET pump;            /* our end of the socketpair */
	struct winsize ws;
	int resized;            /* size changed since last SIGWINCH */
	volatile LONG closed;   /* ClosePseudoConsole done */
	volatile LONG refs;
	volatile ULONGLONG last_output;
	CRITICAL_SECTION lock;
};

static void pty_unref(struct compat_pty *p)
{
	if (InterlockedDecrement(&p->refs) == 0) {
		DeleteCriticalSection(&p->lock);
		free(p);
	}
}

static void pty_close_console(struct compat_pty *p)
{
	if (InterlockedExchange(&p->closed, 1))
		return;
	/* May block until conhost has flushed: never call it from the main
	 * thread while the out pump could be stuck on a full socket. */
	pClosePseudoConsole(p->hpc);
}

static DWORD WINAPI out_pump(void *arg)
{
	struct compat_pty *p = arg;
	char buf[8192];
	DWORD n;
	while (ReadFile(p->out_r, buf, sizeof buf, &n, NULL) && n > 0) {
		p->last_output = GetTickCount64();
		for (DWORD off = 0; off < n;) {
			int s = send(p->pump, buf + off, (int)(n - off), 0);
			if (s == SOCKET_ERROR)
				goto done;
			off += (DWORD)s;
		}
	}
done:
	/* EOF towards the program: its read() on the master returns 0. */
	shutdown(p->pump, SD_SEND);
	CloseHandle(p->out_r);
	pty_unref(p);
	return 0;
}

static DWORD WINAPI in_pump(void *arg)
{
	struct compat_pty *p = arg;
	char buf[4096];
	int n;
	DWORD w;
	while ((n = recv(p->pump, buf, sizeof buf, 0)) > 0) {
		for (int off = 0; off < n; off += (int)w)
			if (!WriteFile(p->in_w, buf + off, (DWORD)(n - off), &w, NULL))
				goto done;
	}
done:
	CloseHandle(p->in_w);
	pty_unref(p);
	return 0;
}

static DWORD WINAPI closer(void *arg)
{
	pty_close_console(arg);
	pty_unref(arg);
	return 0;
}

/* Called by the child waiter once the process is gone. */
void pty_child_exited(struct compat_pty *p)
{
	/* Let conhost render the last frame and the pump forward it: wait for
	 * 150 ms of output silence (max 2 s) before tearing the console down. */
	ULONGLONG start = GetTickCount64();
	while (GetTickCount64() - p->last_output < 150 && GetTickCount64() - start < 2000)
		Sleep(25);
	pty_close_console(p);
}

void pty_on_master_close(struct compat_pty *p)
{
	/* Unix: closing the master hangs up the slave and the session gets
	 * SIGHUP. Here: closing the pseudoconsole sends CTRL_CLOSE_EVENT to
	 * every process attached to it. Do it off-thread, it can block. */
	InterlockedIncrement(&p->refs);
	HANDLE t = CreateThread(NULL, 0, closer, p, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		closer(p);
	pty_unref(p); /* the vfd's reference */
}

int pty_get_winsize(struct compat_pty *p, struct winsize *ws)
{
	*ws = p->ws;
	return 0;
}

int pty_resize(struct compat_pty *p, const struct winsize *ws)
{
	if (!ws->ws_row || !ws->ws_col)
		return 0;
	if (ws->ws_row == p->ws.ws_row && ws->ws_col == p->ws.ws_col)
		return 0;
	p->ws = *ws;
	p->resized = 1;
	if (p->closed)
		return 0;
	COORD c = { (SHORT)ws->ws_col, (SHORT)ws->ws_row };
	return FAILED(pResizePseudoConsole(p->hpc, c)) ? (errno = EIO, -1) : 0;
}

/*
 * SIGWINCH has no Windows counterpart: a console app learns about a resize
 * from a WINDOW_BUFFER_SIZE_EVENT that ResizePseudoConsole() already queued.
 * What dtach/abduco really want from "SIGWINCH after attach" is a *redraw*.
 * ConPTY keeps the whole screen and repaints everything when its size
 * changes, so if no resize happened since the last request we force one by
 * wiggling the height by one row.
 */
int pty_repaint(struct compat_pty *p)
{
	if (p->closed || !p->ws.ws_row)
		return 0;
	if (p->resized) {
		p->resized = 0;
		return 0;
	}
	COORD tmp = { (SHORT)p->ws.ws_col, (SHORT)(p->ws.ws_row + 1) };
	COORD real = { (SHORT)p->ws.ws_col, (SHORT)p->ws.ws_row };
	pResizePseudoConsole(p->hpc, tmp);
	pResizePseudoConsole(p->hpc, real);
	return 0;
}

/* ---- environment block -------------------------------------------------- */

static int env_key_eq(const wchar_t *entry, const wchar_t *key)
{
	size_t kl = wcslen(key);
	return _wcsnicmp(entry, key, kl) == 0 && entry[kl] == L'=';
}

/* Current environment + overrides, as a CREATE_UNICODE_ENVIRONMENT block. */
static wchar_t *build_env(const char *const *pairs)
{
	if (!pairs || !pairs[0])
		return NULL; /* inherit as is */
	size_t npairs = 0;
	while (pairs[npairs * 2])
		npairs++;
	wchar_t **keys = calloc(npairs, sizeof *keys);
	wchar_t **lines = calloc(npairs, sizeof *lines);
	size_t extra = 0;
	for (size_t i = 0; i < npairs; i++) {
		keys[i] = utf8_to_wide(pairs[2 * i]);
		wchar_t *v = utf8_to_wide(pairs[2 * i + 1] ? pairs[2 * i + 1] : "");
		size_t l = wcslen(keys[i]) + wcslen(v) + 2;
		lines[i] = malloc(l * sizeof(wchar_t));
		swprintf(lines[i], l, L"%s=%s", keys[i], v);
		extra += l;
		free(v);
	}
	wchar_t *cur = GetEnvironmentStringsW(), *e;
	size_t curlen = 0;
	for (e = cur; *e; e += wcslen(e) + 1)
		curlen += wcslen(e) + 1;
	wchar_t *blk = malloc((curlen + extra + 1) * sizeof(wchar_t)), *o = blk;
	for (e = cur; *e; e += wcslen(e) + 1) {
		int skip = 0;
		for (size_t i = 0; i < npairs; i++)
			if (env_key_eq(e, keys[i]))
				skip = 1;
		if (skip)
			continue;
		size_t l = wcslen(e) + 1;
		memcpy(o, e, l * sizeof(wchar_t));
		o += l;
	}
	for (size_t i = 0; i < npairs; i++) {
		size_t l = wcslen(lines[i]) + 1;
		memcpy(o, lines[i], l * sizeof(wchar_t));
		o += l;
		free(lines[i]);
		free(keys[i]);
	}
	*o = 0;
	FreeEnvironmentStringsW(cur);
	free(keys);
	free(lines);
	return blk;
}

static int win_to_errno(DWORD e)
{
	switch (e) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_NAME:    return ENOENT;
	case ERROR_ACCESS_DENIED:   return EACCES;
	case ERROR_BAD_EXE_FORMAT:  return ENOEXEC;
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:     return ENOMEM;
	case ERROR_DIRECTORY:       return ENOTDIR;
	default:                    return EIO;
	}
}

pid_t compat_spawn_pty(const struct compat_spawn_opts *o, int *master_fd)
{
	HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL;
	HPCON hpc = NULL;
	SOCKET sv[2] = { INVALID_SOCKET, INVALID_SOCKET };
	LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
	wchar_t *app = NULL, *cmd = NULL, *cwd = NULL, *env = NULL;
	HANDLE inherit[2];
	DWORD ninherit = 0;
	struct compat_pty *p = NULL;
	int err = EIO;
	PROCESS_INFORMATION pi = { 0 };

	compat_init_once();
	if (!load_conpty()) {
		errno = ENOSYS; /* Windows < 10 1809 */
		return -1;
	}
	app = resolve_executable(o->argv[0]);
	if (!app) {
		errno = ENOENT;
		return -1;
	}
	cmd = build_command_line(o->argv);
	if (o->cwd)
		cwd = utf8_to_wide(o->cwd);
	env = build_env(o->env);

	if (!CreatePipe(&in_r, &in_w, NULL, 0) || !CreatePipe(&out_r, &out_w, NULL, 0))
		goto fail_win;

	COORD size = { (SHORT)(o->ws.ws_col ? o->ws.ws_col : 80),
	               (SHORT)(o->ws.ws_row ? o->ws.ws_row : 25) };
	if (FAILED(pCreatePseudoConsole(size, in_r, out_w, 0, &hpc)))
		goto fail_win;
	/* conhost duplicated these; keeping our copies would keep the pipes
	 * alive after conhost exits and we would never see EOF. */
	CloseHandle(in_r); in_r = NULL;
	CloseHandle(out_w); out_w = NULL;

	STARTUPINFOEXW si;
	memset(&si, 0, sizeof si);
	si.StartupInfo.cb = sizeof si;

	/*
	 * Optional stdin/stdout redirection (dvtm's copy-mode editor reads the
	 * scrollback from a pipe while drawing on the pty). With
	 * STARTF_USESTDHANDLES the child takes ALL three handles from us; a
	 * NULL one is replaced by the pseudoconsole's own handle when the
	 * process gets a new console.
	 */
	/*
	 * ALWAYS set STARTF_USESTDHANDLES, even without redirection. Without
	 * it, a console child of a parent whose stdio are pipes/files (a
	 * daemon, a CI job, `dtach -N > log`) inherits the parent's std handle
	 * values and writes *there*, bypassing the pseudoconsole entirely.
	 * Measured: `dtach -N s cmd /c echo hi > out.txt` put "hi" in out.txt.
	 */
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	if (o->stdin_fd >= 0 || o->stdout_fd >= 0) {
		if (o->stdin_fd >= 0) {
			HANDLE h = (HANDLE)_get_osfhandle(o->stdin_fd);
			DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(),
			                &si.StartupInfo.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS);
			inherit[ninherit++] = si.StartupInfo.hStdInput;
		}
		if (o->stdout_fd >= 0) {
			HANDLE h = (HANDLE)_get_osfhandle(o->stdout_fd);
			DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(),
			                &si.StartupInfo.hStdOutput, 0, TRUE, DUPLICATE_SAME_ACCESS);
			inherit[ninherit++] = si.StartupInfo.hStdOutput;
		}
	}

	SIZE_T asize = 0;
	DWORD nattr = ninherit ? 2 : 1;
	InitializeProcThreadAttributeList(NULL, nattr, 0, &asize);
	attrs = malloc(asize);
	if (!attrs || !InitializeProcThreadAttributeList(attrs, nattr, 0, &asize))
		goto fail_win;
	if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
	                               hpc, sizeof hpc, NULL, NULL))
		goto fail_win;
	/* Inherit exactly the redirected handles, never "everything that
	 * happens to be inheritable" (the Windows version of the CLOEXEC bug). */
	if (ninherit && !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
	                                          inherit, ninherit * sizeof(HANDLE), NULL, NULL))
		goto fail_win;
	si.lpAttributeList = attrs;

	if (!CreateProcessW(app, cmd, NULL, NULL, ninherit ? TRUE : FALSE,
	                    EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
	                    env, cwd, &si.StartupInfo, &pi))
		goto fail_win;

	for (DWORD i = 0; i < ninherit; i++)
		CloseHandle(inherit[i]);
	ninherit = 0;

	if (socketpair_raw(sv) < 0) {
		err = errno;
		TerminateProcess(pi.hProcess, 1);
		goto fail;
	}

	p = calloc(1, sizeof *p);
	InitializeCriticalSection(&p->lock);
	p->hpc = hpc;
	p->in_w = in_w;
	p->out_r = out_r;
	p->pump = sv[1];
	p->ws.ws_col = (unsigned short)size.X;
	p->ws.ws_row = (unsigned short)size.Y;
	p->last_output = GetTickCount64();
	p->refs = 4; /* vfd, out pump, in pump, child waiter */

	int fd = vfd_register_socket(sv[0]);
	if (fd < 0) {
		err = errno;
		TerminateProcess(pi.hProcess, 1);
		goto fail;
	}
	vfd_get(fd)->pty = p;

	HANDLE t1 = CreateThread(NULL, 0, out_pump, p, 0, NULL);
	HANDLE t2 = CreateThread(NULL, 0, in_pump, p, 0, NULL);
	if (t1) CloseHandle(t1);
	if (t2) CloseHandle(t2);
	CloseHandle(pi.hThread);
	child_register(pi.hProcess, pi.dwProcessId, p);

	DeleteProcThreadAttributeList(attrs);
	free(attrs); free(app); free(cmd); free(cwd); free(env);
	*master_fd = fd;
	return (pid_t)pi.dwProcessId;

fail_win:
	err = win_to_errno(GetLastError());
fail:
	for (DWORD i = 0; i < ninherit; i++)
		CloseHandle(inherit[i]);
	if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
	if (hpc) pClosePseudoConsole(hpc);
	if (in_r) CloseHandle(in_r);
	if (in_w) CloseHandle(in_w);
	if (out_r) CloseHandle(out_r);
	if (out_w) CloseHandle(out_w);
	if (sv[0] != INVALID_SOCKET) closesocket(sv[0]);
	if (sv[1] != INVALID_SOCKET) closesocket(sv[1]);
	if (attrs) { DeleteProcThreadAttributeList(attrs); free(attrs); }
	free(p); free(app); free(cmd); free(cwd); free(env);
	errno = err;
	return -1;
}
