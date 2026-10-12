#ifndef _COMPAT_STRINGS_H_
#define _COMPAT_STRINGS_H_

#include <string.h>

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#endif /* _COMPAT_STRINGS_H_ */
