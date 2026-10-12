#ifndef _COMPAT_UNISTD_H_
#define _COMPAT_UNISTD_H_

#include <sys/cdefs.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

__BEGIN_DECLS

/* Standard file descriptors */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* Access check modes */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif

/* POSIX types */
#if !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T_)
#define _SSIZE_T_DEFINED
#define _SSIZE_T_
typedef intptr_t ssize_t;
#endif

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif

#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
#endif

#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef unsigned int gid_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef __int64 off_t;
#endif

/* POSIX getopt */
extern char *optarg;
extern int optind, opterr, optopt, optreset;
int getopt(int argc, char * const argv[], const char *optstring);

/* Functions */
#ifndef isatty
#define isatty _isatty
#endif
#ifndef fileno
#define fileno _fileno
#endif
#ifndef unlink
#define unlink _unlink
#endif
#ifndef rmdir
#define rmdir _rmdir
#endif
#ifndef chdir
#define chdir _chdir
#endif
#ifndef getcwd
#define getcwd _getcwd
#endif
#ifndef access
#define access _access
#endif
#ifndef getuid
#define getuid() 1000
#endif

int readlink(const char *path, char *buf, size_t bufsiz);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int ftruncate(int fd, int64_t length);

__END_DECLS

#endif /* _COMPAT_UNISTD_H_ */
