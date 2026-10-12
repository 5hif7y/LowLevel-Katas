/*
 * Sonda: ¿qué hace el CRT de MSVC cuando stdout es un pipe cuyo extremo de
 * lectura ya se cerró?
 *
 * Motivación: usr.bin/yes/yes.c ignora el valor de retorno de puts() y confía
 * en que SIGPIPE lo mate cuando el consumidor se va. Windows no tiene SIGPIPE,
 * así que hay que averiguar empíricamente si puts() al menos devuelve EOF.
 *
 * Uso:
 *   probe_broken_pipe.exe            # escribe hasta romperse, reporta qué pasó
 *   probe_broken_pipe.exe | head -n 3
 *
 * Compilar (MSVC):  cl /nologo /W3 /Fe:probe_broken_pipe.exe probe_broken_pipe.c
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

int
main(void)
{
	long		n = 0;
	int		rc;

	for (;;) {
		rc = puts("y");
		n++;

		if (rc == EOF) {
			fprintf(stderr,
			    "\n[probe] puts() devolvio EOF tras %ld lineas; "
			    "errno=%d (%s)\n", n, errno, strerror(errno));
			return 1;
		}

		if (n >= 50000000L) {
			fprintf(stderr,
			    "\n[probe] %ld lineas escritas SIN error. "
			    "puts() nunca reporta el pipe roto.\n", n);
			return 2;
		}
	}
}
