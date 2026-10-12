# Módulo 07: Redes y Abstracción Multiplataforma (`INetworking_Win32.h`)

## 1. Divergencias entre Sockets BSD y Windows Sockets (Winsock2)

Aunque la API de sockets de Windows (Winsock) fue diseñada históricamente a imagen y semejanza de los sockets de Berkeley (4.2BSD), existen diferencias críticas en su implementación y ciclo de vida:

| Característica | Sockets BSD (OpenBSD / Linux) | Windows Sockets (`ws2_32.dll`) |
| :--- | :--- | :--- |
| **Tipo de dato** | Entero estándar (`int fd >= 0`) | Descriptor sin signo (`SOCKET`, tipado como `UINT_PTR`) |
| **Valor de error** | `-1` | `INVALID_SOCKET` (`(SOCKET)(~0)`) |
| **Inicialización** | Inmediata (integrada en el kernel) | Obligatoria mediante llamada a `WSAStartup()` |
| **Finalización** | `close(fd)` | `closesocket(s)` y `WSACleanup()` |
| **Reporte de errores** | Variable global `errno` | Función `WSAGetLastError()` |
| **Lectura / Escritura genérica** | `read(fd, ...)` y `write(fd, ...)` funcionan igual en archivos y sockets | `read()` y `write()` fallan en sockets; se debe usar `recv()` y `send()` |

---

## 2. Arquitectura de la Interfaz Condicional

Para garantizar máxima portabilidad sin acoplar código Unix a llamadas de Winsock2, establecemos una capa de abstracción condicional tal como se propuso en los requerimientos del proyecto:

```c
/* compat/include/networking.h */
#ifndef _COMPAT_NETWORKING_H_
#define _COMPAT_NETWORKING_H_

#if defined(_WIN32) || defined(_WIN64)
#  include "INetworking_Win32.h"
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

#endif /* _COMPAT_NETWORKING_H_ */
```

Y en la interfaz nativa para Win32:

```c
/* compat/include/INetworking_Win32.h */
#ifndef _INETWORKING_WIN32_H_
#define _INETWORKING_WIN32_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>

#pragma comment(lib, "ws2_32.lib")

static inline int compat_net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void compat_net_cleanup(void) {
    WSACleanup();
}

static inline int compat_net_close(SOCKET s) {
    return closesocket(s);
}

static inline int compat_net_errno(void) {
    int err = WSAGetLastError();
    switch (err) {
    case WSAEWOULDBLOCK:   return EWOULDBLOCK;
    case WSAEINPROGRESS:   return EINPROGRESS;
    case WSAENOTCONN:      return ENOTCONN;
    case WSAECONNREFUSED:  return ECONNREFUSED;
    case WSAETIMEDOUT:     return ETIMEDOUT;
    default:               return err;
    }
}

#endif /* _INETWORKING_WIN32_H_ */
```

---

## 3. Manejo de Descriptores Mixtos en Select / Poll

En Unix, la llamada `select()` o `poll()` puede monitorear de forma transparente una mezcla de descriptores de archivos regulares, tuberías (*pipes*), terminales y sockets.

En Windows:
- `select()` de Winsock **únicamente** acepta descriptores de tipo `SOCKET`. Si se le pasa un descriptor de archivo o un handle de consola, fallará con `WSAENOTSOCK`.
- Para esperar eventos en múltiples objetos en Windows (procesos, pipes, eventos), la API canónica es `WaitForMultipleObjects()`. Para integrar sockets en este modelo, se debe vincular el socket a un evento Win32 mediante `WSAEventSelect()`.
