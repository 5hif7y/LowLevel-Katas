#ifndef _BSD_STDLIB_H_
#define _BSD_STDLIB_H_

#include <stdlib.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

long long strtonum(const char *numstr, long long minval, long long maxval, const char **errstrp);
const char *getprogname(void);
void setprogname(const char *progname);

__END_DECLS

#endif /* _BSD_STDLIB_H_ */
