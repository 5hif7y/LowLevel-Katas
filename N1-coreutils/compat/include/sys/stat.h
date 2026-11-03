#ifndef _COMPAT_SYS_STAT_H_
#define _COMPAT_SYS_STAT_H_

#include <../ucrt/sys/stat.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>
#include <direct.h>

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (0)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) (0)
#endif

#ifndef S_ISUID
#define S_ISUID 0004000
#endif
#ifndef S_ISGID
#define S_ISGID 0002000
#endif
#ifndef S_ISVTX
#define S_ISVTX 0001000
#define S_ISTXT S_ISVTX
#endif

#ifndef S_IRWXU
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#endif
#ifndef S_IRWXG
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#endif
#ifndef S_IRWXO
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#endif

#ifndef DEFFILEMODE
#define DEFFILEMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)
#endif

#ifndef UTIME_NOW
#define UTIME_NOW -1L
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT -2L
#endif
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

static inline int compat_mkdir(const char *path, mode_t mode) {
    (void)mode;
    return _mkdir(path);
}
#ifndef mkdir
#define mkdir compat_mkdir
#endif
#ifndef umask
#define umask _umask
#endif

#ifndef S_ISBLK
#define S_ISBLK(m) 0
#endif
#ifndef S_IFIFO
#define S_IFIFO _S_IFIFO
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif
#ifndef S_IFBLK
#define S_IFBLK 0060000
#endif

struct bsd_stat {
    dev_t st_dev;
    ino_t st_ino;
    unsigned short st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    int64_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long st_blksize;
    long st_blocks;
    unsigned int st_flags;
    unsigned int st_gen;
};

#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#define stat bsd_stat
#define fstat bsd_fstat
#define lstat bsd_stat

__BEGIN_DECLS
int bsd_stat(const char *path, struct bsd_stat *sb);
int bsd_fstat(int fd, struct bsd_stat *sb);
int futimens(int fd, const struct timespec times[2]);
int utimensat(int fd, const char *path, const struct timespec times[2], int flag);
char *strptime(const char *buf, const char *fmt, struct tm *tm);
void strmode(mode_t mode, char *p);
__END_DECLS

#ifndef timegm
#define timegm _mkgmtime
#endif

#endif /* _COMPAT_SYS_STAT_H_ */
