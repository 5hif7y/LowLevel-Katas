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

---

## 4. `SOCKET` no es un `int` (y `read`/`write` no sirven)

Aunque `INetworking_Win32.h` ya marca la diferencia, conviene repetirla porque
es la fuente de la mayoría de los ports de red que fallan:

- `SOCKET` es `UINT_PTR` (64 bits en x64), no un `int`.
- `_open_osfhandle()` puede envolver un `SOCKET`, pero **`_read()` y `_write()`
  sobre ese fd no funcionan**: el CRT termina llamando `ReadFile`/`WriteFile`
  sobre un handle de socket. Hay que usar `recv`/`send` (`WSASend`/`WSARecv`).
- `select()` de Winsock sólo acepta `SOCKET`. No acepta pipes, archivos ni
  consola.

Para que una herramienta POSIX siga usando `int fd`, `read()`, `write()` y
`close()`, hace falta una **tabla de descriptores virtuales**:

```c
struct win32_fd {
    SOCKET sock;
    int    used;
    int    nonblock;
};
static struct win32_fd fds[4096];

int win32_read(int fd, void *buf, unsigned len)
{
    SOCKET s = win32_socket_for_fd(fd);
    if (s != INVALID_SOCKET) {
        int n = recv(s, buf, (int)len, 0);
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            errno = EAGAIN;
            return -1;
        }
        return n;
    }
    return _read(fd, buf, len);   /* pipe/archivo CRT */
}
```

`close()` debe hacer `closesocket()` para un fd virtual y `_close()` para un
fd CRT. Nunca las dos cosas sobre el mismo handle.

## 5. `socketpair()` sin `socketpair()`

Windows no tiene `socketpair(AF_UNIX, ...)`. rsync necesita un par
**full-duplex, seleccionable y no bloqueante**. La emulación más simple es un
par de sockets TCP sobre loopback:

```c
int win32_socketpair(int sv[2])
{
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    int alen = sizeof addr;
    SOCKET a, b;

    bind(listener, (struct sockaddr *)&addr, sizeof addr);
    listen(listener, 1);
    getsockname(listener, (struct sockaddr *)&addr, &alen);

    a = socket(AF_INET, SOCK_STREAM, 0);
    connect(a, (struct sockaddr *)&addr, sizeof addr);
    b = accept(listener, NULL, NULL);
    closesocket(listener);

    sv[0] = win32_alloc_socket_fd(a);
    sv[1] = win32_alloc_socket_fd(b);
    return 0;
}
```

Ventajas: `poll`/`WSAPoll` funcionan, `shutdown` funciona y el par es
simétrico. Desventaja: consume puertos efímeros; en un bucle muy intensivo hay
que cuidar el `TIME_WAIT`.

## 6. `poll()` sobre `WSAPoll`: los bits no son los mismos

`WSAPoll` existe desde Windows Vista, pero **no acepta todos los bits POSIX**.
En un caso real, `poll()` con `POLLIN | POLLPRI` devolvía `-1` con
`WSAEINVAL`. La traducción correcta es:

```c
short ev = 0;
if (fds[i].events & (POLLIN | POLLPRI))
    ev |= POLLRDNORM;
if (fds[i].events & POLLOUT)
    ev |= POLLWRNORM;

WSAPOLLFD p = { .fd = sock, .events = ev, .revents = 0 };
int wr = WSAPoll(&p, 1, 0);

if (wr > 0) {
    short re = 0;
    if (p.revents & (POLLRDNORM | POLLRDBAND)) re |= POLLIN;
    if (p.revents & POLLWRNORM)               re |= POLLOUT;
    if (p.revents & POLLERR)                  re |= POLLERR;
    if (p.revents & POLLHUP)                  re |= POLLHUP;
    if (p.revents & POLLNVAL)                 re |= POLLNVAL;
    fds[i].revents = re;
}
```

Además:

- `WSAPoll` sólo sirve para sockets. Para **pipes CRT** usá
  `PeekNamedPipe()` (lectura) y `WaitForMultipleObjects()` (múltiples handles);
  para sockets + handles mezclados, `WSAEventSelect()` + `WaitForMultipleObjects`
  o un pump thread.
- Un `poll()` con timeout negativo significa “infinito”; no lo pases directo a
  `WSAPoll` sin normalizar.
- `POLLHUP`/`POLLERR` de Winsock no tienen los mismos valores numéricos que
  POSIX; por eso hay que traducir, no copiar.

## 7. `CreateProcess` + pipes + pump threads (transporte SSH)

Para lanzar `ssh.exe` (o cualquier remote shell) y hablar el protocolo de rsync
por stdin/stdout:

1. Crear dos pipes: `child_stdin` (padre escribe, hijo lee) y `child_stdout`
   (hijo escribe, padre lee).
2. Marcar como heredables **sólo** los extremos del hijo
   (`child_in_read`, `child_out_write`); los del padre con
   `SetHandleInformation(..., HANDLE_FLAG_INHERIT, 0)`.
3. `CreateProcessW` con `STARTF_USESTDHANDLES`, `bInheritHandles=TRUE`, y
   `hStdInput/hStdOutput/hStdError`.
4. Cerrar en el padre los extremos del hijo.
5. Crear un `socketpair` para el padre y dos threads:
   - `child_stdout -> socketpair[1]`
   - `socketpair[0] -> child_stdin`

Así rsync ve sólo sockets no bloqueantes y su I/O basada en `poll` sigue
funcionando sin cambios.

Puntos finos:

- `CreateProcessW` recibe **una sola línea de comandos**. Hay que citar cada
  `argv` con las reglas de `CommandLineToArgvW` (backslashes antes de comillas,
  comillas internas, argumento vacío). No alcanza con `"arg1 arg2"`.
- El hijo hereda `stderr` del padre; los prompts de password/host key de
  `ssh.exe` van a la consola, no al pipe de protocolo.
- `CreateProcessA` usa la code page ANSI; si querés Unicode, construí la línea
  con `CreateProcessW` y convertí desde `CP_ACP` o `CP_UTF8` según el origen.
- Guardá el `HANDLE` del proceso y usalo en `waitpid()` (`WaitForSingleObject`
  + `GetExitCodeProcess`). `WIFEXITED`/`WEXITSTATUS` se emulan con el código de
  salida.

## 8. `fork` no existe: qué se puede hacer y qué no

- **Copia local**: se puede evitar `fork` lanzando otra instancia del mismo
  ejecutable con `--server` y pipes. El hijo re-parsea argumentos, no necesita
  memoria compartida.
- **Descarga remota**: `do_recv()` hace `fork` **después** de recibir el
  file-list, y el hijo necesita esa lista en memoria. Ahí no alcanza con
  re-ejecutar: hay que hacer un split por threads con contexto explícito, o
  serializar el file-list a un proceso helper.
- **Daemon**: `fork` por conexión; se puede hacer thread-per-connection, pero
  hay que aislar todo el estado global por conexión.

No intentes `RtlCloneUserProcess` para producción: es no documentado y frágil.
