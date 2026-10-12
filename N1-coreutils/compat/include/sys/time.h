#ifndef _COMPAT_SYS_TIME_H_
#define _COMPAT_SYS_TIME_H_

#include <time.h>
#include <sys/cdefs.h>

#ifndef timespecclear
#define timespecclear(tsp) ((tsp)->tv_sec = (tsp)->tv_nsec = 0)
#endif
#ifndef timespecisset
#define timespecisset(tsp) ((tsp)->tv_sec || (tsp)->tv_nsec)
#endif

#ifndef SIGALRM
#define SIGALRM 14
#endif

__BEGIN_DECLS

int nanosleep(const struct timespec *req, struct timespec *rem);

__END_DECLS

#endif /* _COMPAT_SYS_TIME_H_ */
