#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
    size_t pos = 0;
    int c;

    if (lineptr == NULL || n == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (*lineptr == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }

    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_n = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_n);
            if (new_ptr == NULL) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = new_ptr;
            *n = new_n;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n')
            break;
    }

    if (pos == 0 && c == EOF)
        return -1;

    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}
