# Módulo 17: Porteo de un proyecto GLib grande — dunst

dunst tiene ~12.000 líneas de C, depende de GLib/GIO, cairo, pango y
gdk-pixbuf, y habla con el mundo por D-Bus y X11/Wayland. Es el caso opuesto a
herbe: no conviene reescribir, conviene **cambiar lo mínimo** y dejar que el
resto compile tal cual. Este módulo documenta cómo se hizo y cuánto costó.

Código: fork `dunst-project/dunst`, rama `port/windows` (`WINDOWS.md`,
`src/win32/`).

---

## 1. Medir antes de tocar

```sh
grep -hoE '#include <[^>]+>' src/*.c src/*.h | sort | uniq -c | sort -rn
grep -n 'fork\|exec\|wordexp\|fnmatch\|regcomp\|scandir\|getpwuid\|g_unix_' src/*.c
```

Resultado, separado por estrategia:

| Qué | Dónde | Estrategia |
|---|---|---|
| `struct output` (X11 / Wayland) | `output.h` | **tercera implementación**: `src/win32/win32.c` |
| GDBus | `dbus.c` | **shim de transporte** por macros (sección 4) |
| `regex.h`, `fnmatch.h`, `wordexp.h`, `pwd.h`, `libgen.h`, `dirent.h` (`scandir`), `unistd.h`, `getline` | menu, rules, utils, settings, ini | **headers de compatibilidad** en `src/win32/compat/` + `/FI` |
| `fork`+`execlp` de scripts, `g_unix_signal_add`, `clock_gettime`, `st_mtim`, `setenv` | notification, dunst, utils | `#ifdef _WIN32` **puntual** |
| `argv` | main | `g_win32_get_command_line()` (UTF-8) |

La vtable `struct output` es lo que hace el port viable: dunst ya separaba
“dibujar un frame” de “mostrarlo en pantalla”.

---

## 2. Dependencias: vcpkg y su costo real

```bat
vcpkg install glib cairo pango gdk-pixbuf --triplet x64-windows
```

14 paquetes, **1,7 horas** en esta máquina. El cuello de botella no es GLib
sino `gettext[tools]` y `gettext-libintl` (dependencias de GLib), que se
compilan con autotools dentro de un MSYS2 que vcpkg descarga. Planificá el
tiempo: lanzalo en background al principio y trabajá en el código mientras
tanto (headers de compat, backend, shim), compilando por partes a medida que
se instalan paquetes (`glib` estuvo antes que `pango`: `compat.c`, `log.c` y
`wnotify.c` se pudieron verificar antes).

CMake encuentra las libs por pkg-config; vcpkg no instala un `pkgconf.exe`
utilizable en `installed/`, pero trae uno en su MSYS2
(`downloads/tools/msys2/*/mingw64/bin/pkgconf.exe`):
`-DPKG_CONFIG_EXECUTABLE=...`. Los targets importados por pkg-config no tienen
DLLs asociadas, así que `$<TARGET_RUNTIME_DLLS>` queda vacío: se copian las
DLLs de `installed/x64-windows/bin` con un glob post-build.

---

## 3. Headers de compatibilidad: que el código no se entere

Mismo patrón que coreutils (módulo 04): un directorio `compat/` antes en el
include path, y un header forzado (`/FIdunst_compat.h`) para lo que no es un
header propio (`ssize_t`, `getline`, `strcasecmp`, `S_IFIFO`, `M_PI` vía
`_USE_MATH_DEFINES`).

El más interesante es **`regex.h` implementado sobre GRegex**. dunst ya
enlaza GLib, que trae PCRE2; reimplementar POSIX ERE sería absurdo:

```c
int regcomp(regex_t *preg, const char *regex, int cflags)
{
        GRegexCompileFlags f = G_REGEX_DEFAULT;
        if (cflags & REG_ICASE)   f |= G_REGEX_CASELESS;
        if (cflags & REG_NEWLINE) f |= G_REGEX_MULTILINE;
        preg->re = g_regex_new(regex, f, 0, NULL);
        ...
}
int regexec(const regex_t *preg, const char *s, size_t n, regmatch_t m[], int eflags)
{
        GMatchInfo *mi;
        gboolean ok = g_regex_match(preg->re, s, 0, &mi);
        for (size_t i = 0; ok && i < n; i++)
                g_match_info_fetch_pos(mi, i, &m[i].rm_so, &m[i].rm_eo);
        ...
}
```

`menu.c` (URLs) y `rules.c` (filtros) compilan sin cambios. Diferencia a
documentar: PCRE no es ERE al 100% (backreferences, clases POSIX
`[[:alpha:]]` funcionan; casos raros de *leftmost-longest* no).

`wordexp` se reduce a lo que dunst usa para rutas: `~`, `$VAR`, `${VAR}` y,
por ser Windows, `%VAR%`; sin sustitución de comandos (dunst pasa
`WRDE_NOCMD` igual). `scandir` se apoya en `GDir` para recibir nombres UTF-8.

---

## 4. Reemplazar GDBus sin tocar los métodos

`dbus.c` tiene 1.500 líneas, de las cuales ~90 % construyen o leen
`GVariant`s (decodificar hints, armar el historial, las reglas). Sólo un
puñado de llamadas tocan el **transporte**. Un header incluido bajo `_WIN32`
las redirige:

```c
typedef struct wn_invocation WnInvocation;
#define GDBusMethodInvocation WnInvocation
#define g_dbus_method_invocation_return_value(inv, v) wn_inv_return_value((inv), (v))
#define g_dbus_method_invocation_return_dbus_error(inv, n, m) wn_inv_return_error_literal((inv), (m))
#define g_dbus_method_invocation_return_error wn_inv_return_error
#define g_dbus_connection_flush(...) ((void)0)
#define g_dbus_connection_emit_signal(c, dest, path, iface, name, body, err) \
        wn_emit_signal((iface), (name), (body), (err))
```

Y el bloque que poseía el nombre en el bus (`g_bus_own_name`, callbacks de
*name acquired/lost*) se reemplaza con `#ifndef _WIN32 ... #else #include
"win32/dbus_win32.inc" #endif`. El `.inc` se incluye textualmente **al final
de `dbus.c`** para poder usar sus `static` (introspección, tablas de métodos).

Lo que hace el puente:

* `Notify` en formato wnotify → `GVariant (susssasa{sv}i)` con los tipos
  correctos por hint (`y` urgency, `i` value, `(iiibiiay)` image) →
  `dbus_cb_fdn_methods()` original.
* Métodos de `org.dunstproject.cmd0` (los de `dunstctl`): el argumento viaja
  como **texto GVariant**, parseado con el tipo exacto que dice la
  introspección (`g_dbus_interface_info_lookup_method` → `(u)`, `(si)`…), así
  `"(uint32 0,)"` no se confunde con `(i)`.
* Respuestas: aplanadas a campos para la interfaz freedesktop (cualquier
  cliente wnotify las entiende), `g_variant_print` para la de dunst, o JSON si
  el pedido llega con prefijo `json:` (`dunstctl history`).

### Marshalling al main loop

El servidor de pipes llama en el hilo de cada cliente; dunst es
single-threaded (colas, ventana, settings). Cada request se pasa al main loop
y el hilo del cliente espera:

```c
g_main_context_invoke(NULL, dispatch_in_main, &call);
g_mutex_lock(&call.lock);
while (!call.done && !g_atomic_int_get(&wn_shutting_down))
        g_cond_wait_until(&call.cond, &call.lock, g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND);
```

El `wait_until` con el flag de apagado evita un deadlock al salir: si el main
loop ya terminó, las invocaciones encoladas nunca corren y `wn_server_stop`
esperaría para siempre a hilos que esperan al main loop.

---

## 5. El backend Win32

Detalles en el módulo 15. Lo específico de dunst:

* `win_get_context()` sólo se usa para **medir** texto con pango: alcanza un
  contexto sobre una superficie de 1×1.
* `display_surface()` recibe la `cairo_image_surface` ARGB32 ya dibujada y la
  entrega con `UpdateLayeredWindow` (es BGRA premultiplicado, sin conversión).
* `calc_window_pos()` de `draw.c` se reutiliza tal cual con la
  `screen_info` del monitor activo (work area, DPI efectivo).
* `follow = mouse/keyboard` → `GetCursorPos` / `GetForegroundWindow`.
* Los clics se traducen a los códigos `BTN_LEFT/MIDDLE/RIGHT` de Linux
  (`input-event-codes.h`) que `input.c` espera.

---

## 6. Errores reales del primer build

Después de esperar las dependencias, el primer `cmake --build` dio sólo:

| Error | Causa | Arreglo |
|---|---|---|
| `M_PI` no declarado | MSVC sólo lo define con `_USE_MATH_DEFINES` | en `dunst_compat.h` |
| `S_IFIFO` no declarado | MSVC tiene `_S_IFIFO` | alias en `dunst_compat.h` |
| `newline in string literal` en `libgen.h` | un `'\\'` que el heredoc del shell redujo a `'\'` | escribir archivos C con un editor, no con heredocs |
| `LPSTR` → `LPCWSTR` | `IDC_HAND` es `MAKEINTRESOURCEA` sin `UNICODE` | cast explícito |

Y uno en `dunstctl.ps1`: una función de PowerShell que devuelve un array de
un elemento lo **desenrolla** a escalar, y `[0]` sobre un string devuelve su
primer carácter (`"uint32 3"` → `u`). Se devuelve `,@(...)`.

---

## 7. Verificación

* 3 notificaciones con urgencias distintas → captura: colores por urgencia,
  crítica arriba, `<b>` aplicado, acentos, barra de progreso (`value=60`).
* `GetServerInformation`/`GetCapabilities` por el pipe.
* `dunstctl`: `count`, `close`, `close-all`, `history` (JSON parseable),
  `history-pop`, `is-paused`/`set-paused`, `debug`.
* `notify-send -A ...` + `dunstctl action 0` → el cliente recibe
  `ActionInvoked(id, "default")`.

Pendiente de probar: menú contextual (necesita un “dmenu” en Windows),
reglas con `script`, temas de íconos, la suite de `test/`.
