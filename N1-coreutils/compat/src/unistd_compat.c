#include <unistd.h>
#include <time.h>
#include <windows.h>
#include <io.h>
#include <errno.h>

unsigned int
sleep(unsigned int seconds)
{
    Sleep((DWORD)seconds * 1000);
    return 0;
}

int
usleep(unsigned int usec)
{
    DWORD ms = (DWORD)(usec / 1000);
    if (ms == 0 && usec > 0)
        ms = 1;
    Sleep(ms);
    return 0;
}

int
ftruncate(int fd, int64_t length)
{
    errno_t err = _chsize_s(fd, (__int64)length);
    if (err != 0) {
        errno = err;
        return -1;
    }
    return 0;
}

int
nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    if (req == NULL || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) {
        errno = EINVAL;
        return -1;
    }
    DWORD ms = (DWORD)(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000);
    Sleep(ms);
    return 0;
}
