/*
 * Sonda: ¿en qué estado está el CRT cuando corre un inicializador .CRT$XCU?
 *
 * compat_init.c hace _setmode(_O_BINARY) sobre stdin/stdout/stderr desde un
 * inicializador .CRT$XCU, pero la salida sigue saliendo con \r\n. Esta sonda
 * replica exactamente ese hook y reporta, para cada descriptor estándar:
 *
 *   - si el hook llegó a correr,
 *   - qué devolvía GetFileType en ese momento,
 *   - qué devolvió _setmode (el modo ANTERIOR, o -1 si falló).
 *
 * Uso:
 *   probe_crt_init.exe > archivo.txt 2>&1
 *
 * Compilar (MSVC):
 *   cl /nologo /W3 /Fe:probe_crt_init.exe probe_crt_init.c
 */
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <windows.h>

static int	g_ran;
static int	g_ft[3];
static int	g_rc[3];
static int	g_err[3];

static void __cdecl
my_init(void)
{
	int	fd;

	g_ran = 1;
	for (fd = 0; fd <= 2; fd++) {
		intptr_t	h = _get_osfhandle(fd);

		g_ft[fd] = (h == -1) ? -1 : (int)GetFileType((HANDLE)h);
		errno = 0;
		g_rc[fd] = _setmode(fd, _O_BINARY);
		g_err[fd] = errno;
	}
}

typedef void (__cdecl *initfn)(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static initfn p_my_init = my_init;

int
main(void)
{
	int	fd, now;

	/*
	 * Escribimos a stderr para no contaminar stdout, que es justamente lo
	 * que estamos midiendo.
	 */
	fprintf(stderr, "hook .CRT$XCU ejecutado = %s\n", g_ran ? "SI" : "NO");
	fprintf(stderr, "%-4s %-16s %-22s %s\n",
	    "fd", "GetFileType", "_setmode devolvio", "errno");
	for (fd = 0; fd <= 2; fd++) {
		fprintf(stderr, "%-4d %-16d %-22d %d\n",
		    fd, g_ft[fd], g_rc[fd], g_err[fd]);
	}

	/* _O_BINARY=0x8000, _O_TEXT=0x4000. Lo que devuelve acá es el modo
	 * vigente ANTES de este llamado, o sea el que dejó el hook. */
	errno = 0;
	now = _setmode(1, _O_BINARY);
	fprintf(stderr, "modo vigente de stdout al entrar a main = %d (%s)\n",
	    now, now == _O_BINARY ? "_O_BINARY" :
		 now == _O_TEXT   ? "_O_TEXT" : "otro/ERROR");

	return 0;
}
