#include <pwd.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

static char username_buf[256] = "user";
static struct passwd pw_static = {
    username_buf,
    "*",
    1000,
    1000,
    username_buf,
    "C:\\",
    "cmd.exe"
};

struct passwd *
getpwuid(uid_t uid)
{
    (void)uid;
    DWORD len = sizeof(username_buf);
    if (GetUserNameA(username_buf, &len)) {
        pw_static.pw_name = username_buf;
        pw_static.pw_gecos = username_buf;
    }
    return &pw_static;
}

struct passwd *
getpwnam(const char *name)
{
    (void)name;
    return getpwuid(1000);
}

static char groupname_buf[] = "staff";
static char *group_members[] = { username_buf, NULL };
static struct group gr_static = {
    groupname_buf,
    "*",
    1000,
    group_members
};

struct group *
getgrgid(gid_t gid)
{
    (void)gid;
    return &gr_static;
}

struct group *
getgrnam(const char *name)
{
    (void)name;
    return &gr_static;
}

int
ioctl(int fd, unsigned long req, ...)
{
    (void)fd;
    if (req == TIOCGWINSZ) {
        va_list ap;
        va_start(ap, req);
        struct winsize *ws = va_arg(ap, struct winsize *);
        va_end(ap);

        if (ws != NULL) {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (h != INVALID_HANDLE_VALUE && h != NULL && GetConsoleScreenBufferInfo(h, &csbi)) {
                ws->ws_col = (unsigned short)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
                ws->ws_row = (unsigned short)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
                return 0;
            }
            ws->ws_col = 80;
            ws->ws_row = 24;
            return 0;
        }
    }
    return -1;
}

const char *
user_from_uid(uid_t uid, int nouser)
{
    (void)nouser;
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "user";
}

const char *
group_from_gid(gid_t gid, int nogroup)
{
    (void)nogroup;
    struct group *gr = getgrgid(gid);
    return gr ? gr->gr_name : "staff";
}

char *
fflagstostr(unsigned int flags)
{
    (void)flags;
    return _strdup("-");
}

void *
reallocarray(void *optr, size_t nmemb, size_t size)
{
    if (nmemb > 0 && SIZE_MAX / nmemb < size) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(optr, nmemb * size);
}

void *
recallocarray(void *ptr, size_t oldnmemb, size_t newnmemb, size_t size)
{
    if (ptr == NULL)
        return calloc(newnmemb, size);

    if (newnmemb > 0 && SIZE_MAX / newnmemb < size) {
        errno = ENOMEM;
        return NULL;
    }

    size_t oldsize = oldnmemb * size;
    size_t newsize = newnmemb * size;

    void *newptr = realloc(ptr, newsize);
    if (newptr == NULL)
        return NULL;

    if (newsize > oldsize)
        memset((char *)newptr + oldsize, 0, newsize - oldsize);

    return newptr;
}

int
readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL;
    return -1;
}

int
wcwidth(wchar_t wc)
{
    if (wc == 0)
        return 0;
    if (wc < 32 || (wc >= 0x7f && wc < 0xa0))
        return -1;
    return 1;
}
