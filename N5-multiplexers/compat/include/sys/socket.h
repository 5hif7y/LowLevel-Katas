/* sys/socket.h - BSD socket API over Winsock2, WITHOUT including it.
 * Layouts match Winsock exactly (sa_family is a USHORT there too). */
#ifndef COMPAT_SYS_SOCKET_H
#define COMPAT_SYS_SOCKET_H
#include "compat.h"
#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_LOCAL  AF_UNIX
#define AF_INET   2
#define PF_UNIX   AF_UNIX
#define PF_LOCAL  AF_UNIX
#define PF_INET   AF_INET
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
typedef unsigned short sa_family_t;
struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};
#define socket(d, t, p)        compat_socket(d, t, p)
#define bind(f, a, l)          compat_bind(f, a, l)
#define listen(f, b)           compat_listen(f, b)
#define accept(f, a, l)        compat_accept(f, a, l)
#define connect(f, a, l)       compat_connect(f, a, l)
#define shutdown(f, h)         compat_shutdown(f, h)
#define socketpair(d, t, p, v) compat_socketpair(d, t, p, v)
#endif
