/*
 * compat_internal.h - the only place where Win32 meets the POSIX facade.
 * compat/src/*.c include this; upstream code never does.
 */
#ifndef COMPAT_INTERNAL_H
#define COMPAT_INTERNAL_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "compat_types.h"

/* The same numeric values that compat.h exposes to upstream code. */
#define C_F_GETFD 1
#define C_F_SETFD 2
#define C_F_GETFL 3
#define C_F_SETFL 4
#define C_O_NONBLOCK 0x00100000
#define C_TIOCGWINSZ 0x5413
#define C_TIOCSWINSZ 0x5414
#define C_FIONBIO    0x5421
#define C_FIONREAD   0x541B

#define C_SIGHUP 1
#define C_SIGINT 2
#define C_SIGQUIT 3
#define C_SIGKILL 9
#define C_SIGPIPE 13
#define C_SIGTERM 15
#define C_SIGCHLD 17
#define C_SIGCONT 18
#define C_SIGSTOP 19
#define C_SIGTSTP 20
#define C_SIGTTIN 24
#define C_SIGTTOU 25
#define C_SIGXFSZ 26
#define C_SIGWINCH 28
#define C_NSIG 32

#define C_S_IFSOCK 0xC000
#define C_S_IXUSR 0100
#define C_S_IXGRP 0010

/* ---- virtual descriptors ------------------------------------------------
 * Sockets are not CRT descriptors. To give them a POSIX int that can never
 * collide with a real CRT fd we *reserve* a CRT slot by opening "NUL" and
 * remember which SOCKET lives behind that number.
 */
#define VFD_MAX 8192
struct compat_pty;
struct vfd {
	SOCKET s;
	int nonblock;
	struct compat_pty *pty;   /* non-NULL if this socket is a pty master */
};
struct vfd *vfd_get(int fd);
int  vfd_register_socket(SOCKET s);
void vfd_release(int fd);
void vfd_close_all(void);
int  wsa_to_errno(int wsa);

/* ---- console ----------------------------------------------------------- */
HANDLE console_in(void);    /* NULL when stdin is not a console */
HANDLE console_out(void);   /* NULL when stdout is not a console */
int  console_input_ready(void);         /* scans/cleans the input queue */
int  console_poll_resize(void);         /* 1 if window size changed */
int  console_read(void *buf, size_t n); /* byte-mode read of fd 0 */
int  console_write(HANDLE h, const char *buf, size_t n);
int  console_winsize(struct winsize *ws);

/* ---- signals ----------------------------------------------------------- */
HANDLE signal_event(void);  /* manual-reset event: a signal is pending */
int    signal_pending(void);
void   signal_mask_swap(const sigset_t *set, sigset_t *old);

/* ---- pty --------------------------------------------------------------- */
int  pty_resize(struct compat_pty *p, const struct winsize *ws);
int  pty_get_winsize(struct compat_pty *p, struct winsize *ws);
int  pty_repaint(struct compat_pty *p);
void pty_on_master_close(struct compat_pty *p);

/* ---- processes --------------------------------------------------------- */
struct child;
void child_register(HANDLE proc, DWORD pid, struct compat_pty *pty);
int  child_kill(pid_t pid, int sig);

/* ---- strings ----------------------------------------------------------- */
wchar_t *utf8_to_wide(const char *s);   /* malloc'ed */
char    *wide_to_utf8(const wchar_t *w);/* malloc'ed */
wchar_t *build_command_line(const char *const *argv); /* CommandLineToArgvW rules */
wchar_t *resolve_executable(const char *name);         /* PATH + PATHEXT */
int socketpair_raw(SOCKET sv[2]);

void compat_init_once(void);
void compat_trace(const char *fmt, ...);
const char *compat_daemon_role_raw(void);
void pty_child_exited(struct compat_pty *p);
void proc_init_daemon_env(void);

#endif
