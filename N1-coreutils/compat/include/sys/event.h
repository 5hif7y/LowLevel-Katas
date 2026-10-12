#ifndef _COMPAT_SYS_EVENT_H_
#define _COMPAT_SYS_EVENT_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>

struct kevent {
    uintptr_t ident;
    short filter;
    unsigned short flags;
    unsigned int fflags;
    intptr_t data;
    void *udata;
};

#define EVFILT_READ   (-1)
#define EVFILT_WRITE  (-2)
#define EVFILT_VNODE  (-4)

#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_CLEAR    0x0020
#define EV_ERROR    0x4000

#define NOTE_DELETE   0x0001
#define NOTE_WRITE    0x0002
#define NOTE_EXTEND   0x0004
#define NOTE_ATTRIB   0x0008
#define NOTE_LINK     0x0010
#define NOTE_RENAME   0x0020
#define NOTE_REVOKE   0x0040
#define NOTE_TRUNCATE 0x0080

#define EV_SET(kevp, a, b, c, d, e, f) do { \
    (kevp)->ident = (uintptr_t)(a); \
    (kevp)->filter = (short)(b); \
    (kevp)->flags = (unsigned short)(c); \
    (kevp)->fflags = (unsigned int)(d); \
    (kevp)->data = (intptr_t)(e); \
    (kevp)->udata = (void *)(f); \
} while (0)

__BEGIN_DECLS

int kqueue(void);
int kevent(int kq, const struct kevent *changelist, int nchanges,
           struct kevent *eventlist, int nevents,
           const struct timespec *timeout);

__END_DECLS

#endif /* _COMPAT_SYS_EVENT_H_ */
