#ifndef COMPAT_SYS_IOCTL_H
#define COMPAT_SYS_IOCTL_H
#include "compat.h"
#include <termios.h>
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONBIO    0x5421
#define FIONREAD   0x541B
#endif
