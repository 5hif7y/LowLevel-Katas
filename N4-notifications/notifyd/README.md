# notifyd — notificaciones que quedan guardadas

Los daemons clásicos (dunst, mako, el centro de notificaciones de Windows)
tratan una notificación como algo **efímero**: aparece, expira, y como mucho
queda un historial en memoria que se pierde al reiniciar. `notifyd` invierte
la prioridad: **primero se escribe el evento en disco, después se muestra.**

```
notifyd [--db PATH] [--exec CMD | --forward PIPE]
notifyd history [-n N] [--app APP] [--search TEXT] [--json]
notifyd show ID          # la vida completa de una notificación, evento por evento
notifyd events [-n N] [--since SEQ]
notifyd stats
notifyd tail             # eventos en vivo desde el daemon
```

Base por defecto: `%LOCALAPPDATA%\notifyd\history.sqlite3`.

## Modelo de datos: un log de eventos

[`schema.sql`](schema.sql) (se compila dentro del binario).

* **`events`** — append-only. Cada `Notify`, *replace*, `CloseNotification`,
  cierre (con motivo), acción invocada, arranque y parada del daemon es una
  fila con timestamp, PID y ejecutable del cliente, y el payload completo en
  JSON. Nunca se actualiza ni se borra: es la fuente de verdad.
* **`notifications`** — proyección: el estado actual de cada id (último
  texto, `revision` que sube con cada replace, motivo de cierre, acción).
  Se puede reconstruir desde `events`.
* **`images`** — los hints `image-data` se guardan una vez por buffer
  distinto (hash), no en cada evento.

Decisiones:

| Decisión | Por qué |
|---|---|
| Insert del evento y upsert de la proyección en **una transacción** | nunca hay una notificación sin su evento ni al revés |
| Se persiste **antes** de responder el id | si el daemon muere después del `OK`, la notificación ya está en disco |
| `journal_mode=WAL` | `notifyd history` lee mientras el daemon escribe, sin bloquearlo |
| `synchronous=FULL` | las notificaciones son pocas por segundo: vale el `fsync` por commit |
| ids nunca reutilizados | al arrancar, `next_id = MAX(id)+1`; un id identifica una notificación para siempre |
| `client_exe` del proceso real | `GetNamedPipeClientProcessId`: el `app_name` lo declara el emisor, el ejecutable no se puede falsear |

## Modos de pantalla

* **headless**: sólo registra (como tiramisu). `CloseNotification` cierra con motivo 3.
* **`--exec herbe.exe`**: un proceso por notificación con `app`, `summary`,
  `body` como líneas. El exit code de herbe se traduce: `0` → `ActionInvoked(id,
  "default")` + cierre 2; `2` → cierre 2 (dismiss/timeout; herbe no distingue).
  `CloseNotification` manda `WM_CLOSE` a las ventanas de ese proceso.
  Un *replace* cierra el popup anterior **sin** registrar un cierre.
* **`--forward PIPE`**: cliente de otro daemon wnotify (dunst, tiramisu)
  arrancado con `WNOTIFY_PIPE=PIPE`. Traduce ids en ambas direcciones, se
  suscribe a sus señales y reconecta si se reinicia.

## Verificado

Script de prueba (PowerShell) contra una base temporal:

1. headless: ids 1 y 2; replace de 2 → `revision 2`; `--close=1` → cerrada (3);
2. reinicio: el siguiente id es 3 (no se reutiliza);
3. `--exec herbe`: `herbe --action` → `notify-send -A` imprime `default`;
   evento `action` + `closed(2)`; `--close` → `closed(3)`, no quedan procesos herbe;
4. `--forward` a tiramisu: tiramisu imprime la notificación, el cierre vuelve
   como `NotificationClosed` y se registra;
5. `history`, `history --app build --json`, `show 2` (notify + replace), `stats`.

## Pendiente / ideas

* **FTS5** para `--search`: el `sqlite3` de vcpkg instalado no trae FTS5
  (`vcpkg install sqlite3[fts5]`). Hoy la búsqueda es `LIKE`.
* **Toasts nativos de Windows**: `UserNotificationListener` (WinRT) permite
  leer las notificaciones de *otras* apps de Windows con consentimiento del
  usuario; requiere identidad de paquete (MSIX o *sparse package*). Sería la
  forma de que el historial incluya también lo que no pasa por wnotify.
* Retención/archivo (exportar a JSON lines por mes), UI de historial.

## dunst

El port de dunst (rama `port/windows` del fork) usa este mismo protocolo, así
que `notifyd --forward` funciona con dunst como pantalla. dunst ya tiene su
propio historial en memoria (`dunstctl history`); `notifyd` es el que
sobrevive reinicios.
