# notifyd — notificaciones que quedan guardadas

Los daemons clásicos (dunst, mako, el centro de notificaciones de Windows)
tratan una notificación como algo **efímero**: aparece, expira, y como mucho
queda un historial en memoria que se pierde al reiniciar. `notifyd` invierte
la prioridad: **primero se escribe el evento en disco, después se muestra.**

```
notifyd [--db PATH] [--exec CMD | --forward PIPE] [--windows]
notifyd history [-n N] [--app APP] [--search QUERY] [--json]
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

## Búsqueda de texto completo (FTS5)

Si el SQLite enlazado trae FTS5, al arrancar el daemon crea
`notifications_fts` (tabla FTS5 *external content* sobre `summary`, `body`,
`app_name`, mantenida por triggers, tokenizer `unicode61 remove_diacritics 2`)
y la llena con `rebuild` si la base ya tenía filas. `--search` acepta la
sintaxis de FTS5:

```
notifyd history --search 'build*'                 # prefijo
notifyd history --search 'nandu'                  # encuentra "ñandú" (sin tildes)
notifyd history --search 'produccion AND listo'
notifyd history --search '"from Ana"'             # frase
notifyd history --search 'app_name:discord'       # una columna
```

Sin FTS5 cae a `LIKE` y lo avisa por stderr; una consulta FTS mal formada sale
con el error de SQLite y exit 1. Dos formas de tener FTS5:

```bat
:: SQLite compilado dentro del binario (sin sqlite3.dll), desde la amalgamation
set SQLITE_SRC=C:\ruta\sqlite-autoconf-3530300
notifyd\build.bat

:: o el de vcpkg, con la feature
vcpkg install sqlite3[core,json1,fts5]
```

## Notificaciones de Windows (`--windows`)

Windows guarda los toasts del Centro de actividades en su propia base SQLite,
legible por el usuario:

```
%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db
  Notification(Id, HandlerId, Type, Payload = XML <toast>, ArrivalTime FILETIME, ...)
  NotificationHandler(RecordId, PrimaryId = AUMID de la app)
```

Con `--windows`, un hilo abre esa base en sólo lectura cada ~2 s (o antes si
cambia el directorio), importa cada toast nuevo como una notificación más
(`app_name` derivado del AUMID, `summary` = primer `<text>`, `body` = el resto,
XML completo y AUMID en hints `x-windows-*`) y registra el mapeo en
`windows_toasts` **en la misma transacción**: un toast nunca se importa dos
veces ni queda a medias. Cuando un toast desaparece del Centro de actividades
se registra un cierre con motivo 4 (Windows no dice si fue el usuario o la
expiración).

Por qué así y no `UserNotificationListener`: la API oficial (WinRT) exige
identidad de paquete (MSIX o *sparse package*), y para un build local eso
implica firmar con un certificado e instalarlo como confiable en la máquina.
Leer el store no cambia nada del sistema. El costo: es un formato **no
documentado** (estable desde Windows 10 1607). Si cambia, `notifyd` lo reporta
("unexpected wpndatabase schema") y sigue funcionando para wnotify.

Las notificaciones directorio-WAL no son confiables: Windows no actualiza el
tamaño/mtime de un archivo abierto en el directorio en cada escritura, por eso
el poll de 2 s es lo que garantiza frescura y el aviso del directorio sólo lo
adelanta.

Verificado: al arrancar importó los toasts presentes; un toast nuevo emitido
con `ToastNotificationManager` apareció en `history` en menos de 3 s con UTF-8
correcto y `app_name` = `powershell`; al quitarlo del Centro de actividades
quedó cerrado con motivo 4.

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

* Acciones de los toasts de Windows: quedan en el XML guardado, pero
  invocarlas requiere activar la app dueña (activador COM o protocolo); no
  implementado.
* Retención/archivo (exportar a JSON lines por mes), UI de historial.

## dunst

El port de dunst (rama `port/windows` del fork) habla este mismo protocolo, así
que `notifyd --forward` funciona con dunst como pantalla. dunst tiene su propio
historial en memoria (`dunstctl history`); `notifyd` es el que sobrevive
reinicios.
