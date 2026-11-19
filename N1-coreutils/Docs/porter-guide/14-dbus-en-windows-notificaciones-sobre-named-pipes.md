# Módulo 14: D-Bus en Windows — notificaciones sobre named pipes

Los módulos 11 y 12 cubren procesos, pipes anónimos y sockets entre dos
procesos que se conocen. Los demonios de notificaciones de Linux (`dunst`,
`tiramisu`, `mako`) usan otra cosa: **un bus de mensajes** donde un proceso
*reclama un nombre* (`org.freedesktop.Notifications`) y cualquier otro le
habla por ese nombre. Windows no tiene bus de sesión. Este módulo explica cómo
se reemplazó en los ports de `N4-notifications` sin tocar la semántica.

Código de referencia: [`N4-notifications/wnotify`](../../../N4-notifications/wnotify/).

---

## 1. Qué aporta D-Bus y qué hay que conservar

La [Desktop Notifications Specification](https://specifications.freedesktop.org/notification-spec/latest/)
define cuatro métodos y dos señales:

| Pieza | Firma | Uso |
|---|---|---|
| `Notify` | `(susssasa{sv}i) → u` | app, replaces_id, icon, summary, body, actions, hints, timeout → id |
| `CloseNotification` | `(u)` | cerrar por id |
| `GetCapabilities` | `() → as` | qué soporta el daemon |
| `GetServerInformation` | `() → ssss` | nombre, vendor, versión, versión de spec |
| `NotificationClosed` | señal `(uu)` | id, motivo 1..4 |
| `ActionInvoked` | señal `(us)` | id, clave de acción |

Lo que D-Bus da **gratis** y hay que reconstruir:

1. **Nombre único por sesión.** Sólo un daemon a la vez; el segundo recibe
   *name lost* y termina.
2. **Descubrimiento.** El cliente no necesita saber el PID del daemon.
3. **Tipado.** `a{sv}` lleva valores con tipo (`y` byte para urgency, `i` para
   progress, `(iiibiiay)` para imágenes).
4. **Señales broadcast** a quien se haya suscrito (match rules).
5. **Identidad del emisor** (unique name, y vía el bus, PID/UID).

Lo que **no** hace falta: introspección, activación por `.service`, el bus de
sistema, ni compatibilidad binaria con el protocolo D-Bus.

> Alternativa evaluada: compilar `dbus-daemon` con vcpkg y usar GDBus tal
> cual (lo que hace KDE en Windows). Funciona, pero arrastra un daemon extra,
> autolaunch y un transporte TCP. Para un sistema que *sólo* necesita esta
> interfaz, un transporte propio de 600 líneas es más chico que la dependencia.

---

## 2. El endpoint: un named pipe por sesión de logon

```c
DWORD sid = 0;
ProcessIdToSessionId(GetCurrentProcessId(), &sid);
swprintf(name, L"\\\\.\\pipe\\org.freedesktop.Notifications.%lu", sid);
```

* El namespace `\\.\pipe\` es **global a la máquina**. Sin el SessionId, dos
  usuarios por RDP se pisarían. Es el equivalente de “bus de sesión”.
* `WNOTIFY_PIPE` reemplaza el nombre completo: tests aislados y
  **encadenamiento** (un daemon que reenvía a otro, sección 6).

### Ser dueño del nombre: `FILE_FLAG_FIRST_PIPE_INSTANCE`

```c
HANDLE h = CreateNamedPipeW(name,
    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
    PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, NULL);
if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED)
    /* otro daemon ya tiene el nombre: g_bus_own_name(...) -> name_lost */
```

Sin ese flag, el segundo daemon crea *otra instancia del mismo pipe* y los
clientes se reparten al azar entre los dos. Es el error más fácil de cometer.
`PIPE_REJECT_REMOTE_CLIENTS` evita que el pipe sea alcanzable por SMB.

### Seguridad

La DACL por defecto de un named pipe da control total a SYSTEM,
administradores y al creador, y **lectura** a Everyone. Otro usuario puede
abrirlo sólo para leer y no puede enviar `Notify`. Si eso no alcanza: SDDL
explícito (`D:P(A;;GA;;;OW)(A;;GA;;;SY)`), ver módulo 13.

---

## 3. El protocolo: una línea por mensaje

```
Notify\tfirefox\t0\t\tDescarga completa\tfoto.png\t2\tdefault\tAbrir\t1\turgency\ty\t1\t-1
OK\t7
```

* UTF-8, campos separados por TAB, un mensaje por `\n`.
* Escapes **dentro** de cada campo: `\\ \t \n \r`. Nada más.
* `actions` viaja aplanado como en D-Bus (`key, label, key, label`) con un
  contador delante; `hints` como tripletas `nombre, tipo, valor` donde el tipo
  es **el carácter de la firma D-Bus** (`s b y n q i u x t d`) o `image`.

¿Por qué texto y no binario/JSON? Tres consumidores muy distintos (C sin
dependencias, Odin, PowerShell con `NamedPipeClientStream`) lo parsean en
diez líneas, y se puede depurar con un cliente de pipe y los ojos.

El escape tiene que ser **reversible y local al campo**: un body con `\t`
literal no puede partir la línea. Probá siempre con `"a\tb\nc\\d"`.

---

## 4. Concurrencia: overlapped I/O o deadlock

Un servidor con un hilo por cliente parece trivial: `ConnectNamedPipe`,
`CreateThread`, `ReadFile` en bucle. Hasta que aparecen las **señales**: el
hilo que procesa `CloseNotification` del cliente A tiene que escribir
`NotificationClosed` en el pipe del cliente B, que está suscrito y bloqueado
en su propio `ReadFile`.

> **Windows serializa la I/O sincrónica por handle.** Si un hilo está dentro
> de `ReadFile(h)` sobre un handle sin `FILE_FLAG_OVERLAPPED`, un `WriteFile(h)`
> desde otro hilo espera a que la lectura termine.

Un suscriptor nunca escribe, así que la lectura nunca termina y la señal
nunca sale. El port Odin de tiramisu tuvo exactamente este bug (`notify-send
-w` se colgaba para siempre); el port C no, porque usaba overlapped desde el
principio.

```c
static bool ov_io(wn_conn *c, bool write, void *buf, DWORD n, DWORD *done)
{
    OVERLAPPED ov = { .hEvent = write ? c->wr_ev : c->rd_ev };
    BOOL ok = write ? WriteFile(c->pipe, buf, n, NULL, &ov)
                    : ReadFile (c->pipe, buf, n, NULL, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING)
        return false;
    if (!write) {                           /* la lectura también se corta al apagar */
        HANDLE w[2] = { ov.hEvent, c->srv->stop_ev };
        if (WaitForMultipleObjects(2, w, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(c->pipe, &ov);
            GetOverlappedResult(c->pipe, &ov, done, TRUE);
            return false;
        }
    }
    return GetOverlappedResult(c->pipe, &ov, done, TRUE);
}
```

Reglas:

* **Un evento por dirección** (`rd_ev`, `wr_ev`): lectura y escritura pueden
  estar en vuelo a la vez sobre el mismo handle.
* Escrituras de distintos hilos al mismo cliente: **un lock por conexión**,
  si no las líneas se intercalan.
* El cliente (`notify-send`) puede seguir siendo sincrónico: es un solo hilo
  que escribe y después lee. Para `--wait` abre **una segunda conexión** para
  las señales, y se suscribe *antes* de mandar `Notify` (si no, un cierre
  rápido se pierde; libnotify hace lo mismo con la match rule).

### El hilo de cada request no es el hilo del programa

GDBus despacha los métodos en el hilo del `GMainLoop`. El servidor de pipes
los despacha en el hilo de cada cliente. Para tiramisu alcanza con un
`CRITICAL_SECTION` alrededor del contador de ids y de `stdout`. Para dunst,
cuyo estado (colas, ventana, settings) es de un solo hilo, cada request se
**marshalla al main loop** y el hilo del cliente espera la respuesta (módulo 17).

---

## 5. Identidad del emisor

D-Bus le da al daemon el *unique name* del emisor. El equivalente en pipes:

```c
ULONG pid;
GetNamedPipeClientProcessId(pipe, &pid);
HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
QueryFullProcessImageNameW(p, 0, exe, &len);
```

`notifyd` guarda el ejecutable en cada evento: en el historial se ve *quién*
mandó cada notificación, no sólo el `app_name` que el emisor declaró.

---

## 6. Encadenar daemons

Con el nombre configurable por entorno, un daemon puede ser cliente de otro:

```
apps ──► \\.\pipe\org.freedesktop.Notifications.1 ──► notifyd (SQLite)
                                                         │ Notify (ids traducidos)
                                                         ▼
                                       \\.\pipe\dunst-down ──► dunst (pantalla)
                                                         ▲
                     NotificationClosed / ActionInvoked ─┘ (Subscribe, ids de vuelta)
```

El proxy tiene que **traducir ids en las dos direcciones** (`replaces_id` de
ida, `NotificationClosed(id)` de vuelta), y reconectar si el daemon de abajo
se reinicia. Ver `notifyd --forward`.

---

## 7. Checklist

- [ ] ¿El nombre incluye el SessionId (o viene de `WNOTIFY_PIPE`)?
- [ ] ¿La primera instancia usa `FILE_FLAG_FIRST_PIPE_INSTANCE`?
- [ ] ¿Los handles del servidor son overlapped si otro hilo escribe en ellos?
- [ ] ¿Hay un lock de escritura por conexión?
- [ ] ¿El cliente se suscribe antes de mandar `Notify` cuando va a esperar?
- [ ] ¿`CloseNotification` emite `NotificationClosed(id, 3)` aunque el daemon no muestre nada?
- [ ] ¿El escape se probó con TAB, `\n`, `\r` y `\` dentro de los campos?
- [ ] ¿El segundo daemon falla rápido y con un mensaje claro?
