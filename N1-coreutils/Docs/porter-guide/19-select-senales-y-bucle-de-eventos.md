# Módulo 19: `select()`, señales y el bucle de eventos

Todo multiplexor tiene el mismo corazón:

```c
for (;;) {
    FD_ZERO(&rd);
    FD_SET(STDIN_FILENO, &rd);          /* teclado        */
    FD_SET(server_socket, &rd);         /* clientes       */
    FD_SET(pty_master, &rd);            /* el programa    */
    if (pselect(nfds, &rd, NULL, NULL, NULL, &emptyset) < 0 && errno == EINTR)
        continue;                       /* llegó SIGWINCH/SIGCHLD */
    ...
}
```

En Windows **ninguno de esos tres es seleccionable por el mismo mecanismo** y
**no hay señales asíncronas**. Este módulo describe cómo `N5-multiplexers/compat`
(`fd.c`, `select.c`, `signal.c`, `console.c`) reconstruye ese contrato.

---

## 1. Un solo espacio de `int` para sockets y fds del CRT

Un `SOCKET` es un `UINT_PTR` (handle de kernel), no un fd del CRT: `_read()`
sobre él falla. El código POSIX mezcla todo detrás de `int fd`: `read`, `write`,
`close`, `fcntl`, `FD_SET`.

Técnica usada: **reservar un fd del CRT por cada socket**.

```c
int fd = _open("NUL", _O_RDWR | _O_BINARY | _O_NOINHERIT); /* reserva el número */
vfds[fd] = (struct vfd){ .s = sock };                      /* tabla lateral */
```

- El número es chico (el bitmap `fd_set` de POSIX funciona) y el CRT nunca lo
  reparte dos veces.
- `read/write/close/fcntl/ioctl` miran la tabla: socket → `recv/send/closesocket/ioctlsocket`; si no, `_read/_write/...`.
- `fd_set` es **el bitmap POSIX**, no el arreglo de `SOCKET` de Winsock: así
  `nfds`, `FD_SET(0)` y `FD_ISSET(fd)` conservan su significado y la
  traducción ocurre dentro de `compat_select()`.

Alternativa descartada: guardar el valor del `SOCKET` casteado a `int` y usar
el `fd_set` de Winsock. Funciona hasta que un handle colisiona con un fd del
CRT o no entra en `int`, y obliga a incluir `winsock2.h` en el código upstream.

## 2. El cortafuegos de cabeceras

Para que `struct sockaddr`, `fd_set` o `struct timeval` existan sin incluir
`winsock2.h`, la capa tiene **dos mundos**:

```text
upstream .c ──/FI──> compat.h ──> compat_types.h   (sin windows.h)
compat/src/*.c ────> compat_internal.h ──> winsock2.h + windows.h + compat_types.h
```

- `compat_types.h` define sólo nombres POSIX que **no chocan** con Win32
  (`struct termios`, `struct winsize`, `sigset_t`...) y pasa `sockaddr` como
  `const void *`.
- Las cabeceras sombra (`sys/socket.h`, `sys/un.h`, `sys/time.h`) definen
  `struct sockaddr`, `sockaddr_un`, `timeval` con **el mismo layout** que
  Winsock.
- Beneficio concreto: PDCurses define `KEY_EVENT` y `MOUSE_MOVED`, que
  `wincon.h` también define. Si `windows.h` entrara en `dvtm.c`, chocarían.

Reglas de `compat.h` (forzada con `/FI`):

1. incluir **primero** todas las cabeceras del CRT, **después** definir macros;
2. sólo macros **con paréntesis** para funciones (`#define read(f,b,n) ...`):
   `struct stat sb;` y `struct sigaction sa;` sobreviven, `stat(p,&sb)` se redirige.

## 3. `select()` sobre objetos heterogéneos

Denominador común de Windows: `WaitForMultipleObjects`.

| Fuente | Objeto esperable |
| :--- | :--- |
| sockets (incluido el master de la pty) | `WSAEventSelect(s, ev, FD_READ\|FD_ACCEPT\|FD_CLOSE\|...)` → un `WSAEVENT` |
| teclado (`fd 0` consola) | el handle de entrada de consola (señalado = cola no vacía) |
| "llegó una señal" | evento manual de `signal.c` |
| pipes anónimos | **ninguno** → sondeo cada 100 ms con `PeekNamedPipe` |

Algoritmo de `do_select()`:

1. Si hay señal pendiente → despacharla, `errno = EINTR`, devolver `-1`.
2. **Sondeo** con timeout cero: `select()` de Winsock sobre los sockets,
   `console_input_ready()` para el teclado, `PeekNamedPipe` para pipes.
3. Si algo está listo → devolver. El sondeo es la fuente de verdad; la espera
   es sólo una forma eficiente de dormir.
4. Armar `WSAEventSelect`, `WaitForMultipleObjects(..., min(timeout, 100 ms si hay consola o pipes))`, desarmar, volver a 1.

### Trampa: `WSAEventSelect` cambia el modo del socket

`WSAEventSelect` pone el socket en **no bloqueante** y cancelar la asociación
(`WSAEventSelect(s, NULL, 0)`) **no lo vuelve a bloqueante**. Un `read()`
posterior que el programa espera bloqueante devuelve `EAGAIN`. La tabla de fds
guarda el modo que pidió el programa (`O_NONBLOCK` vía `fcntl`) y se restaura
con `ioctlsocket(FIONBIO)` después de cada espera.

### Trampa: el teclado "listo" que no tiene teclas

El handle de consola está señalado si la cola tiene **cualquier** evento: foco,
movimiento de mouse, *key-up*, redimensionado. Reportar "legible" con sólo
eso hace que `read(0)` bloquee (o que `getch()` bloquee dentro de curses)
mientras el programa ignora la salida de la pty. `console_input_ready()`:

- mira la cabeza de la cola con `PeekConsoleInputW`;
- descarta (`ReadConsoleInputW` exacto) el prefijo inútil: key-up, foco, menú,
  teclas modificadoras solas, mouse en modo bytes;
- convierte `WINDOW_BUFFER_SIZE_EVENT` en `SIGWINCH`;
- recién entonces dice "listo".

Hay **dos modos**: *bytes* (dtach/abduco hacen `read(0)` y la capa traduce
eventos de tecla a UTF-8) y *eventos* (dvtm usa PDCurses, que lee la cola él
mismo: `select()` no debe consumir clics ni teclas especiales). dvtm lo activa
con `compat_console_events_mode(1)`.

## 4. `termios` sobre la consola

| termios | modo de entrada de consola |
| :--- | :--- |
| `ICANON` | `ENABLE_LINE_INPUT` |
| `ECHO` | `ENABLE_ECHO_INPUT` (sólo válido con `LINE_INPUT`) |
| `ISIG` | `ENABLE_PROCESSED_INPUT` (Ctrl+C → evento de control) |
| crudo (flechas = `ESC [ A`) | `ENABLE_VIRTUAL_TERMINAL_INPUT` |
| `SIGWINCH` | `ENABLE_WINDOW_INPUT` |
| `OPOST`/`ONLCR` | salida: `!DISABLE_NEWLINE_AUTO_RETURN` |
| interpretar `ESC[...` | salida: `ENABLE_VIRTUAL_TERMINAL_PROCESSING` |

Detalles que importan:

- `struct termios` lleva dos campos extra con los **modos exactos** de los que
  se leyó. `tcsetattr(orig_term)` restaura bit a bit en vez de re-derivar.
- En modo crudo se fuerza `CP_UTF8` en entrada y salida y se restaura al volver.
- `c_cc[VSUSP] = 0xFF`: sin control de trabajos, el suspend se deshabilita.
  No `0` (`_POSIX_VDISABLE`): dtach compara el primer byte leído con `VSUSP` y
  Ctrl+@ produce un `NUL` real. `0xFF` nunca inicia una secuencia UTF-8.
- `TIOCGWINSZ` usa `CONOUT$`, no stdout: tiene que funcionar con stdout
  redirigido. Y devuelve la **ventana** (`srWindow`), no el buffer (9001 filas).
- Escritura a consola: UTF-8 → UTF-16 → `WriteConsoleW`, **guardando la cola
  incompleta** de una secuencia multibyte para la próxima llamada. La pty
  entrega trozos de 4 KiB arbitrarios; partir un `ñ` entre dos `write()` con
  `WriteFile` bajo cp65001 produce dos `U+FFFD`.

## 5. Señales: bits pendientes + un evento

Windows no entrega señales asíncronas a un hilo arbitrario; el `signal()` del
UCRT conoce siete; los manejadores de Ctrl+C corren **en un hilo nuevo**.

Modelo (el *self-pipe trick* invertido):

```text
cualquier hilo:  pending |= 1<<sig ; SetEvent(sigev)          (compat_signal_post)
hilo principal:  select() ve sigev o pending -> corre handlers -> EINTR
```

- Fuentes: hilo vigía de cada hijo (`SIGCHLD`), sondeo de tamaño de consola
  (`SIGWINCH`), `SetConsoleCtrlHandler` (`SIGINT`, `SIGQUIT`, `SIGHUP` al cerrar
  la ventana), `kill(getpid(), sig)`.
- `SIGSEGV/SIGILL/SIGFPE/SIGABRT` se dejan al CRT: son síncronas y deben correr
  en el hilo que falló.
- Acción por defecto: `SIGCHLD`, `SIGWINCH`, `SIGCONT` y las de *job control* se
  ignoran; el resto termina con `_exit(128+sig)` (como Unix: **sin** `atexit`).
- Es suficiente porque los tres programas bloquean siempre en `select()`. Un
  programa que bloquee en `read()` necesitaría otro punto de entrega.

### Trampa: `pselect()` y la entrega temprana

`pselect(..., &emptyset)` existe para cerrar la carrera "llega la señal entre
el chequeo de la bandera y el `select`". La implementación ingenua:

```c
sigprocmask(SIG_SETMASK, mask, &old);   /* ¡entrega SIGWINCH acá mismo! */
r = select(...);                        /* ...y duerme sin EINTR        */
```

`sigprocmask` debe entregar las pendientes desbloqueadas *antes de volver*
(POSIX). El handler corre, pone `need_resize = 1`, y `select()` se duerme hasta
la próxima tecla: el redimensionado de abduco se mandaba tarde. `pselect` debe
**intercambiar la máscara sin entregar** (`signal_mask_swap`) y dejar que el
propio `select` despache y devuelva `EINTR`.

## 6. `waitpid()` y el estado de salida

Windows tiene un código de salida de 32 bits y ningún "murió por señal". La
capa reconstruye `WIFSIGNALED`:

| Código de salida | Se informa como |
| :--- | :--- |
| `0xE0DE00nn` (nuestro `TerminateProcess` por `kill(pid, nn)`) | señal `nn` |
| `0xC000013A` `STATUS_CONTROL_C_EXIT` | `SIGINT` |
| `0xC0000005`, `0xC00000FD` | `SIGSEGV` |
| `0xC000001D` / `0xC0000094` | `SIGILL` / `SIGFPE` |
| cualquier otro | `WEXITSTATUS = code & 0xff` |

`kill()` hacia otro proceso sólo puede **terminarlo**: no existen señales
capturables entre procesos. `SIGWINCH` hacia un hijo con pty es "repintá"
(módulo 18); `SIGTSTP`/`SIGCONT` son no-ops.

## 7. `errno`: valores que Linux iguala y el UCRT no

En Linux `EWOULDBLOCK == EAGAIN`. En el UCRT valen 140 y 11. abduco pregunta
primero por `EWOULDBLOCK` y dtach sólo por `EAGAIN`: con valores distintos uno
de los dos interpreta mal cada lectura no bloqueante. `compat.h` hace
`#define EWOULDBLOCK EAGAIN` para el código upstream (no para `compat/src`).

## 8. Checklist del bucle de eventos

- [ ] ¿Sockets y fds comparten un único espacio de `int` sin colisiones?
- [ ] ¿`fd_set` es un bitmap POSIX y la traducción es interna?
- [ ] ¿`windows.h` está fuera de las unidades de traducción upstream?
- [ ] ¿Se restaura el modo bloqueante después de `WSAEventSelect`?
- [ ] ¿El teclado sólo cuenta como listo si hay una tecla útil en la cabeza de la cola?
- [ ] ¿Las señales se ejecutan en el hilo principal y `select()` devuelve `EINTR`?
- [ ] ¿`pselect` intercambia la máscara sin entregar?
- [ ] ¿`EWOULDBLOCK == EAGAIN`?
