/*
 * Modo binario por defecto: devolverle a Windows la semántica de E/S de Unix.
 *
 * En Unix un archivo es una secuencia indiferenciada de bytes y no existe el
 * modo texto. En el CRT de Windows, en cambio, tanto los streams estándar como
 * los archivos abiertos con open(2)/fopen(3) arrancan en _O_TEXT, donde el CRT
 * traduce en silencio:
 *
 *	lectura  :  0x0D 0x0A  ->  0x0A    y 0x1A pasa a ser EOF
 *	escritura:  0x0A       ->  0x0D 0x0A
 *
 * Sin corregirlo, cat(1) trunca cualquier binario en su primer 0x1A —y con él
 * cualquier checksum—, y tee(1) escribe más bytes de los que leyó. Ver
 * Docs/porter-guide/03-el-sistema-io-y-modos-binario-texto.md.
 *
 * Se ataca por dos frentes:
 *
 *  1. Los streams estándar pasan a binario al arrancar, pero SOLO si no están
 *     conectados a la consola. Un pipe o una redirección tienen que ser
 *     byte-exactos —ahí está la corrección que importa—, mientras que la
 *     consola interactiva se deja en modo texto para que \n se siga viendo
 *     como un salto de línea y no como un peldaño de escalera.
 *
 *  2. open(2), fopen(3) y freopen(3) fuerzan binario salvo que el llamador pida
 *     _O_TEXT explícitamente. En Unix no hay nada que pedir: binario ES el
 *     comportamiento por defecto, así que emularlo es exactamente esto.
 *
 * Sin el punto 2, `cat crlf.txt | wc -c` devuelve 4 en vez de 6: cat abre el
 * archivo en modo texto, el CRT se come los \r, y lo que sale por el pipe ya
 * está mutilado aunque el pipe mismo sea binario.
 */

#include <compat.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

/* Estos envoltorios se llaman entre sí: nos sacamos las macros de encima. */
#undef open
#undef fopen
#undef freopen

/*
 * ¿El descriptor apunta a la consola?
 * GetFileType devuelve FILE_TYPE_CHAR para la consola, FILE_TYPE_PIPE para un
 * pipe y FILE_TYPE_DISK para un archivo o una redirección.
 */
static int
is_console(int fd)
{
	intptr_t	osfh = _get_osfhandle(fd);

	if (osfh == -1)
		return 0;
	return GetFileType((HANDLE)osfh) == FILE_TYPE_CHAR;
}

/*
 * Pasa a binario los streams estándar que no sean la consola.
 * Lo invoca compat_init.c desde el inicializador .CRT$XCU, antes de main().
 */
void
compat_init_binary_mode(void)
{
	int	fd;

	for (fd = 0; fd <= 2; fd++) {
		if (!is_console(fd))
			(void)_setmode(fd, _O_BINARY);
	}
}

/*
 * Modo de fopen(3) con 'b' garantizada: se descarta cualquier 't' explícita y,
 * si no quedó una 'b', se agrega al final. Esa posición es válida en todas las
 * combinaciones que acepta el CRT: "r" -> "rb", "w+" -> "w+b", "a+" -> "a+b".
 */
static void
binary_fopen_mode(const char *mode, char *out, size_t outsz)
{
	size_t	i;

	for (i = 0; mode[i] != '\0' && i + 2 < outsz; i++)
		out[i] = (mode[i] == 't') ? 'b' : mode[i];
	out[i] = '\0';

	if (strchr(out, 'b') == NULL) {
		out[i] = 'b';
		out[i + 1] = '\0';
	}
}

/*
 * open(2). El tercer argumento sólo existe cuando se pasa O_CREAT, así que se
 * lee de la lista varargs únicamente en ese caso.
 */
int
compat_open(const char *path, int flags, ...)
{
	va_list	ap;
	int	mode = 0;

	if ((flags & _O_TEXT) == 0)
		flags |= _O_BINARY;

	if (flags & _O_CREAT) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
		return _open(path, flags, mode);
	}
	return _open(path, flags);
}

FILE *
compat_fopen(const char *path, const char *mode)
{
	char	buf[16];

	binary_fopen_mode(mode, buf, sizeof(buf));
	return fopen(path, buf);
}

FILE *
compat_freopen(const char *path, const char *mode, FILE *fp)
{
	char	buf[16];

	binary_fopen_mode(mode, buf, sizeof(buf));
	return freopen(path, buf, fp);
}
