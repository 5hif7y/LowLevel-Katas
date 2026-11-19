/*
 * fd.c - one integer namespace for CRT descriptors and Winsock sockets.
 *
 * POSIX code freely mixes sockets, pipes and ttys behind `int fd`:
 * read(), write(), close(), fcntl(), select(). On Windows a SOCKET is a
 * kernel handle (UINT_PTR), not a CRT descriptor, and _read() on it fails.
 *
 * Strategy: every socket gets a *reserved* CRT descriptor (an fd opened on
 * "NUL"). The CRT therefore never hands that number out twice, the int is
 * small (so the POSIX fd_set bitmap works), and a side table remembers which
 * SOCKET lives behind it.
 */
#include "compat_internal.h"
#include <stdarg.h>

static struct vfd *vfds[VFD_MAX];
static CRITICAL_SECTION vfd_lock;
static INIT_ONCE vfd_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK vfd_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o; (void)p; (void)c;
	InitializeCriticalSection(&vfd_lock);
	return TRUE;
}

struct vfd *vfd_get(int fd)
{
	if (fd < 0 || fd >= VFD_MAX)
		return NULL;
	return vfds[fd];
}

int vfd_register_socket(SOCKET s)
{
	InitOnceExecuteOnce(&vfd_once, vfd_init, NULL, NULL);
	/* Never let a child process inherit our sockets by accident. */
	SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
	int fd = _open("NUL", _O_RDWR | _O_BINARY | _O_NOINHERIT);
	if (fd < 0 || fd >= VFD_MAX) {
		if (fd >= 0)
			_close(fd);
		closesocket(s);
		errno = EMFILE;
		return -1;
	}
	struct vfd *v = calloc(1, sizeof *v);
	if (!v) {
		_close(fd);
		closesocket(s);
		errno = ENOMEM;
		return -1;
	}
	v->s = s;
	EnterCriticalSection(&vfd_lock);
	vfds[fd] = v;
	LeaveCriticalSection(&vfd_lock);
	return fd;
}

void vfd_release(int fd)
{
	EnterCriticalSection(&vfd_lock);
	struct vfd *v = vfds[fd];
	vfds[fd] = NULL;
	LeaveCriticalSection(&vfd_lock);
	free(v);
	_close(fd);
}

int compat_is_socket(int fd)
{
	return vfd_get(fd) != NULL;
}

int wsa_to_errno(int e)
{
	switch (e) {
	case WSAEINTR:           return EINTR;
	case WSAEBADF:           return EBADF;
	case WSAEACCES:          return EACCES;
	case WSAEFAULT:          return EFAULT;
	case WSAEINVAL:          return EINVAL;
	case WSAEMFILE:          return EMFILE;
	case WSAEWOULDBLOCK:     return EAGAIN;
	case WSAEINPROGRESS:     return EINPROGRESS;
	case WSAEALREADY:        return EALREADY;
	case WSAENOTSOCK:        return ENOTSOCK;
	case WSAEDESTADDRREQ:    return EDESTADDRREQ;
	case WSAEMSGSIZE:        return EMSGSIZE;
	case WSAEPROTOTYPE:      return EPROTOTYPE;
	case WSAENOPROTOOPT:     return ENOPROTOOPT;
	case WSAEPROTONOSUPPORT: return EPROTONOSUPPORT;
	case WSAEOPNOTSUPP:      return EOPNOTSUPP;
	case WSAEAFNOSUPPORT:    return EAFNOSUPPORT;
	case WSAEADDRINUSE:      return EADDRINUSE;
	case WSAEADDRNOTAVAIL:   return EADDRNOTAVAIL;
	case WSAENETDOWN:        return ENETDOWN;
	case WSAENETUNREACH:     return ENETUNREACH;
	case WSAECONNABORTED:    return ECONNABORTED;
	case WSAECONNRESET:      return ECONNRESET;
	case WSAENOBUFS:         return ENOBUFS;
	case WSAEISCONN:         return EISCONN;
	case WSAENOTCONN:        return ENOTCONN;
	case WSAETIMEDOUT:       return ETIMEDOUT;
	case WSAECONNREFUSED:    return ECONNREFUSED;
	case WSAENAMETOOLONG:    return ENAMETOOLONG;
	case WSAEHOSTUNREACH:    return EHOSTUNREACH;
	case WSAESHUTDOWN:       return EPIPE;
	default:                 return EIO;
	}
}

static int is_console_out_fd(int fd)
{
	if (fd != 1 && fd != 2)
		return 0;
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD mode;
	return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
}

ssize_t compat_read(int fd, void *buf, size_t n)
{
	struct vfd *v = vfd_get(fd);
	if (v) {
		int len = n > INT_MAX ? INT_MAX : (int)n;
		int r = recv(v->s, buf, len, 0);
		if (r == SOCKET_ERROR) {
			errno = wsa_to_errno(WSAGetLastError());
			return -1;
		}
		return r;
	}
	if (fd == 0 && console_in())
		return console_read(buf, n);
	return _read(fd, buf, n > UINT_MAX ? UINT_MAX : (unsigned)n);
}

ssize_t compat_write(int fd, const void *buf, size_t n)
{
	struct vfd *v = vfd_get(fd);
	if (v) {
		int len = n > INT_MAX ? INT_MAX : (int)n;
		int r = send(v->s, buf, len, 0);
		if (r == SOCKET_ERROR) {
			int e = WSAGetLastError();
			errno = wsa_to_errno(e);
			/* No SIGPIPE on Windows: EPIPE is the whole story. */
			if (e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN)
				errno = EPIPE;
			return -1;
		}
		return r;
	}
	if (is_console_out_fd(fd))
		return console_write((HANDLE)_get_osfhandle(fd), buf, n);
	return _write(fd, buf, n > UINT_MAX ? UINT_MAX : (unsigned)n);
}

/*
 * POSIX AF_UNIX close(): the peer reads whatever is queued, then EOF.
 * Winsock close() with *unread data in our receive queue* is an abortive
 * close: the peer gets RST and loses the data IT had not read yet. abduco's
 * client closes right after echoing MSG_EXIT while the server keeps sending
 * MSG_EXIT; the RST destroyed the echo and the server waited forever.
 * Send FIN first, then drain our receive queue, then close.
 */
static void graceful_close(SOCKET s)
{
	char junk[4096];
	u_long nb = 1;
	shutdown(s, SD_SEND);
	ioctlsocket(s, FIONBIO, &nb);
	for (int i = 0; i < 64 && recv(s, junk, sizeof junk, 0) > 0; i++)
		;
	closesocket(s);
}

int compat_close(int fd)
{
	struct vfd *v = vfd_get(fd);
	if (v) {
		struct compat_pty *p = v->pty;
		graceful_close(v->s);
		vfd_release(fd);
		if (p)
			pty_on_master_close(p);
		return 0;
	}
	return _close(fd);
}

int compat_dup(int fd)
{
	if (vfd_get(fd)) {
		errno = ENOTSUP;
		return -1;
	}
	return _dup(fd);
}

int compat_dup2(int fd, int fd2)
{
	if (vfd_get(fd)) {
		errno = ENOTSUP;
		return -1;
	}
	if (vfd_get(fd2))
		compat_close(fd2);
	return _dup2(fd, fd2) == 0 ? fd2 : -1;
}

int compat_pipe(int fds[2])
{
	/* Binary, not inheritable: the default _pipe() is text mode (CRLF
	 * translation!) and the classic Unix bug of leaking pipe ends into
	 * children becomes a hang on Windows too. */
	return _pipe(fds, 65536, _O_BINARY | _O_NOINHERIT);
}

int compat_open(const char *path, int flags, ...)
{
	int mode = 0;
	if (flags & _O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	if (!strcmp(path, "/dev/null"))
		path = "NUL";
	else if (!strcmp(path, "/dev/tty"))
		path = "CONIN$";
	flags &= ~C_O_NONBLOCK;
	flags |= _O_BINARY;
	wchar_t *w = utf8_to_wide(path);
	if (!w) {
		errno = ENOMEM;
		return -1;
	}
	int fd = _wopen(w, flags, (mode & 0200) ? (_S_IREAD | _S_IWRITE) : _S_IREAD);
	free(w);
	return fd;
}

static int set_nonblock(struct vfd *v, int on)
{
	u_long arg = on ? 1 : 0;
	if (ioctlsocket(v->s, FIONBIO, &arg) == SOCKET_ERROR) {
		errno = wsa_to_errno(WSAGetLastError());
		return -1;
	}
	v->nonblock = on;
	return 0;
}

int compat_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	long arg = 0;
	va_start(ap, cmd);
	if (cmd == C_F_SETFD || cmd == C_F_SETFL)
		arg = va_arg(ap, long);
	va_end(ap);

	struct vfd *v = vfd_get(fd);
	HANDLE h = v ? (HANDLE)v->s : (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	switch (cmd) {
	case C_F_GETFD: {
		DWORD f = 0;
		GetHandleInformation(h, &f);
		return (f & HANDLE_FLAG_INHERIT) ? 0 : 1;
	}
	case C_F_SETFD:
		SetHandleInformation(h, HANDLE_FLAG_INHERIT, (arg & 1) ? 0 : HANDLE_FLAG_INHERIT);
		return 0;
	case C_F_GETFL:
		return _O_RDWR | ((v && v->nonblock) ? C_O_NONBLOCK : 0);
	case C_F_SETFL:
		if (v)
			return set_nonblock(v, (arg & C_O_NONBLOCK) != 0);
		return 0; /* pipes/files: there is no O_NONBLOCK for CRT fds */
	}
	errno = EINVAL;
	return -1;
}

int compat_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);

	struct vfd *v = vfd_get(fd);
	switch (req) {
	case C_TIOCGWINSZ:
		if (v && v->pty)
			return pty_get_winsize(v->pty, (struct winsize *)arg);
		if (console_winsize((struct winsize *)arg) == 0)
			return 0;
		errno = ENOTTY;
		return -1;
	case C_TIOCSWINSZ:
		if (v && v->pty)
			return pty_resize(v->pty, (const struct winsize *)arg);
		errno = ENOTTY;
		return -1;
	case C_FIONBIO:
		if (v)
			return set_nonblock(v, *(int *)arg != 0);
		errno = ENOTSOCK;
		return -1;
	case C_FIONREAD:
		if (v) {
			u_long avail = 0;
			if (ioctlsocket(v->s, FIONREAD, &avail) == SOCKET_ERROR) {
				errno = wsa_to_errno(WSAGetLastError());
				return -1;
			}
			*(int *)arg = (int)avail;
			return 0;
		}
		errno = ENOTSUP;
		return -1;
	}
	errno = EINVAL;
	return -1;
}

int compat_isatty(int fd)
{
	/*
	 * NOT _isatty(): the UCRT answers "is this a character device", so the
	 * NUL device is a tty for it. A daemon whose stdio is NUL would then
	 * believe it has a terminal. GetConsoleMode() only succeeds on a real
	 * console handle.
	 */
	if (vfd_get(fd))
		return 0;
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD mode;
	if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) {
		errno = ENOTTY;
		return 0;
	}
	return 1;
}

/*
 * exit() with sockets still open: on Unix the kernel closes them with a FIN
 * and the peer reads EOF. On Windows, process teardown *aborts* them and the
 * peer gets WSAECONNRESET - dtach's client printed "[read returned an
 * error]" instead of "[EOF - dtach terminating]". Close them politely.
 */
void vfd_close_all(void)
{
	for (int fd = 0; fd < VFD_MAX; fd++) {
		struct vfd *v = vfds[fd];
		if (!v)
			continue;
		graceful_close(v->s);
		vfds[fd] = NULL;
	}
}
