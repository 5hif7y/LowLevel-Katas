#ifndef _COMPAT_GRP_H_
#define _COMPAT_GRP_H_

#include <sys/cdefs.h>
#include <sys/types.h>

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

__BEGIN_DECLS

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

__END_DECLS

#endif /* _COMPAT_GRP_H_ */
