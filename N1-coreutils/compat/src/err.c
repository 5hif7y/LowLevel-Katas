#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Forward declaration for getprogname */
const char *getprogname(void);

void
vwarn(const char *fmt, va_list ap)
{
    const char *progname = getprogname();
    int saved_errno = errno;

    if (progname != NULL && *progname != '\0')
        fprintf(stderr, "%s: ", progname);
    if (fmt != NULL) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, ": %s\n", strerror(saved_errno));
    } else {
        fprintf(stderr, "%s\n", strerror(saved_errno));
    }
}

void
vwarnx(const char *fmt, va_list ap)
{
    const char *progname = getprogname();

    if (progname != NULL && *progname != '\0')
        fprintf(stderr, "%s: ", progname);
    if (fmt != NULL)
        vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void
warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void
warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void
verr(int eval, const char *fmt, va_list ap)
{
    vwarn(fmt, ap);
    exit(eval);
}

void
verrx(int eval, const char *fmt, va_list ap)
{
    vwarnx(fmt, ap);
    exit(eval);
}

void
err(int eval, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verr(eval, fmt, ap);
    va_end(ap);
}

void
errx(int eval, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verrx(eval, fmt, ap);
    va_end(ap);
}

void
vwarnc(int code, const char *fmt, va_list ap)
{
    const char *progname = getprogname();

    if (progname != NULL && *progname != '\0')
        fprintf(stderr, "%s: ", progname);
    if (fmt != NULL) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, ": %s\n", strerror(code));
    } else {
        fprintf(stderr, "%s\n", strerror(code));
    }
}

void
warnc(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarnc(code, fmt, ap);
    va_end(ap);
}

void
verrc(int eval, int code, const char *fmt, va_list ap)
{
    vwarnc(code, fmt, ap);
    exit(eval);
}

void
errc(int eval, int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verrc(eval, code, fmt, ap);
    va_end(ap);
}
