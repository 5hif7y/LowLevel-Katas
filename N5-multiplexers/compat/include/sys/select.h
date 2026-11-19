#ifndef COMPAT_SYS_SELECT_H
#define COMPAT_SYS_SELECT_H
#include "compat.h"
#include <sys/time.h>
#define FD_SETSIZE COMPAT_FD_SETSIZE
typedef compat_fd_set fd_set;
#define FD_ZERO(s)     compat_fd_zero(s)
#define FD_SET(f, s)   compat_fd_set_(f, s)
#define FD_CLR(f, s)   compat_fd_clr(f, s)
#define FD_ISSET(f, s) compat_fd_isset(f, s)
#define select(n, r, w, e, t)     compat_select(n, r, w, e, t)
#define pselect(n, r, w, e, t, m) compat_pselect(n, r, w, e, t, m)
#endif
