#ifndef _NETWORKING_H_
#define _NETWORKING_H_

#if defined(_WIN32) || defined(__WIN32)
#include "INetworking_Win32.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define close_socket(s) close(s)
#define get_socket_errno() (errno)
#define net_init() (0)
#define net_cleanup() ((void)0)
#endif

#endif /* _NETWORKING_H_ */
