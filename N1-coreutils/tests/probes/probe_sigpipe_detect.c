/*
 * Sonda: ¿por qué no dispara la detección de pipe roto de compat_on_write_error()?
 *
 * Replica paso a paso la lógica de compat/src/sigpipe.c y reporta qué devuelve
 * cada llamada en el momento exacto en que puts() falla. Sirve para averiguar
 * cuál de las condiciones de is_broken_pipe() está rechazando el caso real,
 * en vez de suponerlo.
 *
 * Uso:
 *   probe_sigpipe_detect.exe | head -n 3
 *
 * Compilar (MSVC):
 *   cl /nologo /W3 /Fe:probe_sigpipe_detect.exe probe_sigpipe_detect.c
 */
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <windows.h>

static const char *
filetype_name(DWORD t)
{
	switch (t) {
	case FILE_TYPE_UNKNOWN:	return "FILE_TYPE_UNKNOWN";
	case FILE_TYPE_DISK:	return "FILE_TYPE_DISK";
	case FILE_TYPE_CHAR:	return "FILE_TYPE_CHAR";
	case FILE_TYPE_PIPE:	return "FILE_TYPE_PIPE";
	case FILE_TYPE_REMOTE:	return "FILE_TYPE_REMOTE";
	default:		return "?";
	}
}

int
main(void)
{
	long		n = 0;
	int		rc, fd;
	intptr_t	osfh;
	DWORD		ftype, err;
	HANDLE		h;
	BOOL		peek;
	DWORD		avail = 0;

	for (;;) {
		rc = puts("y");
		n++;

		if (rc != EOF)
			continue;

		/* --- Replicamos is_broken_pipe() reportando cada paso --- */
		printf("\n[detect] puts() == EOF tras %ld lineas\n", n);
		fprintf(stderr, "[detect] errno          = %d (%s)\n", errno, strerror(errno));
		fprintf(stderr, "[detect] ferror(stdout) = %d\n", ferror(stdout));

		fd = _fileno(stdout);
		fprintf(stderr, "[detect] _fileno        = %d\n", fd);

		osfh = _get_osfhandle(fd);
		fprintf(stderr, "[detect] _get_osfhandle = %lld (INVALID_HANDLE_VALUE=%lld)\n",
		    (long long)osfh, (long long)INVALID_HANDLE_VALUE);
		if (osfh == -1) {
			fprintf(stderr, "[detect] => RECHAZADO en _get_osfhandle\n");
			return 1;
		}

		h = (HANDLE)osfh;
		ftype = GetFileType(h);
		fprintf(stderr, "[detect] GetFileType    = %lu (%s)\n",
		    (unsigned long)ftype, filetype_name(ftype));
		if (ftype != FILE_TYPE_PIPE) {
			fprintf(stderr, "[detect] => RECHAZADO: no es FILE_TYPE_PIPE\n");
			return 1;
		}

		SetLastError(0);
		peek = PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL);
		err = GetLastError();
		fprintf(stderr, "[detect] PeekNamedPipe  = %s  avail=%lu  GetLastError=%lu"
		    "  (ERROR_BROKEN_PIPE=%d ERROR_NO_DATA=%d)\n",
		    peek ? "TRUE (pipe vivo)" : "FALSE (pipe roto)",
		    (unsigned long)avail, (unsigned long)err,
		    ERROR_BROKEN_PIPE, ERROR_NO_DATA);

		if (peek) {
			fprintf(stderr, "[detect] => RECHAZADO: PeekNamedPipe dice que el pipe vive\n");
			return 1;
		}
		if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
			fprintf(stderr, "[detect] => ACEPTADO: pipe roto confirmado\n");
			return 0;
		}
		fprintf(stderr, "[detect] => RECHAZADO: error inesperado %lu\n",
		    (unsigned long)err);
		return 1;
	}
}
