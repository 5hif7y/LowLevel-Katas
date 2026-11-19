/*
 * signal.c - POSIX signals emulated as "pending bits + an event".
 *
 * Windows has no asynchronous signal delivery into an arbitrary thread.
 * The UCRT's signal() only knows INT/ILL/FPE/SEGV/TERM/BREAK/ABRT, and
 * Ctrl+C handlers run on a *new thread*, where calling the program's
 * handler would race with its main loop.
 *
 * Model used here (the "self-pipe trick", inverted):
 *   - any thread may *post* a signal: set a pending bit, SetEvent();
 *   - handlers only run on the main thread, from compat_signal_dispatch(),
 *     which select()/pselect() call before blocking and after waking up.
 *     select() then fails with EINTR, exactly the contract that dtach,
 *     abduco and dvtm expect from a Unix kernel.
 *
 * Every program in this kata blocks in select(), so that single delivery
 * point is enough. A program that blocks in read() would need more.
 */
#include "compat_internal.h"
#include <signal.h>

static volatile LONG64 pending;
static sigset_t blocked;
static compat_sighandler_t handlers[C_NSIG];
static HANDLE sigev;
static INIT_ONCE sig_once = INIT_ONCE_STATIC_INIT;

#define H_DFL ((compat_sighandler_t)0)
#define H_IGN ((compat_sighandler_t)1)

static BOOL WINAPI ctrl_handler(DWORD type);

static BOOL CALLBACK sig_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o; (void)p; (void)c;
	sigev = CreateEventW(NULL, TRUE, FALSE, NULL);
	SetConsoleCtrlHandler(ctrl_handler, TRUE);
	return TRUE;
}

HANDLE signal_event(void)
{
	InitOnceExecuteOnce(&sig_once, sig_init, NULL, NULL);
	return sigev;
}

static int is_crt_sync_signal(int sig)
{
	/* Synchronous faults stay with the CRT: they must run on the faulting
	 * thread, not be deferred to the next select(). */
	return sig == SIGSEGV || sig == SIGILL || sig == SIGFPE || sig == SIGABRT;
}

int signal_pending(void)
{
	return (pending & ~blocked) != 0;
}

void compat_signal_post(int sig)
{
	if (sig <= 0 || sig >= C_NSIG)
		return;
	InterlockedOr64(&pending, 1LL << sig);
	SetEvent(signal_event());
}

static void default_action(int sig)
{
	switch (sig) {
	case C_SIGCHLD: case C_SIGWINCH: case C_SIGCONT:
	/* No job control on Windows: stop signals are ignored. */
	case C_SIGTSTP: case C_SIGTTIN: case C_SIGTTOU: case C_SIGSTOP:
		return;
	default:
		/* Unix terminates without running atexit(): so do we. */
		_exit(128 + sig);
	}
}

int compat_signal_dispatch(void)
{
	int n = 0;
	for (;;) {
		LONG64 deliverable = pending & ~(LONG64)blocked;
		if (!deliverable)
			break;
		int sig = 0;
		while (!((deliverable >> sig) & 1))
			sig++;
		InterlockedAnd64(&pending, ~(1LL << sig));
		compat_sighandler_t h = handlers[sig];
		if (h == H_IGN)
			continue;
		n++;
		if (h == H_DFL)
			default_action(sig);
		else
			h(sig);
	}
	if (!(pending & ~(LONG64)blocked))
		ResetEvent(signal_event());
	return n;
}

compat_sighandler_t compat_signal(int sig, compat_sighandler_t h)
{
	compat_init_once();
	if (sig <= 0 || sig >= C_NSIG) {
		errno = EINVAL;
		return SIG_ERR;
	}
	if (is_crt_sync_signal(sig))
		return signal(sig, h);
	compat_sighandler_t old = handlers[sig];
	handlers[sig] = h;
	return old;
}

int compat_sigaction(int sig, const struct sigaction *sa, struct sigaction *old)
{
	compat_init_once();
	if (sig <= 0 || sig >= C_NSIG || sig == C_SIGKILL || sig == C_SIGSTOP) {
		errno = EINVAL;
		return -1;
	}
	if (old) {
		memset(old, 0, sizeof *old);
		old->sa_handler = handlers[sig];
	}
	if (sa) {
		if (is_crt_sync_signal(sig))
			signal(sig, sa->sa_handler);
		else
			handlers[sig] = sa->sa_handler;
	}
	return 0;
}

int compat_sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
	if (old)
		*old = blocked;
	if (set) {
		switch (how) {
		case 0: blocked |= *set; break;     /* SIG_BLOCK */
		case 1: blocked &= ~*set; break;    /* SIG_UNBLOCK */
		case 2: blocked = *set; break;      /* SIG_SETMASK */
		default: errno = EINVAL; return -1;
		}
	}
	/* POSIX: an unblocked pending signal is delivered before returning. */
	if (signal_pending())
		compat_signal_dispatch();
	return 0;
}

void signal_mask_swap(const sigset_t *set, sigset_t *old)
{
	if (old)
		*old = blocked;
	blocked = *set;
	if (signal_pending())
		SetEvent(signal_event());
}

int compat_raise(int sig)
{
	compat_signal_post(sig);
	compat_signal_dispatch();
	return 0;
}

pid_t compat_getpid(void)
{
	return (pid_t)GetCurrentProcessId();
}

pid_t compat_setsid(void)
{
	/* Sessions/process groups do not exist. Detaching from the console
	 * is done at spawn time (DETACHED_PROCESS), see proc.c. */
	return compat_getpid();
}

int compat_kill(pid_t pid, int sig)
{
	pid_t self = compat_getpid();
	if (pid == 0 || pid == self || pid == -self)
		return compat_raise(sig);
	if (pid < 0)
		pid = -pid; /* "process group" == the child we spawned */
	return child_kill(pid, sig);
}

/*
 * Console control events arrive on a thread the system creates. Translate
 * them into signals and let the main loop run the handler.
 */
static BOOL WINAPI ctrl_handler(DWORD type)
{
	int sig;
	switch (type) {
	case CTRL_C_EVENT:     sig = C_SIGINT; break;
	case CTRL_BREAK_EVENT: sig = C_SIGQUIT; break;
	default:               sig = C_SIGHUP; break; /* close/logoff/shutdown */
	}
	compat_sighandler_t h = handlers[sig];
	if (h == H_DFL)
		return FALSE;          /* let Windows apply its default */
	if (h != H_IGN)
		compat_signal_post(sig);
	if (sig == C_SIGHUP) {
		/* After we return from CTRL_CLOSE_EVENT the process is killed.
		 * Give the main thread a moment to run the handler (and its
		 * atexit() cleanup: unlinking sockets, restoring the tty). */
		Sleep(1500);
	}
	return TRUE;
}
