#include <libgen.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define IS_DIR_SEP(c) ((c) == '/' || (c) == '\\')

char *
basename(char *path)
{
	static char bname[PATH_MAX];
	size_t len;
	const char *endp, *startp;

	/* Empty or NULL string gets treated as "." */
	if (path == NULL || *path == '\0') {
		bname[0] = '.';
		bname[1] = '\0';
		return (bname);
	}

	/* Strip any trailing slashes */
	endp = path + strlen(path) - 1;
	while (endp > path && IS_DIR_SEP(*endp))
		endp--;

	/* All slashes becomes "/" or "\" */
	if (endp == path && IS_DIR_SEP(*endp)) {
		bname[0] = *endp;
		bname[1] = '\0';
		return (bname);
	}

	/* Find the start of the base */
	startp = endp;
	while (startp > path && !IS_DIR_SEP(*(startp - 1)) && *(startp - 1) != ':')
		startp--;

	len = (size_t)(endp - startp + 1);
	if (len >= sizeof(bname)) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	memcpy(bname, startp, len);
	bname[len] = '\0';
	return (bname);
}

char *
dirname(char *path)
{
	static char dname[PATH_MAX];
	size_t len;
	const char *endp;

	/* Empty or NULL string gets treated as "." */
	if (path == NULL || *path == '\0') {
		dname[0] = '.';
		dname[1] = '\0';
		return (dname);
	}

	/* Strip any trailing slashes */
	endp = path + strlen(path) - 1;
	while (endp > path && IS_DIR_SEP(*endp))
		endp--;

	/* Find the start of the dir */
	while (endp > path && !IS_DIR_SEP(*endp))
		endp--;

	/* Either the dir is "/" or "\" or there are no slashes */
	if (endp == path) {
		dname[0] = IS_DIR_SEP(*endp) ? *endp : '.';
		dname[1] = '\0';
		return (dname);
	} else {
		/* Move forward past the separating slashes */
		do {
			endp--;
		} while (endp > path && IS_DIR_SEP(*endp));
	}

	len = (size_t)(endp - path + 1);
	if (len >= sizeof(dname)) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	memcpy(dname, path, len);
	dname[len] = '\0';
	return (dname);
}
