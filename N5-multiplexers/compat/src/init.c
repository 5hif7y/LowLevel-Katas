/*
 * init.c - process-wide setup that must run before main().
 *
 * Hooked into the CRT initializer table (.CRT$XCU). As learned the hard way
 * in N1 (porter guide, module 10): this object must be linked as part of an
 * OBJECT library, never a static .lib - nothing references it by name, so
 * the linker would silently drop it from a .lib.
 */
#include "compat_internal.h"
#include <locale.h>
#include <stdarg.h>

void proc_init_daemon_env(void);

static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK do_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o; (void)p; (void)c;
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	compat_trace("init: fileno in/out/err = %d %d %d",
	             _fileno(stdin), _fileno(stdout), _fileno(stderr));

	/* Binary stdio: no CRLF translation of terminal byte streams. */
	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
	_setmode(2, _O_BINARY);

	/* mbrtowc()/wcrtomb() must speak UTF-8 (dvtm's vt.c relies on it). */
	setlocale(LC_CTYPE, ".UTF-8");

	proc_init_daemon_env();
	compat_trace("init: role=%s fileno out=%d", compat_daemon_role_raw(), _fileno(stdout));
	/* registered first => runs last, after the program's own handlers */
	atexit(vfd_close_all);
	signal_event(); /* installs the console Ctrl handler */
	return TRUE;
}

void compat_init_once(void)
{
	InitOnceExecuteOnce(&once, do_init, NULL, NULL);
}

static void __cdecl crt_init(void)
{
	compat_init_once();
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (__cdecl *compat_crt_init)(void) = crt_init;

/*
 * setlocale(LC_CTYPE, "") on Windows selects the *ANSI* code page of the
 * user (e.g. Spanish_Argentina.1252), not UTF-8, and dvtm calls it first
 * thing in main(). Redirect the empty name to ".UTF-8".
 */
char *compat_setlocale(int cat, const char *name)
{
	if (name && !*name && (cat == LC_CTYPE || cat == LC_ALL))
		name = ".UTF-8";
	return setlocale(cat, name);
}

/*
 * compat_trace(): printf-style log to the file named by %COMPAT_TRACE%.
 * A detached daemon has no console and no stderr; this is its only voice.
 */
void compat_trace(const char *fmt, ...)
{
	static int checked;
	static char path[MAX_PATH];
	if (!checked) {
		checked = 1;
		if (!GetEnvironmentVariableA("COMPAT_TRACE", path, sizeof path))
			path[0] = 0;
	}
	if (!path[0])
		return;
	FILE *f = fopen(path, "a");
	if (!f)
		return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(f, "[%lu] ", GetCurrentProcessId());
	vfprintf(f, fmt, ap);
	fputc('\n', f);
	va_end(ap);
	fclose(f);
}
