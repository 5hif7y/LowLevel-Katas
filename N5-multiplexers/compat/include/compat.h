/*
 * compat.h - forced include (/FI) for upstream sources of the N5 kata.
 *
 * Rule 1: this header NEVER includes <windows.h> or <winsock2.h>. Upstream
 *         code (dtach, abduco, dvtm) and PDCurses' curses.h must not see the
 *         ~40k lines of Win32 macros (KEY_EVENT, MOUSE_MOVED, ERROR, min/max,
 *         `small`, `interface`...). Everything Win32 lives in compat/src.
 *
 * Rule 2: CRT headers are included FIRST, macros are defined AFTER. A
 *         function-like macro such as `#define read(f,b,n) compat_read(...)`
 *         defined before <io.h> would rename the CRT's own declaration.
 *
 * Rule 3: only function-like macros for functions. `struct stat sb;` and
 *         `struct sigaction sa;` survive, `stat(path, &sb)` is redirected.
 */
#ifndef COMPAT_H
#define COMPAT_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE 1
#endif

/* ---- CRT first --------------------------------------------------------- */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <wchar.h>
#include <locale.h>

#include "compat_types.h"

/* ---- GNU-isms MSVC does not know --------------------------------------- */
#ifdef _MSC_VER
#define __attribute__(x)

#endif
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/* ---- errno values the UCRT lacks or gets "wrong" ----------------------- */
/* Linux: EWOULDBLOCK == EAGAIN. UCRT: 140 vs 11. abduco's read_all() tests
 * EWOULDBLOCK first and dtach tests only EAGAIN; with distinct values one
 * of them misreads every non-blocking read. Make them equal, as on Linux. */
#undef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#ifndef ENOTSUP
#define ENOTSUP 129
#endif

/* ---- fcntl ------------------------------------------------------------- */
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define FD_CLOEXEC  1
#define O_NONBLOCK  0x00100000   /* outside every _O_* bit of the UCRT */
#define O_NOCTTY    0
#define O_CLOEXEC   _O_NOINHERIT

/* ---- stat -------------------------------------------------------------- */
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000          /* 0140000: fits in the 16-bit st_mode */
#endif
#define S_IFLNK  0xA000
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == _S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISVTX 01000

/* ---- signals the UCRT does not define ---------------------------------- */
/* UCRT owns 2 (INT), 4 (ILL), 8 (FPE), 11 (SEGV), 15 (TERM), 21 (BREAK),
 * 22 (ABRT). The rest use Linux numbers where they do not collide. */
#define SIGHUP    1
#define SIGQUIT   3
#define SIGKILL   9
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  24
#define SIGTTOU  25
#define SIGXFSZ  26
#define SIGWINCH 28
#define COMPAT_NSIG 32

#define SA_RESTART 0x10000000
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define sigemptyset(s)    (*(s) = 0, 0)
#define sigfillset(s)     (*(s) = ~0ull, 0)
#define sigaddset(s, n)   (*(s) |= (1ull << (n)), 0)
#define sigdelset(s, n)   (*(s) &= ~(1ull << (n)), 0)
#define sigismember(s, n) ((*(s) >> (n)) & 1)

/* ---- wait -------------------------------------------------------------- */
#define WNOHANG 1
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)

/* ---- misc -------------------------------------------------------------- */
#define _SC_OPEN_MAX 4
#define PATH_MAX 4096
#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE '\0'
#endif

/* ---- the redirections -------------------------------------------------- */
#define read(f, b, n)          compat_read(f, b, n)
#define write(f, b, n)         compat_write(f, b, n)
#define close(f)               compat_close(f)
#define dup(f)                 compat_dup(f)
#define dup2(a, b)             compat_dup2(a, b)
#define pipe(p)                compat_pipe(p)
#define open                   compat_open
#define fcntl                  compat_fcntl
#define ioctl                  compat_ioctl
#define isatty(f)              compat_isatty(f)

#define stat(p, b)             compat_stat(p, b)
#define lstat(p, b)            compat_stat(p, b)
#define chmod(p, m)            compat_chmod(p, m)
#define mkdir(p, m)            compat_mkdir(p, m)
#define unlink(p)              compat_unlink(p)
#define fchdir(f)              compat_fchdir(f)
#define mkfifo(p, m)           compat_mkfifo(p, m)
#define realpath(p, r)         compat_realpath(p, r)

#define signal(s, h)           compat_signal(s, h)
#define sigaction(s, a, o)     compat_sigaction(s, a, o)
#define sigprocmask(h, s, o)   compat_sigprocmask(h, s, o)
#define raise(s)               compat_raise(s)
#define kill(p, s)             compat_kill(p, s)
#define waitpid(p, s, o)       compat_waitpid(p, s, o)
#define wait(s)                compat_waitpid(-1, s, 0)
/* fork() cannot be emulated; every reachable call site is patched. */
#define fork()                 (errno = ENOSYS, (pid_t)-1)
#define forkpty(m, n, t, w)    (errno = ENOSYS, (pid_t)-1)
#define getpid()               compat_getpid()
#define setsid()               compat_setsid()

#define getuid()               compat_getuid()
#define geteuid()              compat_getuid()
#define getpwuid(u)            compat_getpwuid(u)
#define setenv(k, v, o)        compat_setenv(k, v, o)
#define unsetenv(k)            compat_unsetenv(k)
#define wcwidth(c)             compat_wcwidth(c)
#define wcswidth(s, n)         compat_wcswidth(s, n)
#define sysconf(n)             compat_sysconf(n)
#define setlocale(c, l)        compat_setlocale(c, l)

#endif /* COMPAT_H */
