/* unistd.h - shadow header. Everything real lives in compat.h (forced). */
#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H
#include "compat.h"
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifndef F_OK
#define F_OK 0
#define X_OK 0  /* _access() rejects X_OK (1) with EINVAL */
#define W_OK 2
#define R_OK 4
#endif
#define optarg compat_optarg
#define optind compat_optind
#define opterr compat_opterr
#define optopt compat_optopt
#define getopt(c, v, o) compat_getopt(c, v, o)
#define gethostname(n, l) compat_gethostname(n, l)
#endif
