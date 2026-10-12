#ifndef _COMPAT_H_
#define _COMPAT_H_

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE 1
#endif

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <err.h>
#include <pledge.h>
#include <unveil.h>

extern char *__progname;
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef _POSIX2_LINE_MAX
#define _POSIX2_LINE_MAX 2048
#endif

#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif

#ifndef fseeko
#define fseeko _fseeki64
#endif
#ifndef ftello
#define ftello _ftelli64
#endif
#ifndef fpurge
static inline int fpurge(FILE *fp) {
    return fflush(fp);
}
#endif

#ifndef bzero
#define bzero(b, len) memset((b), 0, (len))
#endif
#ifndef bcopy
#define bcopy(src, dst, len) memmove((dst), (src), (len))
#endif
#ifndef bcmp
#define bcmp(b1, b2, len) memcmp((b1), (b2), (len))
#endif

__BEGIN_DECLS
const char *user_from_uid(uid_t uid, int nouser);
const char *group_from_gid(gid_t gid, int nogroup);
char *fflagstostr(unsigned int flags);
void *reallocarray(void *optr, size_t nmemb, size_t size);
void *recallocarray(void *ptr, size_t oldnmemb, size_t newnmemb, size_t size);
void *bsd_setmode(const char *mode_str);
mode_t bsd_getmode(const void *set, mode_t mode);
#define setmode bsd_setmode
#define getmode bsd_getmode
__END_DECLS

/* Fallback for st_blksize which is absent in MSVC struct _stat */
#ifndef st_blksize
#define st_blksize st_rdev
#endif

/* Fallback for O_BINARY on POSIX-oriented code */
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#ifndef O_TEXT
#define O_TEXT _O_TEXT
#endif

/* Safe fclose for standard streams on Windows */
static inline int compat_fclose(FILE *fp) {
    if (fp == stdout || fp == stderr || fp == stdin) {
        return fflush(fp);
    }
    return fclose(fp);
}
#ifndef fclose
#define fclose compat_fclose
#endif

/* Windows VT100 / Console Init helper */
__BEGIN_DECLS
void compat_enable_vt100(void);
__END_DECLS

/* --- Emulación de SIGPIPE ----------------------------------------------------
 * Windows no tiene SIGPIPE. Sin esto, un productor infinito como yes(1) —que
 * descarta el valor de retorno de puts() y confía en que el kernel lo mate—
 * gira para siempre cuando el consumidor cierra el pipe. Ver compat/src/sigpipe.c.
 */
#ifndef SIGPIPE
#define SIGPIPE 13
#endif

__BEGIN_DECLS
void	(*compat_signal(int, void (*)(int)))(int);
void	compat_on_write_error(FILE *);
int	compat_puts(const char *);
int	compat_fputs(const char *, FILE *);
int	compat_fputc(int, FILE *);
int	compat_putchar(int);
size_t	compat_fwrite(const void *, size_t, size_t, FILE *);
int	compat_fflush(FILE *);
int	compat_printf(const char *, ...);
int	compat_fprintf(FILE *, const char *, ...);
__END_DECLS

/* --- Modo binario por defecto ------------------------------------------------
 * En Unix no existe el modo texto: la E/S es byte-transparente siempre. Estos
 * envoltorios evitan que el CRT traduzca \n a \r\n y que confunda 0x1A con EOF.
 * Ver compat/src/binary_mode.c.
 */
__BEGIN_DECLS
void	compat_init_binary_mode(void);
int	compat_open(const char *, int, ...);
FILE *	compat_fopen(const char *, const char *);
FILE *	compat_freopen(const char *, const char *, FILE *);
__END_DECLS

#define open(...)		compat_open(__VA_ARGS__)
#define fopen(path, mode)	compat_fopen((path), (mode))
#define freopen(path, mode, fp)	compat_freopen((path), (mode), (fp))

#define signal(sig, handler)	compat_signal((sig), (handler))
#define puts(s)			compat_puts(s)
#define fputs(s, fp)		compat_fputs((s), (fp))
#define fputc(c, fp)		compat_fputc((c), (fp))
#define putchar(c)		compat_putchar(c)
#define fwrite(p, sz, n, fp)	compat_fwrite((p), (sz), (n), (fp))
#define fflush(fp)		compat_fflush(fp)
#define printf(...)		compat_printf(__VA_ARGS__)
#define fprintf(...)		compat_fprintf(__VA_ARGS__)

#endif /* _COMPAT_H_ */
