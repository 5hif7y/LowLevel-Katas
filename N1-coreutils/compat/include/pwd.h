#ifndef _COMPAT_PWD_H_
#define _COMPAT_PWD_H_

#include <sys/cdefs.h>
#include <sys/types.h>

struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

__BEGIN_DECLS

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

__END_DECLS

#endif /* _COMPAT_PWD_H_ */
