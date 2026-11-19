/* termios.h - Linux-compatible values; meaning is mapped to console modes
 * in compat/src/console.c. */
#ifndef COMPAT_TERMIOS_H
#define COMPAT_TERMIOS_H
#include "compat.h"
#define NCCS COMPAT_NCCS
/* c_cc indexes (Linux) */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16
/* c_iflag */
#define IGNBRK 0000001
#define BRKINT 0000002
#define IGNPAR 0000004
#define PARMRK 0000010
#define INPCK  0000020
#define ISTRIP 0000040
#define INLCR  0000100
#define IGNCR  0000200
#define ICRNL  0000400
#define IXON   0002000
#define IXANY  0004000
#define IXOFF  0010000
#define IUTF8  0040000
/* c_oflag */
#define OPOST  0000001
#define ONLCR  0000004
/* c_cflag */
#define CSIZE  0000060
#define CS8    0000060
#define CSTOPB 0000100
#define CREAD  0000200
#define PARENB 0000400
/* c_lflag */
#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ECHOK  0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000400
#define IEXTEN 0100000
/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define tcgetattr(f, t)    compat_tcgetattr(f, t)
#define tcsetattr(f, a, t) compat_tcsetattr(f, a, t)
#define cfmakeraw(t)       compat_cfmakeraw(t)
#endif
