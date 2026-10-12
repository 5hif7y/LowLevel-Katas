#ifndef _UNVEIL_H_
#define _UNVEIL_H_

#include <sys/cdefs.h>

__BEGIN_DECLS

int unveil(const char *path, const char *permissions);

__END_DECLS

#endif /* _UNVEIL_H_ */
