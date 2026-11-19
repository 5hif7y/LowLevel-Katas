# Módulo 25: Notificaciones que no se olvidan — Portmaster sobre notifyd

En Portmaster una notificación es un record en memoria (`notifications:all/<EventID>`):
se crea, la UI la muestra, el usuario elige una acción, expira o se borra, y
desaparece. No queda nada. El objetivo de esta parte del kata: que **cada**
notificación quede guardada para siempre como historial de eventos, sin
reescribir un sistema de persistencia — reusando `notifyd` de
[N4](../../../N4-notifications/README.md).

Código: `odin/base/notifications/` (port + `system.odin`, el puente),
`odin/base/notifications/wnotify/` (cliente del protocolo en Odin puro),
`odin/tests/fakedisplay/`.

---

## 1. Por qué no una base propia

El port ya tiene base de datos (módulos 23–24). Pero notifyd ya resuelve lo
difícil y lo resuelve para **todas** las apps, no sólo Portmaster:

* log **append-only** (`events`): nada se actualiza ni se borra; un cambio es
  un evento nuevo;
* proyección por id (`notifications`) reconstruible desde el log;
* WAL + `synchronous=FULL`: un evento confirmado sobrevive a un corte de luz;
* FTS5 para buscar, imágenes deduplicadas, toasts de Windows importados;
* delegación de la pantalla (herbe, dunst, tiramisu) con `--exec`/`--forward`.

Portmaster pasa a ser un cliente wnotify más. Lo que tiene que resolver el
puente es **el mapeo**.

## 2. El mapeo

| Portmaster | wnotify / notifyd |
|---|---|
| `Title` / `Message` | `summary` / `body` |
| `EventID` | hint `x-portmaster-event-id`; y un mapa `EventID → id de notifyd` |
| re-`save` del mismo EventID (cambio de estado, `update`) | `replaces_id` = id previo → evento **`replace`** (revisión +1) |
| `Type` Info / Warning / Prompt / Error | `urgency` 0 / 1 / 2 / 2; `category` `portmaster.<tipo o Category>` |
| `AvailableActions` | `actions` planas `ID, Text, ...` (todas, incluso `in-app-only`) + hint `x-portmaster-actions` con tipo, payload y visibilidad en JSON |
| `State`, `SelectedActionID`, `Expires`, `GUID`, `EventData` | hints `x-portmaster-*` |
| `ShowOnSystem && useSystemNotifications` | hint `x-portmaster-show-on-system`: **se manda igual**; el display decide si la muestra |
| Prompt | `expire_timeout` 0 (nunca); con `Expires`: ms restantes; si no, -1 |
| ícono | `app_icon` **e** `image-path` = `assets/data/icons/pm_light_128.png` absoluto (configurable con `-define:PM_NOTIFICATION_ICON=`) |
| `Delete()` / expiración | `CloseNotification` → `close_request`, y `closed` cuando el display lo confirma |
| `ActionInvoked <id> <key>` del display | `select_and_execute_action(key)` + `save` (→ `replace` con estado `executed`) |
| `NotificationClosed` razón 2 (descartada) | se saca del conjunto en memoria, como el "dismiss" de la UI; el historial la conserva |

La decisión clave es la fila de `ShowOnSystem`: en Go ese flag decide si la
notificación sale del proceso. Acá **todas** salen, porque salir es lo que la
persiste; mostrarla es otro problema.

## 3. Lo que muestra el historial

Un prompt "¿Permitir conexión?" que el usuario acepta, y una actualización que
se borra (salida real de `notifyd events` en el test e2e):

```
3  notify        2  {"app_name":"Portmaster","app_icon":"…pm_light_128.png","summary":"Allow connection?",
                     "expire_timeout":0,"actions":[{"key":"allow","label":"Allow"},{"key":"block","label":"Block"}],
                     "hints":{"urgency":2,"category":"portmaster.prompt","image-path":"…pm_light_128.png",
                              "x-portmaster-state":"active",…}}
4  action        2  {"key":"allow"}
5  replace       2  {…"x-portmaster-state":"executed","x-portmaster-selected-action":"allow"…}
6  notify        3  {…"summary":"Update available","expire_timeout":-1…}
7  replace       3  {…"expire_timeout":3600000,"x-portmaster-expires":1795099314…}
8  close_request 3  {}
9  closed        3  {"reason":3}
```

Con esto se puede responder lo que Portmaster solo no puede: qué prompts se
mostraron, cuál fue la respuesta y cuándo, qué avisos se ignoraron.

## 4. El puente: cuatro problemas de concurrencia

### 4.1 El pipe síncrono que bloquea escrituras

Primer intento de display de prueba: un hilo por conexión, leyendo; para
mandar `ActionInvoked`, otro hilo escribe en el handle del suscriptor. La
señal nunca salía.

En Windows, sobre un handle de pipe **sin** `FILE_FLAG_OVERLAPPED`, la E/S
está serializada por handle: mientras un hilo está bloqueado en `ReadFile`,
un `WriteFile` de otro hilo sobre el mismo handle **espera a que la lectura
termine**. El suscriptor no manda nada después de `Subscribe`: la lectura no
termina nunca.

Reglas que salen de ahí (el módulo 14 las vio del lado del daemon en C):

* conexiones separadas para pedidos y para la suscripción (wnotify lo hace);
* el hilo de una conexión de suscripción **deja de leer** tras `Subscribe`, o
  se usa E/S overlapped;
* para cortar una suscripción: `CancelIoEx` + `CloseHandle` destraban el
  `ReadFile` del hilo lector antes del `join`.

### 4.2 La respuesta y la señal llegan por caminos distintos

notifyd contesta `OK <id>` en la conexión de pedidos y, si el display hace
clic enseguida, `ActionInvoked <id>` llega por la de suscripción. El hilo
lector podía buscar el id antes de que el emisor lo guardara en el mapa.
Arreglo: el mutex del puente se mantiene durante la llamada **y** la
actualización del mapa; el handler de señales lo toma antes de buscar.
(notifyd tiene el mismo problema hacia abajo y lo resuelve igual: `fwd_lock`
cubre `forward_show` completo.)

### 4.3 Join con un lock tomado

`unsubscribe` hace `join` del hilo lector, y el handler de ese hilo toma el
mutex del puente. Desuscribirse con el mutex tomado = deadlock. La
suscripción se desarma fuera del mutex, con un `connect_mu` aparte que
serializa conectar/desconectar.

### 4.4 Un delete que se bloqueaba a sí mismo

`database.delete_record` (corregido en el port para que bloquee el record,
módulo 23) marca `Meta.Deleted` y llama al storage. En el storage inyectado
de notificaciones, *ese record es la notificación*, y su `delete` la volvía a
bloquear: la suite entera colgada con la CPU ocupada. En Go ese camino
retornaba temprano y dejaba un zombi en el mapa. Arreglo:
`delete_internal(already_locked, already_marked)`.

## 5. Cuando notifyd no está

* Una advertencia por corte, no una por notificación.
* Un worker `Repeat` reintenta conectar; al volver, **reenvía las activas** y
  vacía los cierres que ocurrieron mientras tanto.
* El test e2e lo cubre: una notificación creada antes de levantar notifyd
  aparece en el historial como su primer `notify`.

## 6. Otros detalles del port

* `Response()` devuelve un `chan string` en Go → `response_wait(n, timeout)`
  sobre una `Cond` del record. Devuelve también una respuesta elegida
  **antes** de empezar a esperar (en Go el oyente tardío la perdía: hay un
  TODO upstream) y vuelve al expirar (en Go colgaba para siempre).
* `Expired()` (canal que se cierra) → `expired_wait`.
* `EventData`/`Payload` son `interface{}`: en Odin un `any` que apunta a una
  variable local es un puntero al stack. `new_payload` copia al heap.
* `strings.replace_all` de Odin devuelve **su entrada** (no una copia) si no
  reemplazó nada; Go siempre devuelve una cadena nueva. Liberar "el
  resultado" con otro allocator rompió el proceso. Clonar siempre.
* El display de prueba responde `CloseNotification` con
  `NotificationClosed <id> 3`, como uno real: en modo `--forward` notifyd sólo
  registra `closed` si el display lo confirma.

## 7. Probarlo

```bash
export PATH="$PATH:/c/Users/Shifty/DEV/vcpkg/installed/x64-windows/bin"
odin test odin/base/notifications -collection:pm=odin -vet \
  -define:PM_NOTIFYD=C:/Users/Shifty/DEV/LowLevel-Katas-n6/N4-notifications/notifyd/notifyd.exe
```

El e2e levanta `fakedisplay` y `notifyd --db <tmp> --forward <pipe>` en pipes
únicos (`WNOTIFY_PIPE`), los cierra **por handle de proceso** (nunca por
nombre de imagen: puede haber un notifyd real corriendo) y verifica contra el
SQLite de notifyd: eventos, `app_icon`, `image-path`, revisión y razón de
cierre.

Con un display real en vez del falso:

```powershell
notifyd.exe --exec ..\..\gitclones\herbe\win32\herbe.exe
notifyd.exe history --app Portmaster
```

---

Anterior: [24](24-compatibilidad-binaria-con-go.md) · Auditoría de features de pago: [N6](../../../N6-portmaster/AUDITORIA-features-de-pago.md).
