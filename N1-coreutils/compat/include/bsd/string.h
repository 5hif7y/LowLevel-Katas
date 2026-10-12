#ifndef _BSD_STRING_H_
#define _BSD_STRING_H_

#include <string.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);
char *strsep(char **stringp, const char *delim);

__END_DECLS

#endif /* _BSD_STRING_H_ */
