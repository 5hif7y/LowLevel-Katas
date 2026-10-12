#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#undef st_atime
#undef st_mtime
#undef st_ctime

int
bsd_stat(const char *path, struct bsd_stat *sb)
{
    struct _stat64i32 s;
    if (_stat64i32(path, &s) != 0)
        return -1;

    sb->st_dev = s.st_dev;
    sb->st_ino = s.st_ino;
    sb->st_mode = s.st_mode;
    sb->st_nlink = s.st_nlink;
    sb->st_uid = s.st_uid;
    sb->st_gid = s.st_gid;
    sb->st_rdev = s.st_rdev;
    sb->st_size = s.st_size;
    sb->st_atim.tv_sec = s.st_atime;
    sb->st_atim.tv_nsec = 0;
    sb->st_mtim.tv_sec = s.st_mtime;
    sb->st_mtim.tv_nsec = 0;
    sb->st_ctim.tv_sec = s.st_ctime;
    sb->st_ctim.tv_nsec = 0;
    sb->st_blksize = 4096;
    sb->st_blocks = (s.st_size + 511) / 512;
    return 0;
}

int
bsd_fstat(int fd, struct bsd_stat *sb)
{
    struct _stat64i32 s;
    if (_fstat64i32(fd, &s) != 0)
        return -1;

    sb->st_dev = s.st_dev;
    sb->st_ino = s.st_ino;
    sb->st_mode = s.st_mode;
    sb->st_nlink = s.st_nlink;
    sb->st_uid = s.st_uid;
    sb->st_gid = s.st_gid;
    sb->st_rdev = s.st_rdev;
    sb->st_size = s.st_size;
    sb->st_atim.tv_sec = s.st_atime;
    sb->st_atim.tv_nsec = 0;
    sb->st_mtim.tv_sec = s.st_mtime;
    sb->st_mtim.tv_nsec = 0;
    sb->st_ctim.tv_sec = s.st_ctime;
    sb->st_ctim.tv_nsec = 0;
    sb->st_blksize = 4096;
    sb->st_blocks = (s.st_size + 511) / 512;
    return 0;
}
