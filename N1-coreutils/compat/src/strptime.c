#include <time.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

char *
strptime(const char *buf, const char *fmt, struct tm *tm)
{
    char c;
    const char *bp = buf;

    while ((c = *fmt++) != '\0') {
        if (isspace((unsigned char)c)) {
            while (isspace((unsigned char)*bp))
                bp++;
            continue;
        }

        if (c != '%') {
            if (*bp++ != c)
                return NULL;
            continue;
        }

        c = *fmt++;
        switch (c) {
        case '%':
            if (*bp++ != '%')
                return NULL;
            break;

        case 'F': { /* %Y-%m-%d */
            char *ret = strptime(bp, "%Y-%m-%d", tm);
            if (ret == NULL)
                return NULL;
            bp = ret;
            break;
        }

        case 'T': { /* %H:%M:%S */
            char *ret = strptime(bp, "%H:%M:%S", tm);
            if (ret == NULL)
                return NULL;
            bp = ret;
            break;
        }

        case 'Y': { /* 4-digit year */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 0)
                return NULL;
            tm->tm_year = (int)(val - 1900);
            bp = ep;
            break;
        }

        case 'y': { /* 2-digit year */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 0 || val > 99)
                return NULL;
            if (val < 69)
                val += 2000;
            else
                val += 1900;
            tm->tm_year = (int)(val - 1900);
            bp = ep;
            break;
        }

        case 'm': { /* month 1-12 */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 1 || val > 12)
                return NULL;
            tm->tm_mon = (int)(val - 1);
            bp = ep;
            break;
        }

        case 'd':
        case 'e': { /* day 1-31 */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 1 || val > 31)
                return NULL;
            tm->tm_mday = (int)val;
            bp = ep;
            break;
        }

        case 'H': { /* hour 0-23 */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 0 || val > 23)
                return NULL;
            tm->tm_hour = (int)val;
            bp = ep;
            break;
        }

        case 'M': { /* minute 0-59 */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 0 || val > 59)
                return NULL;
            tm->tm_min = (int)val;
            bp = ep;
            break;
        }

        case 'S': { /* second 0-60 */
            char *ep;
            long val = strtol(bp, &ep, 10);
            if (ep == bp || val < 0 || val > 60)
                return NULL;
            tm->tm_sec = (int)val;
            bp = ep;
            break;
        }

        default:
            return NULL;
        }
    }

    return (char *)bp;
}
