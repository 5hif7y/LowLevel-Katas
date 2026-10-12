#ifndef _INETWORKING_WIN32_H_
#define _INETWORKING_WIN32_H_

#if defined(_WIN32) || defined(__WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sys/cdefs.h>

#pragma comment(lib, "ws2_32.lib")

__BEGIN_DECLS

/* WinSock Initialization & Teardown Interface */
static inline int net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void net_cleanup(void) {
    WSACleanup();
}

/* Socket Operation Abstractions */
#ifndef close_socket
#define close_socket(s) closesocket(s)
#endif

#ifndef get_socket_errno
#define get_socket_errno() WSAGetLastError()
#endif

/* Standard POSIX socket error mappings for Windows */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED WSAECONNREFUSED
#endif
#ifndef ENOTCONN
#define ENOTCONN WSAENOTCONN
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT WSAETIMEDOUT
#endif

__END_DECLS

#endif /* _WIN32 */

#endif /* _INETWORKING_WIN32_H_ */
