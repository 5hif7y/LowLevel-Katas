/*
 * select.c - POSIX select()/pselect() over heterogeneous Windows objects.
 *
 * Winsock's select() accepts sockets only. A terminal multiplexer waits on
 * sockets AND the keyboard AND "a signal arrived". The common denominator on
 * Windows is WaitForMultipleObjects():
 *
 *   sockets  -> WSAEventSelect() binds them to one WSAEVENT
 *   console  -> the console input handle is waitable (signaled = queue
 *               not empty), but "not empty" includes focus/mouse/key-up
 *               noise, so console_input_ready() filters the queue first
 *   signals  -> the manual-reset event of signal.c
 *
 * Every wait is followed by a zero-timeout readiness probe, which is the
 * source of truth; the wait is just an efficient way to sleep.
 */
#include "compat_internal.h"

#define BITS 64

void compat_fd_zero(compat_fd_set *s)
{
	memset(s, 0, sizeof *s);
}

void compat_fd_set_(int fd, compat_fd_set *s)
{
	if (fd >= 0 && fd < COMPAT_FD_SETSIZE)
		s->bits[fd / BITS] |= 1ull << (fd % BITS);
}

void compat_fd_clr(int fd, compat_fd_set *s)
{
	if (fd >= 0 && fd < COMPAT_FD_SETSIZE)
		s->bits[fd / BITS] &= ~(1ull << (fd % BITS));
}

int compat_fd_isset(int fd, const compat_fd_set *s)
{
	if (fd < 0 || fd >= COMPAT_FD_SETSIZE)
		return 0;
	return (s->bits[fd / BITS] >> (fd % BITS)) & 1;
}

/* Is this CRT descriptor a pipe with bytes waiting (or a broken pipe)? */
static int pipe_ready(int fd, int *is_pipe)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	*is_pipe = 0;
	if (h == INVALID_HANDLE_VALUE)
		return 1; /* let read() report EBADF */
	if (GetFileType(h) != FILE_TYPE_PIPE)
		return 1; /* regular files and NUL are always readable */
	*is_pipe = 1;
	DWORD avail = 0;
	if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
		return 1; /* broken pipe: read() returns 0 (EOF) */
	return avail > 0;
}

static int probe(int nfds, compat_fd_set *r, compat_fd_set *w,
                 compat_fd_set *ro, compat_fd_set *wo, int *need_poll)
{
	fd_set wr, ww;
	int nsock = 0, ready = 0;
	FD_ZERO(&wr);
	FD_ZERO(&ww);
	*need_poll = 0;

	for (int fd = 0; fd < nfds; fd++) {
		int inr = r && compat_fd_isset(fd, r);
		int inw = w && compat_fd_isset(fd, w);
		if (!inr && !inw)
			continue;
		struct vfd *v = vfd_get(fd);
		if (v) {
			if (inr && wr.fd_count < FD_SETSIZE) { FD_SET(v->s, &wr); nsock++; }
			if (inw && ww.fd_count < FD_SETSIZE) { FD_SET(v->s, &ww); nsock++; }
			continue;
		}
		if (inr) {
			int ok, is_pipe;
			if (fd == 0 && console_in())
				ok = console_input_ready();
			else {
				ok = pipe_ready(fd, &is_pipe);
				if (is_pipe)
					*need_poll = 1; /* anonymous pipes are not waitable */
			}
			if (ok) { compat_fd_set_(fd, ro); ready++; }
		}
		if (inw) { compat_fd_set_(fd, wo); ready++; } /* CRT fds: assume writable */
	}

	if (nsock) {
		struct timeval zero = { 0, 0 };
		if (select(0, wr.fd_count ? &wr : NULL, ww.fd_count ? &ww : NULL,
		           NULL, &zero) == SOCKET_ERROR) {
			errno = wsa_to_errno(WSAGetLastError());
			return -1;
		}
		for (int fd = 0; fd < nfds; fd++) {
			struct vfd *v = vfd_get(fd);
			if (!v)
				continue;
			if (r && compat_fd_isset(fd, r) && FD_ISSET(v->s, &wr)) {
				compat_fd_set_(fd, ro); ready++;
			}
			if (w && compat_fd_isset(fd, w) && FD_ISSET(v->s, &ww)) {
				compat_fd_set_(fd, wo); ready++;
			}
		}
	}
	return ready;
}

static WSAEVENT sock_event;

static void arm_sockets(int nfds, compat_fd_set *r, compat_fd_set *w, int on)
{
	for (int fd = 0; fd < nfds; fd++) {
		int inr = r && compat_fd_isset(fd, r);
		int inw = w && compat_fd_isset(fd, w);
		struct vfd *v;
		if ((!inr && !inw) || !(v = vfd_get(fd)))
			continue;
		if (on) {
			long ev = 0;
			if (inr) ev |= FD_READ | FD_ACCEPT | FD_CLOSE;
			if (inw) ev |= FD_WRITE | FD_CONNECT | FD_CLOSE;
			WSAEventSelect(v->s, sock_event, ev);
		} else {
			/*
			 * Trap: WSAEventSelect() silently switches the socket to
			 * non-blocking mode and cancelling the association does
			 * NOT switch it back. Restore what the program asked for.
			 */
			WSAEventSelect(v->s, NULL, 0);
			u_long nb = v->nonblock ? 1 : 0;
			ioctlsocket(v->s, FIONBIO, &nb);
		}
	}
}

static int do_select(int nfds, compat_fd_set *r, compat_fd_set *w,
                     compat_fd_set *e, DWORD timeout_ms)
{
	compat_init_once();
	if (nfds > COMPAT_FD_SETSIZE)
		nfds = COMPAT_FD_SETSIZE;
	if (!sock_event)
		sock_event = WSACreateEvent();

	int watch_console = r && compat_fd_isset(0, r) && console_in();
	ULONGLONG start = GetTickCount64();

	for (;;) {
		compat_fd_set ro, wo;
		int need_poll;

		if (signal_pending()) {
			compat_signal_dispatch();
			if (r) compat_fd_zero(r);
			if (w) compat_fd_zero(w);
			if (e) compat_fd_zero(e);
			errno = EINTR;
			return -1;
		}
		if (watch_console && console_poll_resize())
			continue; /* posted SIGWINCH: deliver it on the next turn */

		compat_fd_zero(&ro);
		compat_fd_zero(&wo);
		int n = probe(nfds, r, w, &ro, &wo, &need_poll);
		if (n < 0)
			return -1;
		ULONGLONG elapsed = GetTickCount64() - start;
		if (n > 0 || (timeout_ms != INFINITE && elapsed >= timeout_ms)) {
			if (r) *r = ro;
			if (w) *w = wo;
			if (e) compat_fd_zero(e);
			return n;
		}

		HANDLE hs[3];
		DWORD nh = 0;
		hs[nh++] = sock_event;
		hs[nh++] = signal_event();
		if (watch_console)
			hs[nh++] = console_in();

		DWORD wait = timeout_ms == INFINITE ? INFINITE : (DWORD)(timeout_ms - elapsed);
		/* Pipes cannot be waited on and a classic conhost window resize
		 * does not always queue an event: fall back to a short poll. */
		if ((need_poll || watch_console) && wait > 100)
			wait = 100;

		WSAResetEvent(sock_event);
		arm_sockets(nfds, r, w, 1);
		WaitForMultipleObjects(nh, hs, FALSE, wait);
		arm_sockets(nfds, r, w, 0);
	}
}

int compat_select(int nfds, compat_fd_set *r, compat_fd_set *w,
                  compat_fd_set *e, struct timeval *tv)
{
	DWORD ms = INFINITE;
	if (tv)
		ms = (DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
	return do_select(nfds, r, w, e, ms);
}

int compat_pselect(int nfds, compat_fd_set *r, compat_fd_set *w,
                   compat_fd_set *e, const struct timespec *ts,
                   const sigset_t *mask)
{
	DWORD ms = INFINITE;
	sigset_t old;
	if (ts)
		ms = (DWORD)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
	/*
	 * pselect = atomically swap the mask, wait, restore. Atomicity is free
	 * here because signals are only delivered inside do_select(). But the
	 * swap must NOT use sigprocmask(): that one delivers pending signals
	 * immediately, the handler would run *before* the wait, and select()
	 * would then sleep without returning EINTR - the exact lost-wakeup
	 * race pselect() exists to prevent.
	 */
	if (mask)
		signal_mask_swap(mask, &old);
	int ret = do_select(nfds, r, w, e, ms);
	if (mask)
		signal_mask_swap(&old, NULL);
	return ret;
}
