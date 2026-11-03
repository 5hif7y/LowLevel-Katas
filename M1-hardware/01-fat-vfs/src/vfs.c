#include <string.h>

#include "vfs.h"

typedef struct mount_point {
	char		 prefix[VFS_PREFIX_MAX];
	size_t		 len;		/* 0 = montado en "/" */
	const vfs_ops	*ops;
	void		*fs;
} mount_point;

static mount_point mounts[VFS_MAX_MOUNTS];

static size_t
prefix_len(const char *prefix)
{
	size_t len;

	if (!prefix || prefix[0] != '/')
		return (size_t)-1;
	len = strlen(prefix);
	while (len > 0 && prefix[len - 1] == '/')
		len--;
	return len;
}

static mount_point *
find_exact(const char *prefix, size_t len)
{
	int i;

	for (i = 0; i < VFS_MAX_MOUNTS; i++)
		if (mounts[i].ops && mounts[i].len == len &&
		    memcmp(mounts[i].prefix, prefix, len) == 0)
			return &mounts[i];
	return NULL;
}

int
vfs_mount(const char *prefix, const vfs_ops *ops, void *fs)
{
	size_t len = prefix_len(prefix);
	int i;

	if (len == (size_t)-1 || len >= VFS_PREFIX_MAX || !ops)
		return FS_EINVAL;
	if (find_exact(prefix, len))
		return FS_EEXIST;
	for (i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (mounts[i].ops)
			continue;
		memcpy(mounts[i].prefix, prefix, len);
		mounts[i].prefix[len] = '\0';
		mounts[i].len = len;
		mounts[i].ops = ops;
		mounts[i].fs = fs;
		return FS_OK;
	}
	return FS_ENOSPC;
}

int
vfs_unmount(const char *prefix)
{
	size_t len = prefix_len(prefix);
	mount_point *m;
	int r = FS_OK;

	if (len == (size_t)-1 || !(m = find_exact(prefix, len)))
		return FS_ENOENT;
	if (m->ops->sync)
		r = m->ops->sync(m->fs);
	memset(m, 0, sizeof(*m));
	return r;
}

int
vfs_sync_all(void)
{
	int i, r, first = FS_OK;

	for (i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (!mounts[i].ops || !mounts[i].ops->sync)
			continue;
		r = mounts[i].ops->sync(mounts[i].fs);
		if (r && !first)
			first = r;
	}
	return first;
}

/*
 * Prefijo más largo que coincide en un límite de componente: "/sd" toma
 * "/sd" y "/sd/x", pero no "/sdcard". Devuelve el resto de la ruta, que el
 * backend ve como absoluta dentro de su volumen.
 */
static const mount_point *
resolve(const char *path, const char **rest)
{
	const mount_point *best = NULL;
	size_t len;
	int i;

	if (!path || path[0] != '/')
		return NULL;
	for (i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (!mounts[i].ops)
			continue;
		len = mounts[i].len;
		if (memcmp(path, mounts[i].prefix, len) != 0)
			continue;
		if (path[len] != '/' && path[len] != '\0')
			continue;
		if (!best || len > best->len)
			best = &mounts[i];
	}
	if (best)
		*rest = path[best->len] ? path + best->len : "/";
	return best;
}

int
vfs_open(vfs_file *f, const char *path, int flags)
{
	const mount_point *m;
	const char *rest;
	int r;

	f->ops = NULL;
	if (!(m = resolve(path, &rest)))
		return FS_ENOENT;
	if ((r = m->ops->open(m->fs, f->priv.bytes, rest, flags)))
		return r;
	f->ops = m->ops;
	return FS_OK;
}

int32_t
vfs_read(vfs_file *f, void *buf, uint32_t n)
{
	return f->ops ? f->ops->read(f->priv.bytes, buf, n) : FS_EBADF;
}

int32_t
vfs_write(vfs_file *f, const void *buf, uint32_t n)
{
	return f->ops ? f->ops->write(f->priv.bytes, buf, n) : FS_EBADF;
}

int64_t
vfs_seek(vfs_file *f, int64_t off, int whence)
{
	return f->ops ? f->ops->seek(f->priv.bytes, off, whence) : FS_EBADF;
}

int
vfs_close(vfs_file *f)
{
	int r;

	if (!f->ops)
		return FS_EBADF;
	r = f->ops->close(f->priv.bytes);
	f->ops = NULL;
	return r;
}

/* Las operaciones por ruta comparten la misma forma: resolver y delegar. */
#define VFS_PATH_OP(name, call)					\
int								\
vfs_##name(const char *path)					\
{								\
	const mount_point *m;					\
	const char *rest;					\
								\
	if (!(m = resolve(path, &rest)))			\
		return FS_ENOENT;				\
	return m->ops->call ? m->ops->call(m->fs, rest) : FS_EINVAL; \
}

VFS_PATH_OP(mkdir, mkdir)
VFS_PATH_OP(unlink, unlink)
VFS_PATH_OP(rmdir, rmdir)

int
vfs_stat(const char *path, vfs_info *st)
{
	const mount_point *m;
	const char *rest;

	if (!(m = resolve(path, &rest)))
		return FS_ENOENT;
	return m->ops->stat(m->fs, rest, st);
}

int
vfs_opendir(vfs_dir *d, const char *path)
{
	const mount_point *m;
	const char *rest;
	int r;

	d->ops = NULL;
	if (!(m = resolve(path, &rest)))
		return FS_ENOENT;
	if ((r = m->ops->opendir(m->fs, d->priv.bytes, rest)))
		return r;
	d->ops = m->ops;
	return FS_OK;
}

int
vfs_readdir(vfs_dir *d, vfs_dirent *ent)
{
	return d->ops ? d->ops->readdir(d->priv.bytes, ent) : FS_EBADF;
}
