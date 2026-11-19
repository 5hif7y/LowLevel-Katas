/*
 * proc.c - children, waitpid(), kill(), and the "fork() to daemonize"
 * replacement.
 */
#include "compat_internal.h"

void pty_child_exited(struct compat_pty *p);

/* ---- strings ------------------------------------------------------------ */

wchar_t *utf8_to_wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *w = malloc((n ? n : 1) * sizeof *w);
	if (!w)
		return NULL;
	if (!n)
		w[0] = 0;
	else
		MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
	return w;
}

char *wide_to_utf8(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	char *s = malloc(n ? n : 1);
	if (!s)
		return NULL;
	if (!n)
		s[0] = 0;
	else
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
	return s;
}

/*
 * Quote one argument following the rules CommandLineToArgvW()/the MSVC CRT
 * use to split it again: backslashes are literal unless they precede a
 * double quote; 2n backslashes + " -> n backslashes + end/begin of quotes.
 */
static void append_arg(wchar_t **buf, size_t *len, size_t *cap, const wchar_t *a)
{
	size_t need = *len + wcslen(a) * 2 + 4;
	if (need > *cap) {
		*cap = need * 2;
		*buf = realloc(*buf, *cap * sizeof(wchar_t));
	}
	wchar_t *o = *buf + *len;
	if (*len)
		*o++ = L' ';
	int quote = !*a || wcspbrk(a, L" \t\n\v\"") != NULL;
	if (!quote) {
		wcscpy(o, a);
		o += wcslen(a);
	} else {
		*o++ = L'"';
		for (const wchar_t *p = a;; p++) {
			size_t bs = 0;
			while (*p == L'\\') { p++; bs++; }
			if (!*p) {
				for (size_t i = 0; i < bs * 2; i++) *o++ = L'\\';
				break;
			} else if (*p == L'"') {
				for (size_t i = 0; i < bs * 2 + 1; i++) *o++ = L'\\';
				*o++ = L'"';
			} else {
				for (size_t i = 0; i < bs; i++) *o++ = L'\\';
				*o++ = *p;
			}
		}
		*o++ = L'"';
	}
	*o = 0;
	*len = (size_t)(o - *buf);
}

wchar_t *build_command_line(const char *const *argv)
{
	size_t len = 0, cap = 256;
	wchar_t *buf = malloc(cap * sizeof *buf);
	buf[0] = 0;
	for (; *argv; argv++) {
		wchar_t *w = utf8_to_wide(*argv);
		append_arg(&buf, &len, &cap, w);
		free(w);
	}
	return buf;
}

/*
 * execvp() searched PATH. CreateProcessW() with lpApplicationName == NULL
 * also searches, but it parses the *command line* to find the program
 * (C:\Program Files\x.exe -> tries C:\Program.exe first) and only appends
 * ".exe". Resolve explicitly: PATH x PATHEXT, so "dvtm", "vim.bat" or
 * "powershell" all work.
 */
wchar_t *resolve_executable(const char *name)
{
	wchar_t *w = utf8_to_wide(name), out[MAX_PATH * 2];
	wchar_t exts[512] = L".COM;.EXE;.BAT;.CMD";
	GetEnvironmentVariableW(L"PATHEXT", exts, 512);
	int has_ext = wcsrchr(w, L'.') && wcsrchr(w, L'.') > wcsrchr(w, L'\\') &&
	              wcsrchr(w, L'.') > wcsrchr(w, L'/');
	if (has_ext && SearchPathW(NULL, w, NULL, MAX_PATH * 2, out, NULL)) {
		free(w);
		return _wcsdup(out);
	}
	wchar_t *ctx = NULL, *e = wcstok(exts, L";", &ctx);
	for (; e; e = wcstok(NULL, L";", &ctx)) {
		if (SearchPathW(NULL, w, e, MAX_PATH * 2, out, NULL)) {
			DWORD a = GetFileAttributesW(out);
			if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
				free(w);
				return _wcsdup(out);
			}
		}
	}
	free(w);
	return NULL;
}

/* ---- children ----------------------------------------------------------- */

struct child {
	struct child *next;
	HANDLE proc;
	DWORD pid;
	DWORD code;
	int exited;
	struct compat_pty *pty;
};

static struct child *children;
static CRITICAL_SECTION clock_;
static INIT_ONCE child_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK child_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o; (void)p; (void)c;
	InitializeCriticalSection(&clock_);
	return TRUE;
}

static DWORD WINAPI waiter(void *arg)
{
	struct child *c = arg;
	DWORD code = 0;
	WaitForSingleObject(c->proc, INFINITE);
	GetExitCodeProcess(c->proc, &code);
	EnterCriticalSection(&clock_);
	c->code = code;
	c->exited = 1;
	LeaveCriticalSection(&clock_);
	compat_signal_post(C_SIGCHLD);
	if (c->pty)
		pty_child_exited(c->pty);
	return 0;
}

void child_register(HANDLE proc, DWORD pid, struct compat_pty *pty)
{
	InitOnceExecuteOnce(&child_once, child_init, NULL, NULL);
	struct child *c = calloc(1, sizeof *c);
	c->proc = proc;
	c->pid = pid;
	c->pty = pty;
	EnterCriticalSection(&clock_);
	c->next = children;
	children = c;
	LeaveCriticalSection(&clock_);
	HANDLE t = CreateThread(NULL, 0, waiter, c, 0, NULL);
	if (t)
		CloseHandle(t);
}

/*
 * Exit status encoding. Windows has a 32-bit exit code and no "killed by
 * signal". We give one back:
 *   0xE0DE00nn       our own TerminateProcess(.., 0xE0DE0000|sig) -> signaled
 *   0xC000013A       STATUS_CONTROL_C_EXIT                       -> SIGINT
 *   0xC0000005 etc.  NTSTATUS crash codes                        -> SIGSEGV...
 *   anything else    exited, WEXITSTATUS = low 8 bits
 */
#define KILL_TAG 0xE0DE0000u
static int encode_status(DWORD code)
{
	if ((code & 0xFFFF0000u) == KILL_TAG)
		return (int)(code & 0x7f);
	switch (code) {
	case 0xC000013A: return 2;                /* SIGINT  */
	case 0xC0000005: case 0xC00000FD: return 11; /* SIGSEGV */
	case 0xC000001D: return 4;                /* SIGILL  */
	case 0xC0000094: case 0xC000008E: return 8;  /* SIGFPE  */
	case 0xC0000409: return 6;                /* fail-fast -> SIGABRT-ish */
	}
	return (int)((code & 0xff) << 8);
}

pid_t compat_waitpid(pid_t pid, int *status, int options)
{
	InitOnceExecuteOnce(&child_once, child_init, NULL, NULL);
	for (;;) {
		HANDLE live[MAXIMUM_WAIT_OBJECTS];
		DWORD nlive = 0;
		int any = 0;
		EnterCriticalSection(&clock_);
		for (struct child **pp = &children, *c; (c = *pp);) {
			if (pid > 0 && c->pid != (DWORD)pid) {
				pp = &c->next;
				continue;
			}
			any = 1;
			if (c->exited) {
				*pp = c->next;
				LeaveCriticalSection(&clock_);
				if (status)
					*status = encode_status(c->code);
				pid_t r = (pid_t)c->pid;
				CloseHandle(c->proc);
				free(c);
				return r;
			}
			if (nlive < MAXIMUM_WAIT_OBJECTS)
				live[nlive++] = c->proc;
			pp = &c->next;
		}
		LeaveCriticalSection(&clock_);
		if (!any) {
			errno = ECHILD;
			return -1;
		}
		if (options & 1 /* WNOHANG */)
			return 0;
		WaitForMultipleObjects(nlive, live, FALSE, INFINITE);
		Sleep(1); /* let the waiter thread record the exit code */
	}
}

int child_kill(pid_t pid, int sig)
{
	struct child *found = NULL;
	InitOnceExecuteOnce(&child_once, child_init, NULL, NULL);
	EnterCriticalSection(&clock_);
	for (struct child *c = children; c; c = c->next)
		if (c->pid == (DWORD)pid)
			found = c;
	LeaveCriticalSection(&clock_);

	if (sig == 0) {
		if (found)
			return found->exited ? (errno = ESRCH, -1) : 0;
		HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
		if (!h) {
			errno = ESRCH;
			return -1;
		}
		DWORD r = WaitForSingleObject(h, 0);
		CloseHandle(h);
		return r == WAIT_TIMEOUT ? 0 : (errno = ESRCH, -1);
	}
	switch (sig) {
	case C_SIGWINCH:
		if (found && found->pty)
			return pty_repaint(found->pty);
		return 0;
	case C_SIGCHLD: case C_SIGCONT: case C_SIGTSTP:
	case C_SIGTTIN: case C_SIGTTOU: case C_SIGSTOP:
		return 0;
	}
	/* Everything else terminates. There is no way to deliver a catchable
	 * signal to another process; TerminateProcess() is SIGKILL. */
	HANDLE h = found ? found->proc : OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
	if (!h) {
		errno = ESRCH;
		return -1;
	}
	BOOL ok = TerminateProcess(h, KILL_TAG | (DWORD)sig);
	if (!found)
		CloseHandle(h);
	if (!ok) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

/* ---- daemonizing without fork() ----------------------------------------- */

static char *daemon_role;
static HANDLE daemon_status = INVALID_HANDLE_VALUE;

/* Called once from compat_init(): consume and REMOVE the variables, or the
 * program running inside the session would inherit them and believe it is
 * a daemon too (dtach inside dtach). */
void proc_init_daemon_env(void)
{
	char *role = getenv("COMPAT_DAEMON_ROLE");
	char *st = getenv("COMPAT_STATUS_HANDLE");
	if (role && *role)
		daemon_role = _strdup(role);
	if (st && *st) {
		daemon_status = (HANDLE)(intptr_t)_strtoui64(st, NULL, 16);
		SetHandleInformation(daemon_status, HANDLE_FLAG_INHERIT, 0);
	}
	_putenv("COMPAT_DAEMON_ROLE=");
	_putenv("COMPAT_STATUS_HANDLE=");
}

const char *compat_daemon_role_raw(void)
{
	return daemon_role ? daemon_role : "(none)";
}

const char *compat_daemon_role(void)
{
	compat_init_once();
	return daemon_role;
}

int compat_daemon_statusfd(void)
{
	compat_init_once();
	if (daemon_status == INVALID_HANDLE_VALUE)
		return -1;
	int fd = _open_osfhandle((intptr_t)daemon_status, _O_WRONLY | _O_BINARY);
	compat_trace("statusfd: handle=%p -> fd %d", (void *)daemon_status, fd);
	daemon_status = INVALID_HANDLE_VALUE;
	return fd;
}

/*
 * fork() + setsid() + (double fork) for a daemon becomes:
 *   CreateProcess(our own .exe, same argv, DETACHED_PROCESS)
 *   + an inherited pipe that the daemon writes errors to and closes when
 *     it is ready (the exact role of dtach's statusfd / abduco's
 *     client_pipe).
 * The child knows its role from COMPAT_DAEMON_ROLE and re-parses argv.
 */
pid_t compat_spawn_self_detached(const char *role, const char *const *argv,
                                 int *status_rd)
{
	HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	wchar_t exe[MAX_PATH * 2], *cmd = NULL;
	LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
	PROCESS_INFORMATION pi = { 0 };
	char hex[32];
	int ok = 0;

	compat_init_once();
	if (!CreatePipe(&rd, &wr, &sa, 0))
		goto out;
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	cmd = build_command_line(argv);

	/*
	 * The daemon must not hold our console, but it does need *valid*
	 * stdio: started with NULL std handles, the UCRT binds stdout to the
	 * pseudo-descriptor -2, and a later dup2(statusfd, 1) + printf() writes
	 * into the void (and freopen() would pick fd 4, not 1). Hand it NUL.
	 */
	nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
	                  FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
	HANDLE inh[2] = { wr, nul };

	SIZE_T asize = 0;
	InitializeProcThreadAttributeList(NULL, 1, 0, &asize);
	attrs = malloc(asize);
	InitializeProcThreadAttributeList(attrs, 1, 0, &asize);
	UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
	                          inh, sizeof inh, NULL, NULL);

	STARTUPINFOEXW si;
	memset(&si, 0, sizeof si);
	si.StartupInfo.cb = sizeof si;
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = nul;
	si.lpAttributeList = attrs;

	/* The environment is the channel: set, spawn, unset. */
	snprintf(hex, sizeof hex, "%llx", (unsigned long long)(uintptr_t)wr);
	SetEnvironmentVariableA("COMPAT_DAEMON_ROLE", role);
	SetEnvironmentVariableA("COMPAT_STATUS_HANDLE", hex);

	DWORD flags = EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS |
	              CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
	/*
	 * Windows Terminal, conhost and ssh put their children in a Job with
	 * JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: close the tab and every process in
	 * the job dies - including our "daemon". Ask to break away; if the job
	 * forbids it CreateProcess fails with ERROR_ACCESS_DENIED: retry
	 * without (the session then lives as long as the terminal).
	 */
	ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE,
	                    flags | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL,
	                    &si.StartupInfo, &pi);
	if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
		ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, flags, NULL, NULL,
		                    &si.StartupInfo, &pi);

	SetEnvironmentVariableA("COMPAT_DAEMON_ROLE", NULL);
	SetEnvironmentVariableA("COMPAT_STATUS_HANDLE", NULL);
out:
	if (wr)
		CloseHandle(wr); /* only the child holds the write end now */
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (attrs) {
		DeleteProcThreadAttributeList(attrs);
		free(attrs);
	}
	free(cmd);
	if (!ok) {
		if (rd)
			CloseHandle(rd);
		errno = EAGAIN;
		return -1;
	}
	CloseHandle(pi.hThread);
	child_register(pi.hProcess, pi.dwProcessId, NULL);
	*status_rd = _open_osfhandle((intptr_t)rd, _O_RDONLY | _O_BINARY);
	return (pid_t)pi.dwProcessId;
}
