# Módulo 20: Demonios sin `fork()`, sockets AF_UNIX y sesiones

dtach y abduco separan **cliente** (lo que ves) y **servidor** (lo que sobrevive
cuando cerrás la terminal), conectados por un socket Unix en el filesystem.
Este módulo cubre las tres piezas que eso exige en Windows: crear el demonio
sin `fork()`, el socket AF_UNIX de Winsock, y los metadatos que los programas
guardan *en el propio archivo del socket*. Código: `N5-multiplexers/compat/src/proc.c`,
`net.c`, `fs.c`.

---

## 1. Demonizar sin `fork()`

abduco usa el "doble fork" clásico:

```c
pipe(client_pipe);
server.socket = server_create_socket(name);  /* el CLIENTE crea el socket */
if (fork() == 0) {            /* hijo */
    setsid();
    if (fork() == 0) {        /* nieto = servidor */
        forkpty(...);         /* + execvp del programa */
        /* error -> write(client_pipe[1], msg); EOF = éxito (FD_CLOEXEC) */
        server_mainloop();
    }
    _exit(0);
}
read(client_pipe[0], ...);    /* el cliente espera: mensaje de error o EOF */
```

Lo que importa de ese patrón no es `fork`, es el **protocolo**:

1. el servidor arranca **desacoplado** de la terminal del cliente;
2. hereda **todo el estado ya parseado** (opciones, nombre, tamaño de ventana);
3. un **pipe de estado** devuelve un error legible o EOF cuando está listo.

Traducción (patrón "re-ejecutar el mismo binario" del módulo 11):

```c
/* cliente */
setenv("ABDUCO_WIN32_WINSIZE", "120x40", 1);           /* estado extra */
pid = compat_spawn_self_detached("abduco-server", argv, &status_rd);
len = read_all(status_rd, errormsg, sizeof errormsg);  /* error o EOF */

/* el mismo .exe, al arrancar */
if (compat_daemon_role())          /* COMPAT_DAEMON_ROLE en el entorno */
    win32_server_main(...);        /* re-parsea argv, crea socket, ConPTY,
                                      close(statusfd) = "estoy listo" */
```

Diferencia de diseño respecto de Unix: **el socket lo crea el servidor**, no el
cliente, porque un socket AF_UNIX no se puede pasar entre procesos de forma
fiable. Sus errores (dirección en uso, ruta inválida) viajan por el pipe de estado.

### Detalles de `compat_spawn_self_detached()`

| Necesidad | Mecanismo |
| :--- | :--- |
| sin consola | `DETACHED_PROCESS \| CREATE_NEW_PROCESS_GROUP` |
| heredar **sólo** el pipe | `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` (el `FD_CLOEXEC` de Windows al revés: lista blanca) |
| pasar el handle | su valor en hex en `COMPAT_STATUS_HANDLE` |
| que el rol no se propague | la capa **borra** `COMPAT_DAEMON_ROLE`/`COMPAT_STATUS_HANDLE` al arrancar; si no, un `dtach` corriendo *dentro* de la sesión creería ser el demonio |
| sobrevivir al cierre de la pestaña | `CREATE_BREAKAWAY_FROM_JOB` (ver abajo) |
| stdio válido | handles de `NUL` heredables como stdin/stdout/stderr (ver trampa) |

### Trampa: Jobs con `KILL_ON_JOB_CLOSE`

Windows Terminal, conhost y `sshd` meten a sus hijos en un *Job object* con
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Cerrás la pestaña y muere todo el Job,
**incluido tu "demonio"**: el equivalente de que `setsid()` no funcione. Se pide
`CREATE_BREAKAWAY_FROM_JOB`; si el Job no lo permite, `CreateProcess` falla con
`ERROR_ACCESS_DENIED` y se reintenta sin la bandera (la sesión vive entonces lo
que viva la terminal).

### Trampa: el demonio sin stdio pierde sus mensajes

Primera versión: `STARTF_USESTDHANDLES` con los tres handles en `NULL`. El
mensaje "could not execute foo.exe" nunca llegaba al cliente. Con
`COMPAT_TRACE` se vio:

```text
[1520] init: fileno in/out/err = -2 -2 -2
```

Sin handles estándar, el UCRT ata `stdout` al pseudo-descriptor **-2**, no al fd 1.
`dup2(statusfd, 1)` cambia el fd 1, pero `printf()` sigue escribiendo en -2.
Intentar arreglarlo dentro del hijo con `freopen("NUL", "w", stdout)` tampoco
sirve: `freopen` toma el fd libre más bajo (fue el **4**). La corrección es de
origen: el padre le pasa `NUL` como stdio; el CRT del hijo arranca con 0/1/2 reales.

### Trampa: `_isatty(NUL)` es verdadero

`_isatty()` del UCRT responde "¿es un dispositivo de caracteres?", y `NUL` lo
es. Un demonio con stdio en `NUL` creería tener terminal. `isatty()` se
implementa con `GetConsoleMode()`, que sólo funciona sobre una consola real.

## 2. AF_UNIX en Winsock

Desde Windows 10 1803: `#include <afunix.h>`, `socket(AF_UNIX, SOCK_STREAM, 0)`,
`bind/listen/accept/connect` sobre una **ruta del filesystem**, y `select()`.
**No** hay: `socketpair()`, `SOCK_DGRAM`, nombres abstractos, `SCM_RIGHTS`,
`SO_PEERCRED`. Para dtach y abduco alcanza.

| Situación | Unix | Windows |
| :--- | :--- | :--- |
| el archivo del socket | inodo `S_IFSOCK` | *reparse point* `IO_REPARSE_TAG_AF_UNIX` (0x80000023) |
| `stat()` sobre él | funciona | el `stat()` del UCRT sigue el reparse point y **falla** |
| `unlink()` | borra | `DeleteFileW` borra el reparse point |
| `bind()` si existe | `EADDRINUSE` | idem |
| conectar a ruta inexistente | `ENOENT` | código WSA variable → la capa verifica la ruta y devuelve `ENOENT` (dtach `-A` decide "crear" con eso) |
| `socketpair()` | sí | no → TCP loopback `127.0.0.1:0` |
| `sun_path` | 108 bytes | 108 bytes; `/` y `\` aceptados |

### Trampa: cerrar con datos sin leer = RST

En Linux, cerrar un socket Unix con datos sin leer en **tu** cola de recepción
no le hace nada al par: él lee lo que tenía y después EOF. Winsock implementa
AF_UNIX con semántica de TCP: ese cierre es **abortivo**, el par recibe RST y
**pierde lo que todavía no había leído**.

Síntoma real: `abduco -a` de una sesión cuyo comando ya terminó mostraba
"exit status 5" pero el servidor nunca salía. Con `cdb -p <pid> -c "~*k"` el
hilo principal estaba en `select()`. Secuencia: el servidor manda `MSG_EXIT` en
cada vuelta; el cliente lo recibe, devuelve su `MSG_EXIT` y cierra con otro
`MSG_EXIT` aún sin leer → RST → el eco se pierde → el servidor espera para
siempre a un cliente que ya se fue.

Corrección en `close()` de sockets: `shutdown(SD_SEND)`, **vaciar** la cola de
recepción en modo no bloqueante, recién entonces `closesocket()`.

Variante del mismo problema al **terminar el proceso**: el sistema cierra los
sockets abortivamente. dtach mostraba `[read returned an error]` en vez de
`[EOF - dtach terminating]`. Un `atexit()` registrado por la capa *antes* que
los del programa (por lo tanto corre *después*) cierra todos los sockets con el
mismo procedimiento.

## 3. Metadatos en el archivo del socket

dtach y abduco usan los **bits de permiso del socket** como una base de datos
diminuta que cualquiera puede consultar con `stat()`:

| Bit | Significado |
| :--- | :--- |
| `S_IXUSR` | hay un cliente adjunto (abduco lista `*`) |
| `S_IXGRP` | el comando ya terminó (abduco lista `+`) |

NTFS no tiene bits Unix. `chmod()` del UCRT sólo alterna *read-only* (y un
socket de sólo lectura no se puede borrar). Se guardan en dos **atributos de
archivo inocuos del propio reparse point**:

```text
S_IXUSR  <->  FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
S_IXGRP  <->  FILE_ATTRIBUTE_HIDDEN
```

`stat()` propio: `GetFileAttributesExW` + `FindFirstFileW` (su `dwReserved0`
trae el tag de reparse) → `S_IFSOCK | 0600 | bits`. Es atómico, no deja archivos
auxiliares y lo ve cualquier proceso.

## 4. El directorio de sesiones y la propiedad

abduco rechaza el directorio de sockets si `st_uid != getuid()` o si tiene
permisos de grupo/otros. Con el `stat()` del UCRT falla siempre: copia los bits
de usuario a grupo y otros (`0755` en todo directorio) y `st_uid` es 0.

- `st_mode`: sólo bits de usuario (`0700` para directorios).
- `st_uid`: **real**: se compara el SID dueño (`GetNamedSecurityInfoW`) con el
  del token (`TokenUser`, o `TokenOwner` si un admin elevado lo creó). Si es
  tuyo, `getuid()`; si no, `-2`.
- `getuid()`: el RID del SID del usuario (`S-1-5-21-...-1001` → 1001).
- Ubicación: `HOME` casi nunca existe fuera de Git Bash/MSYS. Se agregaron
  `USERPROFILE` y `LOCALAPPDATA` a la lista de abduco; un directorio dentro del
  perfil ya hereda una ACL privada.

## 5. Checklist de demonios y sesiones

- [ ] ¿El rol del proceso re-ejecutado viaja por el entorno y se borra al arrancar?
- [ ] ¿Sólo el pipe de estado (y `NUL`) se heredan, vía `HANDLE_LIST`?
- [ ] ¿El demonio tiene stdio válido (`NUL`), no `NULL`?
- [ ] ¿`CREATE_BREAKAWAY_FROM_JOB` con reintento?
- [ ] ¿`stat()` reconoce el reparse point AF_UNIX?
- [ ] ¿Los bits que el programa guarda en el socket sobreviven (`chmod` → atributos)?
- [ ] ¿`close()` y `exit()` cierran sockets sin RST?
- [ ] ¿La verificación de propiedad compara SIDs de verdad?
