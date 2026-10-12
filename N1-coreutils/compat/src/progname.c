#include <bsd/stdlib.h>
#include <string.h>
#include <windows.h>

static char progname_buf[MAX_PATH];
static const char *current_progname = NULL;
char *__progname = "bsd_util";

const char *
getprogname(void)
{
    if (current_progname != NULL)
        return current_progname;

    if (GetModuleFileNameA(NULL, progname_buf, sizeof(progname_buf)) > 0) {
        char *p = strrchr(progname_buf, '\\');
        char *p2 = strrchr(progname_buf, '/');
        if (p2 > p)
            p = p2;
        if (p != NULL)
            current_progname = p + 1;
        else
            current_progname = progname_buf;

        /* Strip .exe extension if present */
        size_t len = strlen(current_progname);
        if (len > 4 && _stricmp(current_progname + len - 4, ".exe") == 0) {
            char *mutable_p = (char *)current_progname;
            mutable_p[len - 4] = '\0';
        }
    } else {
        current_progname = "unknown";
    }
    __progname = (char *)current_progname;
    return current_progname;
}

void
setprogname(const char *progname)
{
    if (progname == NULL)
        return;
    const char *p = strrchr(progname, '\\');
    const char *p2 = strrchr(progname, '/');
    if (p2 > p)
        p = p2;
    if (p != NULL)
        current_progname = p + 1;
    else
        current_progname = progname;
    __progname = (char *)current_progname;
}
