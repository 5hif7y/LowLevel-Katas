# Módulo 16: Arqueología de un porteo — tiramisu (C → Vala → C MSVC y Odin)

tiramisu es un daemon de notificaciones que no dibuja nada: imprime cada
notificación en `stdout` y deja que el usuario la procese. En octubre de 2021
se reescribió **entero** de C a Vala. Portarlo a Windows obligó a decidir
*qué* tiramisu portar, y la respuesta fue: los dos, en dos ramas, con la
unión de las features de toda su historia.

| Rama | Desde | Lenguaje destino |
|---|---|---|
| `port/c-msvc` | `76932bb` (último commit en C) | C11, MSVC, sin GLib |
| `port/odin` | `master` (Vala) | Odin, `core:sys/windows` |

---

## 1. Encontrar el corte en el historial

```sh
git log --format='%h %ad %s' --date=short --stat | grep -E '^[0-9a-f]{7} |\.(c|h|vala)'
```

```
f161a91 2021-10-11 Remove C source files
 src/events.c | 92 ---  src/notification.c | 320 ---  src/output.c | 211 --- ...
02f38ce 2021-10-11 Initial Vala source
...
76932bb 2021-10-09 Update README.md        <- último árbol en C
```

Y para ver cómo evolucionaron las opciones, sin leer cada commit:

```sh
for c in $(git log --format=%h 76932bb -- src/tiramisu.c | head -30); do
  echo "## $c"; git show $c:src/tiramisu.c | grep -n 'getopt'
done
```

Resultado: **tres generaciones**, no dos.

| Generación | Época | Opciones | Salida |
|---|---|---|---|
| 1.x C | 2020 | `-j`, `-d delim` | etiquetada (`app_name: ...`) o JSON con hints/actions como objetos |
| 2.x C | may–oct 2021 | `-o fmt`, `-s`, `-j` | string de formato `#source #id ...`, hints CSV, imágenes base64 |
| Vala | oct 2021– | `-o`, `-j`, `-s` + largas | igual que 2.x, con `replaces_id` y `icon` en JSON |

Dato que sólo aparece al leer el árbol: en `76932bb`, `src/output.c` existe
pero **no está en el Makefile**. Es código muerto de 1.x que nadie borró. Sus
features (JSON tipado, actions como objeto) no estaban en el binario de 2.x.

---

## 2. Matriz de features y decisiones

| Feature | 1.x C | 2.x C | Vala | Ports |
|---|---|---|---|---|
| `-o FORMAT` | – | ✔ | ✔ | ✔ |
| `-j` JSON | ✔ (válido) | ✔ (**comillas simples**: JSON inválido) | ✔ | ✔ válido |
| `-s` escape | `"` y `\n` | sólo `"` | `"` `\r` `\n` | `"` `\\` `\n` `\r` `\t` y `\u00XX` |
| `-d` delimitador | ✔ | – | – | ✔ (separador del formato por defecto) |
| opciones largas | – | – | ✔ | ✔ |
| `#actions` | objeto en JSON | TODO (nunca implementado) | `joinv(",")` | C: `Label=key` (README de 2.x); Odin: `key,label` (Vala); JSON: objeto `{key: label}` |
| hints tipados | ✔ | ✔ (CSV) | JSON: todo como string | JSON con tipos reales |
| `image-data` | – | base64 **con validación de largo** | base64 sin validar | base64 con validación |
| `replaces_id` | – | ignorado (`++id` siempre) | respetado | respetado |
| `#id` impreso | – | id asignado | **`replaces_id`** (0 para nuevas) | id asignado |
| `CloseNotification` | – | stub | stub | emite `NotificationClosed(id, 3)` |
| SIGINT/SIGTERM | – | ✔ | (GApplication) | `SetConsoleCtrlHandler` |

Cada port sigue a su linaje cuando las versiones discrepan en algo cosmético
(formato de `#actions` en CSV) y toma la versión correcta cuando una era un bug.

---

## 3. Bugs que aparecieron al portar

### Interpolación secuencial = inyección

2.x C y Vala interpolan con reemplazos encadenados:

```vala
fmt.replace("#source", app_name).replace("#id", ...).replace("#summary", summary).replace("#body", body)...
```

Un `summary` que contiene `#body` se expande **otra vez** con el body. En
modo JSON eso rompe el documento; en el wrapper bash (`herbed-tiramisu.sh`,
que *ejecuta* cada línea) es inyección de comandos. Los ports recorren el
template una sola vez y nunca re-escanean el texto reemplazado.

Test que lo prueba: `notify-send 'New email' 'You have mail #summary'` →
el body sale literal.

### JSON “válido”

Escapar sólo `"` no alcanza: una `\` en el texto (rutas de Windows,
`app"q\b`) produce JSON inválido. Los ports escapan `\` primero, después `"`,
y todo carácter < 0x20. Cada línea de salida se validó con `ConvertFrom-Json`.

### El wrapper que nunca mostraba nada

`herbed-tiramisu.sh` hacía `timeout ${timeout}s herbe ...` con el
`expire_timeout` de la spec, que está en **milisegundos** y vale `-1` por
defecto. `timeout -1s` es un error: con la configuración por defecto de casi
todas las apps, herbe nunca se ejecutaba. El port en PowerShell parsea JSON,
convierte ms y no mata nada cuando el timeout es `<= 0`.

---

## 4. De Vala a Odin

Vala compila a C sobre GObject/GLib. Lo que Vala resolvía con atributos y
GLib, en Odin se escribe:

| Vala | Odin |
|---|---|
| `[DBus (name = "...")] class NotificationDaemon` | `handle_request :: proc(c: ^wnotify.Conn, f: []string, user: rawptr)` con `switch f[0]` |
| `GLib.HashTable<string, Variant> hints` | `[]wnotify.Hint` (nombre, tipo D-Bus, valor) |
| `OptionEntry[]` + `GLib.Application` | parser propio de `os.args` (cortos agrupables, `--x=v`) |
| `this.hold()` | `WaitForSingleObject(quit_event)` |
| `public static bool json` | campo de un struct global `Tiramisu` (el nombre del tipo y de la variable no pueden coincidir en Odin) |
| `string.replace` encadenado | un recorrido con `strings.Builder` |
| GLib.Base64 | `core:encoding/base64` |

Odin no trae binding para `ProcessIdToSessionId`; se declara con
`foreign import kernel32 "system:Kernel32.lib"`. El resto de la API de pipes
(`CreateNamedPipeW`, `ConnectNamedPipe`, `GetOverlappedResult`) está en
`core:sys/windows`.

La memoria temporal por request usa `context.temp_allocator` del hilo de la
conexión y se libera con `free_all` al terminar cada línea: el equivalente a
un arena por mensaje.

El bug de concurrencia del port Odin (handles sincrónicos + señales) está
explicado en el módulo 14, §4.

---

## 5. Cómo se verificó

El mismo script de PowerShell corre contra los dos binarios
(`tiramisu.exe` C y `tiramisu-odin.exe`):

1. segunda instancia → falla con “another notification daemon owns …”;
2. `GetServerInformation` / `GetCapabilities`;
3. `notify-send -A ...` espera y se libera con `--close=ID`;
4. `replaces_id` conserva el id; uno nuevo recibe el siguiente;
5. raw protocol con comillas, `\`, ñ, emoji, `\n`, TAB, imagen válida e
   inválida (la inválida se descarta);
6. cada línea de `-j` pasa por `ConvertFrom-Json`.

> PowerShell 5.1 elimina las comillas dobles embebidas al pasar argumentos a
> programas nativos (`'body "x"'` llega como `body x`). No es un bug del port:
> por eso los casos con comillas usan el protocolo crudo con
> `System.IO.Pipes.NamedPipeClientStream`.
