/* sys/time.h - struct timeval with the exact Winsock layout (two longs). */
#ifndef COMPAT_SYS_TIME_H
#define COMPAT_SYS_TIME_H
#include "compat.h"
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif
#endif
