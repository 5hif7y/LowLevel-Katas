/*
 * Emulación de SIGPIPE para Windows.
 *
 * En Unix, escribir a un pipe cuyo extremo de lectura ya se cerró hace que el
 * kernel entregue SIGPIPE, y la acción por defecto de SIGPIPE es terminar el
 * proceso. Gracias a eso el código de usuario puede ignorar olímpicamente el
 * valor de retorno de puts()/fputs()/fwrite() y el programa igual muere cuando
 * el consumidor se va. yes(1) de OpenBSD hace exactamente eso:
 *
 *	for (;;)
 *		puts("y");
 *
 * Windows no tiene SIGPIPE. El CRT sí detecta el pipe roto y puts() devuelve
 * EOF, pero como nadie mira ese valor de retorno el bucle se convierte en un
 * giro infinito a CPU completa escribiendo a un descriptor muerto.
 *
 * Este archivo restaura la semántica Unix envolviendo las funciones de salida
 * de stdio: si la escritura falla y el stream es un pipe sin lector, se
 * reproduce la acción por defecto de SIGPIPE — o el handler que haya instalado
 * el programa, si llamó a signal(SIGPIPE, ...).
 *
 * Medición que originó esto (tests/probes/probe_broken_pipe.c, MSVC 14.50):
 *
 *	$ ./probe.exe | head -n 3
 *	[probe] puts() devolvio EOF tras 24577 lineas; errno=22 (Invalid argument)
 *
 * Los 24577 = 12 x 4096 confirman que stdout queda block-buffered sobre el
 * pipe (no line-buffered, como sería en una terminal): el primer error aparece
 * recién cuando se desborda el buffer, no en el primer \n.
 *
 * Dos callejones sin salida que las sondas de tests/probes/ descartaron antes
 * de llegar al criterio final:
 *
 *   1. errno NO sirve. El CRT reporta EINVAL (22), no EPIPE, y EINVAL es
 *      demasiado ambiguo para decidir sobre él.
 *
 *   2. PeekNamedPipe() NO sirve. La idea era preguntarle al pipe si seguía
 *      vivo, pero stdout es un handle write-only y PeekNamedPipe exige acceso
 *      de lectura: falla con ERROR_ACCESS_DENIED (5) tanto sobre un pipe sano
 *      como sobre uno roto (ver tests/probes/probe_peek_healthy.c). Como
 *      discriminante no distingue nada.
 *
 * El criterio que sí funciona es no preguntar nada: cuando esta función corre,
 * la escritura YA falló. Un fallo de escritura sobre un pipe sólo tiene dos
 * causas, y ninguna se parece a un error transitorio:
 *
 *   - el extremo de lectura se cerró, o
 *   - el pipe está lleno en modo no bloqueante (EAGAIN), que sí es recuperable
 *     y por eso se excluye explícitamente.
 *
 * Alcanza entonces con clasificar el handle (GetFileType no necesita permiso de
 * lectura) y descartar EAGAIN.
 */

#include <compat.h>
#include <windows.h>
#include <io.h>
#include <signal.h>
#include <stdarg.h>

/* compat.h ya incluyó <stdio.h>; nos sacamos de encima los envoltorios para
 * poder llamar a las funciones reales desde acá. */
#undef puts
#undef fputs
#undef fputc
#undef putchar
#undef fwrite
#undef fflush
#undef printf
#undef fprintf
#undef signal

static void	(*sigpipe_handler)(int) = SIG_DFL;

/*
 * signal() de reemplazo. El CRT no conoce SIGPIPE, así que ese número se
 * registra acá; cualquier otra señal se reenvía tal cual (sleep(1) depende de
 * SIGALRM y tee(1) de SIGINT).
 */
void
(*compat_signal(int sig, void (*handler)(int)))(int)
{
	void	(*prev)(int);

	if (sig != SIGPIPE)
		return signal(sig, handler);

	prev = sigpipe_handler;
	sigpipe_handler = handler;
	return prev;
}

/*
 * ¿El fallo de escritura que nos trajo hasta acá fue un pipe sin lector?
 *
 * Se llama únicamente después de que una escritura falló y ferror() quedó
 * activo, así que la pregunta real es sólo si el destino es un pipe: sobre un
 * pipe, una escritura no puede fallar por falta de espacio ni por I/O de disco.
 */
static int
is_broken_pipe(FILE *fp)
{
	intptr_t	osfh;

	if (fp == NULL)
		return 0;

	/* Pipe lleno en modo no bloqueante: recuperable, no es SIGPIPE. */
	if (errno == EAGAIN)
		return 0;

	if ((osfh = _get_osfhandle(_fileno(fp))) == -1)
		return 0;

	/* GetFileType sólo clasifica el handle; no necesita permiso de lectura. */
	return GetFileType((HANDLE)osfh) == FILE_TYPE_PIPE;
}

/*
 * Punto único de decisión, invocado por cada envoltorio cuando la escritura
 * falló. Reproduce lo que haría el kernel en Unix.
 */
void
compat_on_write_error(FILE *fp)
{
	if (fp == NULL || !ferror(fp) || !is_broken_pipe(fp))
		return;			/* no fue un pipe roto: que el programa decida */

	if (sigpipe_handler == SIG_IGN)
		return;			/* el programa pidió ignorar SIGPIPE */

	if (sigpipe_handler != SIG_DFL) {
		sigpipe_handler(SIGPIPE);	/* handler propio del programa */
		return;
	}

	/*
	 * Acción por defecto: terminar sin flush (en Unix SIGPIPE no da chance
	 * de vaciar buffers). 141 = 128 + SIGPIPE, que es el código que reporta
	 * el shell para un proceso muerto por SIGPIPE.
	 */
	_exit(141);
}

int
compat_puts(const char *s)
{
	int	rc = puts(s);

	if (rc == EOF)
		compat_on_write_error(stdout);
	return rc;
}

int
compat_fputs(const char *s, FILE *fp)
{
	int	rc = fputs(s, fp);

	if (rc == EOF)
		compat_on_write_error(fp);
	return rc;
}

int
compat_fputc(int c, FILE *fp)
{
	int	rc = fputc(c, fp);

	if (rc == EOF)
		compat_on_write_error(fp);
	return rc;
}

int
compat_putchar(int c)
{
	return compat_fputc(c, stdout);
}

size_t
compat_fwrite(const void *p, size_t sz, size_t n, FILE *fp)
{
	size_t	rc = fwrite(p, sz, n, fp);

	if (rc < n)
		compat_on_write_error(fp);
	return rc;
}

int
compat_fflush(FILE *fp)
{
	int	rc = fflush(fp);

	if (rc == EOF)
		compat_on_write_error(fp);
	return rc;
}

int
compat_printf(const char *fmt, ...)
{
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vprintf(fmt, ap);
	va_end(ap);

	if (rc < 0)
		compat_on_write_error(stdout);
	return rc;
}

int
compat_fprintf(FILE *fp, const char *fmt, ...)
{
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vfprintf(fp, fmt, ap);
	va_end(ap);

	if (rc < 0)
		compat_on_write_error(fp);
	return rc;
}
