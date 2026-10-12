#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void *
bsd_setmode(const char *p)
{
    if (p == NULL || *p == '\0')
        return NULL;

    char *ep;
    long val = strtol(p, &ep, 8);
    if (*ep == '\0' && val >= 0 && val <= 07777) {
        mode_t *m = (mode_t *)malloc(sizeof(mode_t));
        if (m != NULL)
            *m = (mode_t)val;
        return m;
    }

    mode_t *m = (mode_t *)malloc(sizeof(mode_t));
    if (m != NULL)
        *m = 0755;
    return m;
}

mode_t
bsd_getmode(const void *bbox, mode_t omode)
{
    if (bbox == NULL)
        return omode;
    return *(const mode_t *)bbox;
}
