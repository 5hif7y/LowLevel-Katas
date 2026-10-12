/*
 * Sonda: ¿sirve PeekNamedPipe para detectar un pipe roto en stdout?
 *
 * Hipótesis a refutar: PeekNamedPipe sobre el handle de stdout (que es
 * write-only) falla SIEMPRE, esté el pipe sano o roto, porque la función
 * requiere acceso de lectura. Si eso se confirma, PeekNamedPipe no sirve como
 * discriminante y la detección tiene que apoyarse en otro criterio.
 *
 * Uso:
 *   probe_peek_healthy.exe | cat        # consumidor que NO cierra el pipe
 *   probe_peek_healthy.exe | head -n 1  # consumidor que lo cierra enseguida
 *
 * Compilar (MSVC):
 *   cl /nologo /W3 /Fe:probe_peek_healthy.exe probe_peek_healthy.c
 */
#include <stdio.h>
#include <io.h>
#include <windows.h>

int
main(void)
{
	intptr_t	osfh;
	HANDLE		h;
	DWORD		avail = 0, err, ftype;
	BOOL		ok;

	osfh = _get_osfhandle(_fileno(stdout));
	h = (HANDLE)osfh;
	ftype = GetFileType(h);

	SetLastError(0);
	ok = PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL);
	err = GetLastError();

	fprintf(stderr, "[peek] handle          = %lld\n", (long long)osfh);
	fprintf(stderr, "[peek] GetFileType     = %lu (%s)\n",
	    (unsigned long)ftype,
	    ftype == FILE_TYPE_PIPE ? "FILE_TYPE_PIPE" :
	    ftype == FILE_TYPE_CHAR ? "FILE_TYPE_CHAR" :
	    ftype == FILE_TYPE_DISK ? "FILE_TYPE_DISK" : "otro");
	fprintf(stderr, "[peek] PeekNamedPipe   = %s  avail=%lu  GetLastError=%lu\n",
	    ok ? "TRUE" : "FALSE", (unsigned long)avail, (unsigned long)err);
	fprintf(stderr, "[peek] veredicto      = %s\n",
	    ok ? "PeekNamedPipe SIRVE sobre este handle"
	       : "PeekNamedPipe NO SIRVE (falla aunque el pipe este sano)");

	/* Dejamos algo en stdout para que el consumidor tenga qué leer. */
	puts("listo");
	return 0;
}
