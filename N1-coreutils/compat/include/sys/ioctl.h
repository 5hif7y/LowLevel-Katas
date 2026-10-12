#ifndef _COMPAT_SYS_IOCTL_H_
#define _COMPAT_SYS_IOCTL_H_

#include <sys/cdefs.h>
#include <stdarg.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
#endif

__BEGIN_DECLS

int ioctl(int fd, unsigned long req, ...);

__END_DECLS

#endif /* _COMPAT_SYS_IOCTL_H_ */
