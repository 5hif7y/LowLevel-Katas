#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H
#include "compat.h"
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_SOCK 12
#define opendir(p)          compat_opendir(p)
#define readdir(d)          compat_readdir(d)
#define closedir(d)         compat_closedir(d)
#define scandir(d, l, f, c) compat_scandir(d, l, f, c)
#define alphasort           compat_alphasort
#endif
