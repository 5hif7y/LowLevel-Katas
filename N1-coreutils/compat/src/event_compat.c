/* Windows has no kqueue(2).  This is a deliberately conservative polling
 * stand-in, not a real event API: it keeps tail(1) responsive by sleeping
 * between checks, but it cannot report NOTE_WRITE/NOTE_DELETE/NOTE_RENAME.
 * A real implementation would need ReadDirectoryChangesW, WaitForMultipleObjects
 * or overlapped I/O.  See Docs/porter-guide/09-... and 11-... . */
#include <sys/event.h>
#include <windows.h>
#include <errno.h>

int
kqueue(void)
{
    return 42;
}

int
kevent(int kq, const struct kevent *changelist, int nchanges,
       struct kevent *eventlist, int nevents,
       const struct timespec *timeout)
{
    (void)kq;
    (void)changelist;
    (void)nchanges;
    (void)eventlist;
    (void)nevents;

    if (timeout != NULL) {
        DWORD ms = (DWORD)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
        Sleep(ms);
    } else {
        Sleep(250);
    }
    return 0;
}
