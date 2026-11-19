/*
 * compat_types.h - POSIX types and compat_* prototypes, WITHOUT <windows.h>.
 *
 * This header is the "firewall" of the port: it is shared by
 *   - upstream sources (dtach/abduco/dvtm), through the forced include
 *     compat.h and the shadow headers (unistd.h, termios.h, sys/socket.h...);
 *   - the implementation in compat/src/*.c, which DOES include <winsock2.h>
 *     and <windows.h>.
 *
 * Therefore nothing here may clash with Win32/Winsock names: no `struct
 * sockaddr`, no `fd_set`, no `SOCKET`. Types that exist in both worlds with
 * the same layout (sockaddr, sockaddr_un) are passed as `const void *`.
 */
#ifndef COMPAT_TYPES_H
#define COMPAT_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- basic POSIX scalar types ------------------------------------------ */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif
#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif
#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#endif
typedef int socklen_t;
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

/* ---- file descriptor sets ---------------------------------------------- */
/*
 * POSIX fd_set is a bitmap indexed by descriptor number. Winsock's fd_set is
 * an array of SOCKET handles. We keep the POSIX semantics (so `nfds` and
 * FD_SET(fd) with small ints keep their meaning) and translate inside
 * compat_select().
 */
#define COMPAT_FD_SETSIZE 1024
typedef struct {
	unsigned long long bits[COMPAT_FD_SETSIZE / 64];
} compat_fd_set;

/* ---- terminals --------------------------------------------------------- */
#define COMPAT_NCCS 32
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t     c_line;
	cc_t     c_cc[COMPAT_NCCS];
	/*
	 * Win32 extension: the exact console modes this termios was read from.
	 * tcsetattr() of an untouched copy (the classic "restore orig_term")
	 * restores these bit for bit instead of re-deriving them from flags.
	 */
	unsigned long c_win_in_mode;
	unsigned long c_win_out_mode;
	int           c_win_valid;
};

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

/* ---- signals ----------------------------------------------------------- */
typedef unsigned long long sigset_t;
typedef void (*compat_sighandler_t)(int);

struct sigaction {
	compat_sighandler_t sa_handler;
	sigset_t sa_mask;
	int sa_flags;
};


/* ---- users ------------------------------------------------------------- */
struct passwd {
	char *pw_name;
	char *pw_passwd;
	uid_t pw_uid;
	gid_t pw_gid;
	char *pw_gecos;
	char *pw_dir;
	char *pw_shell;
};

/* ---- directories ------------------------------------------------------- */
struct dirent {
	unsigned long d_ino;
	unsigned char d_type;
	char d_name[260 * 3]; /* MAX_PATH UTF-16 units, re-encoded as UTF-8 */
};
typedef struct compat_dir DIR;

/* ---- prototypes (implemented in compat/src) ---------------------------- */
struct stat;
struct timeval;

/* fd layer */
ssize_t compat_read(int fd, void *buf, size_t n);
ssize_t compat_write(int fd, const void *buf, size_t n);
int     compat_close(int fd);
int     compat_dup(int fd);
int     compat_dup2(int fd, int fd2);
int     compat_pipe(int fds[2]);
int     compat_open(const char *path, int flags, ...);
int     compat_fcntl(int fd, int cmd, ...);
int     compat_ioctl(int fd, unsigned long req, ...);
int     compat_isatty(int fd);
int     compat_is_socket(int fd);

/* sockets */
int compat_socket(int domain, int type, int proto);
int compat_bind(int fd, const void *addr, socklen_t len);
int compat_listen(int fd, int backlog);
int compat_accept(int fd, void *addr, socklen_t *len);
int compat_connect(int fd, const void *addr, socklen_t len);
int compat_shutdown(int fd, int how);
int compat_socketpair(int domain, int type, int proto, int sv[2]);
int compat_gethostname(char *name, size_t len);

/* select */
void compat_fd_zero(compat_fd_set *s);
void compat_fd_set_(int fd, compat_fd_set *s);
void compat_fd_clr(int fd, compat_fd_set *s);
int  compat_fd_isset(int fd, const compat_fd_set *s);
int  compat_select(int nfds, compat_fd_set *r, compat_fd_set *w,
                   compat_fd_set *e, struct timeval *tv);
int  compat_pselect(int nfds, compat_fd_set *r, compat_fd_set *w,
                    compat_fd_set *e, const struct timespec *ts,
                    const sigset_t *mask);

/* signals */
compat_sighandler_t compat_signal(int sig, compat_sighandler_t h);
int compat_sigaction(int sig, const struct sigaction *sa, struct sigaction *old);
int compat_sigprocmask(int how, const sigset_t *set, sigset_t *old);
int compat_raise(int sig);
int compat_kill(pid_t pid, int sig);
void compat_signal_post(int sig);      /* async-safe: from any thread */
int  compat_signal_dispatch(void);     /* main thread: run handlers */

/* processes */
pid_t compat_waitpid(pid_t pid, int *status, int options);
pid_t compat_getpid(void);
pid_t compat_setsid(void);

/* terminals */
int compat_tcgetattr(int fd, struct termios *t);
int compat_tcsetattr(int fd, int act, const struct termios *t);
void compat_cfmakeraw(struct termios *t);

/* pty (ConPTY) */
struct compat_spawn_opts {
	const char *const *argv;  /* argv[0] resolved through PATH/PATHEXT */
	const char *cwd;          /* NULL = inherit */
	const char *const *env;   /* NULL-terminated "K", "V" pairs to add */
	struct winsize ws;
	int stdin_fd;             /* -1 = pseudoconsole; else redirect */
	int stdout_fd;            /* -1 = pseudoconsole; else redirect */
};
pid_t compat_spawn_pty(const struct compat_spawn_opts *o, int *master_fd);

/* "fork()" replacement for daemons: re-exec ourselves */
pid_t compat_spawn_self_detached(const char *role, const char *const *argv,
                                 int *status_rd);
const char *compat_daemon_role(void);
int compat_daemon_statusfd(void);

/* console */
void compat_console_events_mode(int on);

/* filesystem */
int compat_stat(const char *path, struct stat *st);
int compat_chmod(const char *path, int mode);
int compat_mkdir(const char *path, int mode);
int compat_unlink(const char *path);
int compat_fchdir(int fd);
int compat_mkfifo(const char *path, int mode);
char *compat_realpath(const char *path, char *resolved);
DIR *compat_opendir(const char *path);
struct dirent *compat_readdir(DIR *d);
int compat_closedir(DIR *d);
int compat_scandir(const char *dir, struct dirent ***list,
                   int (*filter)(const struct dirent *),
                   int (*cmp)(const struct dirent **, const struct dirent **));
int compat_alphasort(const struct dirent **a, const struct dirent **b);
char *compat_basename(char *path);
char *compat_dirname(char *path);

/* misc */
char *compat_setlocale(int cat, const char *name);
uid_t compat_getuid(void);
struct passwd *compat_getpwuid(uid_t uid);
int compat_setenv(const char *k, const char *v, int overwrite);
int compat_unsetenv(const char *k);
int compat_wcwidth(wchar_t c);
int compat_wcswidth(const wchar_t *s, size_t n);
long compat_sysconf(int name);

extern char *compat_optarg;
extern int compat_optind, compat_opterr, compat_optopt;
int compat_getopt(int argc, char *const argv[], const char *optstring);

#ifdef __cplusplus
}
#endif
#endif
