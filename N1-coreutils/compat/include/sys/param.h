#ifndef _COMPAT_SYS_PARAM_H_
#define _COMPAT_SYS_PARAM_H_

#include <sys/types.h>
#include <limits.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef ALIGNBYTES
#define ALIGNBYTES (sizeof(uintptr_t) - 1)
#endif

#ifndef ALIGN
#define ALIGN(p) (((uintptr_t)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif

#endif /* _COMPAT_SYS_PARAM_H_ */
