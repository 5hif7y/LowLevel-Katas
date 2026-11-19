/*
 * net.c - BSD sockets on Winsock2, including AF_UNIX (Windows 10 1803+).
 *
 * AF_UNIX on Windows supports SOCK_STREAM, filesystem paths, bind/listen/
 * accept/connect and select(). It does NOT support socketpair(),
 * SOCK_DGRAM, abstract names or SCM_RIGHTS. That subset is exactly what
 * dtach and abduco need for their session sockets.
 */
#include "compat_internal.h"

static int fail_wsa(void)
{
	errno = wsa_to_errno(WSAGetLastError());
	return -1;
}

int compat_socket(int domain, int type, int proto)
{
	compat_init_once();
	SOCKET s = WSASocketW(domain, type, proto, NULL, 0,
	                      WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
	if (s == INVALID_SOCKET)
		return fail_wsa();
	return vfd_register_socket(s);
}

static SOCKET sock_of(int fd)
{
	struct vfd *v = vfd_get(fd);
	if (!v) {
		errno = ENOTSOCK;
		return INVALID_SOCKET;
	}
	return v->s;
}

int compat_bind(int fd, const void *addr, socklen_t len)
{
	SOCKET s = sock_of(fd);
	if (s == INVALID_SOCKET)
		return -1;
	/*
	 * Winsock wants the full sizeof(struct sockaddr_un) or the exact
	 * offsetof(sun_path) + strlen + 1 that abduco computes; both work.
	 */
	if (bind(s, (const struct sockaddr *)addr, len) == SOCKET_ERROR)
		return fail_wsa();
	return 0;
}

int compat_listen(int fd, int backlog)
{
	SOCKET s = sock_of(fd);
	if (s == INVALID_SOCKET)
		return -1;
	if (listen(s, backlog) == SOCKET_ERROR)
		return fail_wsa();
	return 0;
}

int compat_accept(int fd, void *addr, socklen_t *len)
{
	SOCKET s = sock_of(fd);
	if (s == INVALID_SOCKET)
		return -1;
	SOCKET c = accept(s, (struct sockaddr *)addr, len);
	if (c == INVALID_SOCKET)
		return fail_wsa();
	/* An accepted socket inherits FIONBIO from the listener: reset it so
	 * the new descriptor starts blocking, as on POSIX. */
	u_long off = 0;
	ioctlsocket(c, FIONBIO, &off);
	return vfd_register_socket(c);
}

int compat_connect(int fd, const void *addr, socklen_t len)
{
	SOCKET s = sock_of(fd);
	if (s == INVALID_SOCKET)
		return -1;
	if (connect(s, (const struct sockaddr *)addr, len) == SOCKET_ERROR) {
		int e = WSAGetLastError();
		errno = wsa_to_errno(e);
		/*
		 * Connecting to a path that does not exist yields WSAENOENT-ish
		 * WSAEINVAL/WSAENETDOWN depending on the build. POSIX callers
		 * expect ENOENT (dtach -A uses it to decide "create").
		 */
		const struct sockaddr_un *un = addr;
		if (un->sun_family == AF_UNIX && e != WSAECONNREFUSED) {
			wchar_t *w = utf8_to_wide(un->sun_path);
			if (w && GetFileAttributesW(w) == INVALID_FILE_ATTRIBUTES)
				errno = ENOENT;
			free(w);
		}
		return -1;
	}
	return 0;
}

int compat_shutdown(int fd, int how)
{
	SOCKET s = sock_of(fd);
	if (s == INVALID_SOCKET)
		return -1;
	if (shutdown(s, how) == SOCKET_ERROR)
		return fail_wsa();
	return 0;
}

/*
 * socketpair(): AF_UNIX on Windows has no socketpair, so build one over TCP
 * loopback. Bind to 127.0.0.1:0 (never 0.0.0.0: that would trigger the
 * firewall prompt and expose the port), accept exactly one connection and
 * close the listener.
 */
int socketpair_raw(SOCKET sv[2])
{
	compat_init_once();
	SOCKET l = INVALID_SOCKET, a = INVALID_SOCKET, c = INVALID_SOCKET;
	struct sockaddr_in addr = { 0 };
	int alen = sizeof addr;

	l = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
	               WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
	if (l == INVALID_SOCKET)
		goto fail;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(l, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR ||
	    getsockname(l, (struct sockaddr *)&addr, &alen) == SOCKET_ERROR ||
	    listen(l, 1) == SOCKET_ERROR)
		goto fail;
	c = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
	               WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
	if (c == INVALID_SOCKET ||
	    connect(c, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR)
		goto fail;
	a = accept(l, NULL, NULL);
	if (a == INVALID_SOCKET)
		goto fail;
	closesocket(l);
	/* Interactive traffic: one keystroke per packet, no 40 ms Nagle delay. */
	BOOL one = TRUE;
	setsockopt(a, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
	setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
	sv[0] = a;
	sv[1] = c;
	return 0;
fail:
	errno = wsa_to_errno(WSAGetLastError());
	if (l != INVALID_SOCKET) closesocket(l);
	if (c != INVALID_SOCKET) closesocket(c);
	if (a != INVALID_SOCKET) closesocket(a);
	return -1;
}

int compat_socketpair(int domain, int type, int proto, int sv[2])
{
	(void)domain; (void)type; (void)proto;
	SOCKET s[2];
	if (socketpair_raw(s) < 0)
		return -1;
	sv[0] = vfd_register_socket(s[0]);
	if (sv[0] < 0) {
		closesocket(s[1]);
		return -1;
	}
	sv[1] = vfd_register_socket(s[1]);
	if (sv[1] < 0) {
		compat_close(sv[0]);
		return -1;
	}
	return 0;
}

int compat_gethostname(char *name, size_t len)
{
	compat_init_once();
	if (gethostname(name, (int)len) == SOCKET_ERROR)
		return fail_wsa();
	return 0;
}
