#include <sys/stat.h>
#include <sys/time.h>
#include <windows.h>
#include <io.h>
#include <errno.h>

#define WINDOWS_TICK 10000000ULL
#define SEC_TO_UNIX_EPOCH 11644473600ULL

static void
timespec_to_filetime(const struct timespec *ts, FILETIME *ft)
{
    ULARGE_INTEGER uli;
    uli.QuadPart = ((ULONGLONG)ts->tv_sec + SEC_TO_UNIX_EPOCH) * WINDOWS_TICK + ((ULONGLONG)ts->tv_nsec / 100ULL);
    ft->dwLowDateTime = uli.LowPart;
    ft->dwHighDateTime = uli.HighPart;
}

int
futimens(int fd, const struct timespec times[2])
{
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE || hFile == NULL) {
        errno = EBADF;
        return -1;
    }

    FILETIME atime, mtime;
    FILETIME *pAtime = NULL, *pMtime = NULL;

    if (times == NULL) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        pAtime = &now;
        pMtime = &now;
    } else {
        if (times[0].tv_nsec != -2L) { /* UTIME_OMIT */
            if (times[0].tv_nsec == -1L) { /* UTIME_NOW */
                GetSystemTimeAsFileTime(&atime);
            } else {
                timespec_to_filetime(&times[0], &atime);
            }
            pAtime = &atime;
        }
        if (times[1].tv_nsec != -2L) { /* UTIME_OMIT */
            if (times[1].tv_nsec == -1L) { /* UTIME_NOW */
                GetSystemTimeAsFileTime(&mtime);
            } else {
                timespec_to_filetime(&times[1], &mtime);
            }
            pMtime = &mtime;
        }
    }

    if (!SetFileTime(hFile, NULL, pAtime, pMtime)) {
        errno = EACCES;
        return -1;
    }

    return 0;
}

int
utimensat(int fd, const char *path, const struct timespec times[2], int flag)
{
    (void)fd;
    (void)flag;

    HANDLE hFile = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            errno = ENOENT;
        else if (err == ERROR_ACCESS_DENIED)
            errno = EACCES;
        else
            errno = EINVAL;
        return -1;
    }

    FILETIME atime, mtime;
    FILETIME *pAtime = NULL, *pMtime = NULL;

    if (times == NULL) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        pAtime = &now;
        pMtime = &now;
    } else {
        if (times[0].tv_nsec != -2L) {
            if (times[0].tv_nsec == -1L) {
                GetSystemTimeAsFileTime(&atime);
            } else {
                timespec_to_filetime(&times[0], &atime);
            }
            pAtime = &atime;
        }
        if (times[1].tv_nsec != -2L) {
            if (times[1].tv_nsec == -1L) {
                GetSystemTimeAsFileTime(&mtime);
            } else {
                timespec_to_filetime(&times[1], &mtime);
            }
            pMtime = &mtime;
        }
    }

    BOOL ok = SetFileTime(hFile, NULL, pAtime, pMtime);
    CloseHandle(hFile);

    if (!ok) {
        errno = EACCES;
        return -1;
    }

    return 0;
}
