#ifndef _PLEDGE_H_
#define _PLEDGE_H_

#include <sys/cdefs.h>

__BEGIN_DECLS

int pledge(const char *promises, const char *execpromises);

__END_DECLS

#endif /* _PLEDGE_H_ */
