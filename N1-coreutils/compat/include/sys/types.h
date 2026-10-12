#ifndef _COMPAT_SYS_TYPES_H_
#define _COMPAT_SYS_TYPES_H_

#include <../ucrt/sys/types.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _BSDTYPES_DEFINED
#define _BSDTYPES_DEFINED
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
#endif

#if !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T_)
#define _SSIZE_T_DEFINED
#define _SSIZE_T_
typedef intptr_t ssize_t;
#endif

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif

#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
#endif

#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef unsigned int gid_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

#ifndef _NLINK_T_DEFINED
#define _NLINK_T_DEFINED
typedef short nlink_t;
#endif

#ifndef _BLKCNT_T_DEFINED
#define _BLKCNT_T_DEFINED
typedef int64_t blkcnt_t;
#endif

#ifndef major
#define major(dev) ((int)(((dev) >> 8) & 0xff))
#endif
#ifndef minor
#define minor(dev) ((int)((dev) & 0xff))
#endif

#endif /* _COMPAT_SYS_TYPES_H_ */
