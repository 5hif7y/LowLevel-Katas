#ifndef _COMPAT_UTIL_H_
#define _COMPAT_UTIL_H_

#include <sys/cdefs.h>
#include <sys/types.h>

#define FMT_SCALED_STRSIZE 7

__BEGIN_DECLS

int fmt_scaled(long long number, char *result);
int scan_scaled(char *scaled, long long *result);
char *getbsize(int *headerlenp, long *blocksizep);

__END_DECLS

#endif /* _COMPAT_UTIL_H_ */
