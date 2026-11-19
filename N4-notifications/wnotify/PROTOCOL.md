# wnotify — `org.freedesktop.Notifications` sobre un named pipe de Windows

Windows no tiene bus de sesión D-Bus. `wnotify` conserva **la semántica** de la
[Desktop Notifications Specification 1.2](https://specifications.freedesktop.org/notification-spec/latest/)
(mismos métodos, mismos argumentos, mismas señales, mismos hints) y cambia **el
transporte**: un named pipe por sesión de logon y un protocolo de texto de una
línea por mensaje.

Todos los ports (`herbe` no lo necesita; `tiramisu` C y Odin, `dunst`,
`notify-send`, el historial SQLite) hablan exactamente esto, así que cualquier
cliente sirve con cualquier daemon — igual que en Linux cualquier `notify-send`
sirve con cualquier daemon.

## Endpoint

```
\\.\pipe\org.freedesktop.Notifications.<SessionId>
```

* `<SessionId>` = `ProcessIdToSessionId(GetCurrentProcessId())`. Dos usuarios por
  RDP no se pisan (equivalente a "un bus de sesión por login").
* `WNOTIFY_PIPE` (variable de entorno) lo reemplaza completo: tests, o encadenar
  daemons (un proxy que guarda en SQLite y reenvía a otro pipe).
* El servidor crea la primera instancia con `FILE_FLAG_FIRST_PIPE_INSTANCE`: si
  ya hay un daemon, falla con `ERROR_ACCESS_DENIED`. Es el equivalente de
  `g_bus_own_name(..., DO_NOT_QUEUE)` → "name lost".
* `PIPE_REJECT_REMOTE_CLIENTS`: nada de SMB.

## Framing

* UTF-8, un mensaje por línea, terminado en `\n` (se tolera `\r\n`).
* Campos separados por TAB.
* Escapes dentro de un campo: `\\` `\t` `\n` `\r`. Nada más.
* Una conexión puede hacer varias llamadas seguidas (request → reply).
* Respuesta: `OK[\t campo]*` o `ERR\t<mensaje>`.

## Métodos

| D-Bus | Request | Reply |
|---|---|---|
| `GetServerInformation` | `GetServerInformation` | `OK\tname\tvendor\tversion\tspec_version` |
| `GetCapabilities` | `GetCapabilities` | `OK\tcap1\tcap2...` |
| `Notify` | ver abajo | `OK\t<id>` |
| `CloseNotification(id)` | `CloseNotification\t<id>` | `OK` |
| (match rule sobre señales) | `Subscribe` | `OK`, y desde ahí el server empuja señales |

### Notify

```
Notify  app_name  replaces_id  app_icon  summary  body
        N  action_key_1  action_label_1 ... (N strings, N par)
        H  hint_name_1  hint_type_1  hint_value_1 ... (H tripletas)
        expire_timeout
```

Es el `(susssasa{sv}i)` de D-Bus aplanado. `actions` viaja como en D-Bus: lista
plana `key, label, key, label`.

### Tipos de hint

El tipo es el carácter de la firma D-Bus del `variant`:

| tipo | D-Bus | valor |
|---|---|---|
| `s` | string | texto |
| `b` | boolean | `true`/`false` |
| `y` | byte | decimal 0..255 |
| `n` `q` | int16 / uint16 | decimal |
| `i` `u` | int32 / uint32 | decimal |
| `x` `t` | int64 / uint64 | decimal |
| `d` | double | `%.17g` |
| `image` | `(iiibiiay)` | `w:h:rowstride:alpha:bits_per_sample:channels:<base64>` |

`image` usa la misma representación que ya imprimía tiramisu, así que la salida
de tiramisu y el protocolo coinciden.

## Señales (`Subscribe`)

```
NotificationClosed  <id>  <reason>     reason: 1 expired, 2 dismissed, 3 CloseNotification, 4 undefined
ActionInvoked       <id>  <action_key>
ActivationToken     <id>  <token>
```

El suscriptor deja la conexión abierta; el server escribe una línea por señal.
Cerrar el pipe es desuscribirse.

## Ejemplo

```
> Notify\tfirefox\t0\t\tDescarga completa\tfoto.png\t2\tdefault\tAbrir\t2\turgency\ty\t1\tdesktop-entry\ts\tfirefox\t-1
< OK\t7
> Subscribe
< OK
< ActionInvoked\t7\tdefault
< NotificationClosed\t7\t2
```

## Seguridad

DACL por defecto de un named pipe: control total para SYSTEM, administradores y
el creador; **lectura** para Everyone. Un proceso de otro usuario puede abrir el
pipe sólo para leer, por lo que no puede enviar `Notify`. Si eso no alcanza, el
siguiente paso es un SDDL explícito (`D:P(A;;GA;;;OW)(A;;GA;;;SY)`), ver módulo
13 de la guía de porteo.
