#include <string.h>

#include "fat_vfs.h"

/* Si fat_file o fat_dir crecen más que el buffer del VFS, que no compile. */
typedef char fat_file_fits[sizeof(fat_file) <= VFS_PRIV_SIZE ? 1 : -1];
typedef char fat_dir_fits[sizeof(fat_dir) <= VFS_PRIV_SIZE ? 1 : -1];

static int
fv_open(void *fs, void *fh, const char *path, int flags)
{
	return fat_open(fs, fh, path, flags);
}

static int32_t
fv_read(void *fh, void *buf, uint32_t n)
{
	return fat_read(fh, buf, n);
}

static int32_t
fv_write(void *fh, const void *buf, uint32_t n)
{
	return fat_write(fh, buf, n);
}

static int64_t
fv_seek(void *fh, int64_t off, int whence)
{
	return fat_seek(fh, off, whence);
}

static int
fv_close(void *fh)
{
	return fat_close(fh);
}

static int
fv_stat(void *fs, const char *path, vfs_info *st)
{
	fat_info fi;
	int r;

	if ((r = fat_stat(fs, path, &fi)))
		return r;
	st->size = fi.size;
	st->mtime = fi.mtime;
	st->is_dir = (fi.attr & FAT_ATTR_DIR) != 0;
	st->readonly = (fi.attr & FAT_ATTR_RDONLY) != 0;
	return FS_OK;
}

static int
fv_mkdir(void *fs, const char *path)
{
	return fat_mkdir(fs, path);
}

static int
fv_unlink(void *fs, const char *path)
{
	return fat_unlink(fs, path);
}

static int
fv_rmdir(void *fs, const char *path)
{
	return fat_rmdir(fs, path);
}

static int
fv_opendir(void *fs, void *dh, const char *path)
{
	return fat_opendir(fs, dh, path);
}

static int
fv_readdir(void *dh, vfs_dirent *ent)
{
	fat_dirent fe;
	int r;

	if ((r = fat_readdir(dh, &fe)) <= 0)
		return r;
	memcpy(ent->name, fe.name, sizeof(fe.name));
	ent->size = fe.size;
	ent->is_dir = (fe.attr & FAT_ATTR_DIR) != 0;
	return 1;
}

static int
fv_sync(void *fs)
{
	return fat_sync(fs);
}

const vfs_ops fat_vfs_ops = {
	"fat",
	fv_open, fv_read, fv_write, fv_seek, fv_close,
	fv_stat, fv_mkdir, fv_unlink, fv_rmdir,
	fv_opendir, fv_readdir,
	fv_sync
};
