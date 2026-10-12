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
