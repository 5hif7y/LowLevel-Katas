/*
 * fs.c - stat(), chmod() and directories, with AF_UNIX socket files.
 *
 * An AF_UNIX socket on NTFS is a *reparse point* tagged
 * IO_REPARSE_TAG_AF_UNIX. The UCRT's stat() follows reparse points, the
 * socket target cannot be opened as a file, and stat() fails. dtach and
 * abduco call stat() on their socket constantly (S_ISSOCK, S_IXUSR...).
 *
 * Both programs also abuse the socket's *permission bits* as a tiny
 * database: S_IXUSR = "a client is attached", S_IXGRP = "the command
 * exited". NTFS has no Unix mode bits, so we store those two bits in two
 * harmless file attributes of the reparse point itself:
 *     S_IXUSR <-> FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
 *     S_IXGRP <-> FILE_ATTRIBUTE_HIDDEN
 */
#include "compat_internal.h"
#include <aclapi.h>
#include <sddl.h>

#ifndef IO_REPARSE_TAG_AF_UNIX
#define IO_REPARSE_TAG_AF_UNIX 0x80000023L
#endif

#define ATTR_XUSR FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
#define ATTR_XGRP FILE_ATTRIBUTE_HIDDEN

static time_t ft_to_time(FILETIME ft)
{
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return (time_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static DWORD reparse_tag(const wchar_t *path)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(path, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	FindClose(h);
	return (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? fd.dwReserved0 : 0;
}

/* Is the owner of `path` the user running this process? */
static int owned_by_me(const wchar_t *path)
{
	PSID owner = NULL;
	PSECURITY_DESCRIPTOR sd = NULL;
	int mine = 0;
	if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
	                          &owner, NULL, NULL, NULL, &sd) != ERROR_SUCCESS)
		return 0;
	HANDLE tok;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
		BYTE buf[256];
		DWORD len;
		if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &len))
			mine = EqualSid(owner, ((TOKEN_USER *)buf)->User.Sid);
		/* Files created by an elevated admin are owned by BUILTIN\Administrators. */
		if (!mine && GetTokenInformation(tok, TokenOwner, buf, sizeof buf, &len))
			mine = EqualSid(owner, ((TOKEN_OWNER *)buf)->Owner);
		CloseHandle(tok);
	}
	LocalFree(sd);
	return mine;
}

int compat_stat(const char *path, struct stat *st)
{
	wchar_t *w = utf8_to_wide(path);
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!w) {
		errno = ENOMEM;
		return -1;
	}
	if (!GetFileAttributesExW(w, GetFileExInfoStandard, &a)) {
		DWORD e = GetLastError();
		free(w);
		errno = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ||
		         e == ERROR_INVALID_NAME) ? ENOENT : EACCES;
		return -1;
	}
	memset(st, 0, sizeof *st);
	unsigned mode;
	if ((a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    reparse_tag(w) == (DWORD)IO_REPARSE_TAG_AF_UNIX) {
		mode = C_S_IFSOCK | 0600;
		if (a.dwFileAttributes & ATTR_XUSR) mode |= C_S_IXUSR;
		if (a.dwFileAttributes & ATTR_XGRP) mode |= C_S_IXGRP;
	} else if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		mode = _S_IFDIR | 0700;
	} else {
		mode = _S_IFREG | ((a.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0400 : 0600);
		const wchar_t *dot = wcsrchr(w, L'.');
		if (dot && (!_wcsicmp(dot, L".exe") || !_wcsicmp(dot, L".bat") ||
		            !_wcsicmp(dot, L".cmd") || !_wcsicmp(dot, L".com")))
			mode |= 0100;
	}
	/*
	 * No group/other bits: the UCRT copies the user bits into group and
	 * other (0755 for every directory), which makes abduco's "socket dir
	 * must not be group/world accessible" check fail with EACCES.
	 * Ownership is real: compared against the token's SID.
	 */
	st->st_mode = (unsigned short)mode;
	st->st_nlink = 1;
	st->st_uid = owned_by_me(w) ? (short)compat_getuid() : (short)-2;
	st->st_gid = st->st_uid;
	st->st_size = (_off_t)a.nFileSizeLow;
	st->st_atime = ft_to_time(a.ftLastAccessTime);
	st->st_mtime = ft_to_time(a.ftLastWriteTime);
	st->st_ctime = ft_to_time(a.ftCreationTime);
	free(w);
	return 0;
}

int compat_chmod(const char *path, int mode)
{
	wchar_t *w = utf8_to_wide(path);
	DWORD a = GetFileAttributesW(w);
	int r = 0;
	if (a == INVALID_FILE_ATTRIBUTES) {
		errno = ENOENT;
		r = -1;
	} else if ((a & FILE_ATTRIBUTE_REPARSE_POINT) &&
	           reparse_tag(w) == (DWORD)IO_REPARSE_TAG_AF_UNIX) {
		DWORD na = a & ~(ATTR_XUSR | ATTR_XGRP | FILE_ATTRIBUTE_REPARSE_POINT);
		if (mode & C_S_IXUSR) na |= ATTR_XUSR;
		if (mode & C_S_IXGRP) na |= ATTR_XGRP;
		if (!na)
			na = FILE_ATTRIBUTE_NORMAL;
		if (!SetFileAttributesW(w, na)) {
			errno = EACCES;
			r = -1;
		}
	} else {
		r = _wchmod(w, (mode & 0200) ? (_S_IREAD | _S_IWRITE) : _S_IREAD);
	}
	free(w);
	return r;
}

int compat_mkdir(const char *path, int mode)
{
	(void)mode; /* the new directory inherits the parent's ACL */
	wchar_t *w = utf8_to_wide(path);
	int r = _wmkdir(w);
	free(w);
	return r;
}

int compat_unlink(const char *path)
{
	wchar_t *w = utf8_to_wide(path);
	/* DeleteFileW removes the reparse point itself, socket included. */
	int r = DeleteFileW(w) ? 0 : -1;
	if (r < 0) {
		DWORD e = GetLastError();
		errno = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
	}
	free(w);
	return r;
}

int compat_fchdir(int fd)
{
	(void)fd;
	errno = ENOSYS; /* open(".") of a directory is not possible either */
	return -1;
}

int compat_mkfifo(const char *path, int mode)
{
	(void)path; (void)mode;
	errno = ENOSYS; /* named pipes live in \\.\pipe\, not in the FS */
	return -1;
}

char *compat_realpath(const char *path, char *resolved)
{
	wchar_t *w = utf8_to_wide(path), full[MAX_PATH * 2];
	DWORD n = GetFullPathNameW(w, MAX_PATH * 2, full, NULL);
	free(w);
	if (!n || n >= MAX_PATH * 2 || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
		errno = ENOENT;
		return NULL;
	}
	char *u = wide_to_utf8(full);
	if (!resolved)
		return u;
	strncpy(resolved, u, 4095);
	resolved[4095] = 0;
	free(u);
	return resolved;
}

/* ---- directories ------------------------------------------------------- */

struct compat_dir {
	HANDLE h;
	WIN32_FIND_DATAW fd;
	int first;
	struct dirent ent;
};

DIR *compat_opendir(const char *path)
{
	size_t l = strlen(path);
	char *pat = malloc(l + 3);
	memcpy(pat, path, l);
	if (l && path[l - 1] != '/' && path[l - 1] != '\\')
		pat[l++] = '\\';
	pat[l++] = '*';
	pat[l] = 0;
	wchar_t *w = utf8_to_wide(pat);
	free(pat);
	DIR *d = calloc(1, sizeof *d);
	d->h = FindFirstFileW(w, &d->fd);
	free(w);
	if (d->h == INVALID_HANDLE_VALUE) {
		free(d);
		errno = ENOENT;
		return NULL;
	}
	d->first = 1;
	return d;
}

struct dirent *compat_readdir(DIR *d)
{
	if (!d->first && !FindNextFileW(d->h, &d->fd))
		return NULL;
	d->first = 0;
	char *u = wide_to_utf8(d->fd.cFileName);
	strncpy(d->ent.d_name, u, sizeof d->ent.d_name - 1);
	free(u);
	DWORD a = d->fd.dwFileAttributes;
	d->ent.d_type = (a & FILE_ATTRIBUTE_REPARSE_POINT &&
	                 d->fd.dwReserved0 == (DWORD)IO_REPARSE_TAG_AF_UNIX) ? 12 :
	                (a & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
	return &d->ent;
}

int compat_closedir(DIR *d)
{
	FindClose(d->h);
	free(d);
	return 0;
}

int compat_scandir(const char *dir, struct dirent ***list,
                   int (*filter)(const struct dirent *),
                   int (*cmp)(const struct dirent **, const struct dirent **))
{
	DIR *d = compat_opendir(dir);
	if (!d)
		return -1;
	struct dirent **v = NULL, *e;
	int n = 0, cap = 0;
	while ((e = compat_readdir(d))) {
		if (filter && !filter(e))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 16;
			v = realloc(v, cap * sizeof *v);
		}
		v[n] = malloc(sizeof *e);
		memcpy(v[n++], e, sizeof *e);
	}
	compat_closedir(d);
	if (cmp && n > 1)
		qsort(v, n, sizeof *v, (int (*)(const void *, const void *))cmp);
	*list = v;
	return n;
}

int compat_alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

/* POSIX basename()/dirname(): both separators, may modify the input. */
char *compat_basename(char *path)
{
	static char dot[] = ".";
	if (!path || !*path)
		return dot;
	size_t l = strlen(path);
	while (l > 1 && (path[l - 1] == '/' || path[l - 1] == '\\'))
		path[--l] = 0;
	char *s1 = strrchr(path, '/'), *s2 = strrchr(path, '\\');
	char *s = s1 > s2 ? s1 : s2;
	return s ? s + 1 : path;
}

char *compat_dirname(char *path)
{
	static char dot[] = ".";
	if (!path || !*path)
		return dot;
	char *s1 = strrchr(path, '/'), *s2 = strrchr(path, '\\');
	char *s = s1 > s2 ? s1 : s2;
	if (!s)
		return dot;
	if (s == path)
		s[1] = 0;
	else
		*s = 0;
	return path;
}
