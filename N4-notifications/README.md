# N4 — Notificaciones: de D-Bus/X11 a Windows, y un historial que no olvida

Kata en dos partes:

1. **Portear** cuatro programas de notificaciones de Linux a Windows nativo
   (MSVC/Odin, sin MSYS ni Cygwin) y documentar cada decisión.
2. Sobre esos ports, construir **`notifyd`**: un daemon donde las
   notificaciones no son efímeras, sino eventos guardados en SQLite.

```
                 cualquier cliente wnotify
        notify-send · dunstctl · apps · scripts PowerShell
                              │  \\.\pipe\org.freedesktop.Notifications.<Session>
                              ▼
   ┌──────────────────────── notifyd ─────────────────────────┐
   │  1. persiste el evento (SQLite, WAL, synchronous=FULL)    │
   │  2. responde el id                                         │
   │  3. delega la pantalla:                                    │
   │       --exec herbe.exe        un popup por notificación   │
   │       --forward \\.\pipe\X    dunst / tiramisu encadenado  │
   │       (nada)                  headless                     │
   └───────────────────────────────────────────────────────────┘
          ▲  NotificationClosed / ActionInvoked vuelven, se registran y se reenvían
```

## Piezas

| Pieza | Dónde | Estado |
|---|---|---|
| `wnotify` — protocolo + librería C + `notify-send` | [`wnotify/`](wnotify/) | ✔ ([PROTOCOL.md](wnotify/PROTOCOL.md)) |
| `notifyd` — daemon con historial SQLite | [`notifyd/`](notifyd/) | ✔ |
| `wnotify-toast` — toasts nativos como pantalla de notifyd | [`toast/`](toast/) | ✔ |
| herbe → Win32/GDI | fork `dudik/herbe`, rama `port/windows` | ✔ |
| tiramisu C (2021) → MSVC | fork `Sweets/tiramisu`, rama `port/c-msvc` | ✔ |
| tiramisu Vala → Odin | fork `Sweets/tiramisu`, rama `port/odin` | ✔ |
| herbed-tiramisu → PowerShell | fork `mertoalex/herbed-tiramisu`, rama `port/windows` | ✔ |
| dunst → Win32 + cairo/pango (vcpkg) | fork `dunst-project/dunst`, rama `port/windows` | ✔ (ver `WINDOWS.md` del fork) |

Los forks viven fuera de este repo (`DEV/gitclones/*`), cada uno con su rama de
port; cada port trae una copia de `wnotify.c/.h`. La copia canónica es la de
este directorio.

## ¿Toasts nativos de Windows o wnotify?

Los dos funcionan y se pueden combinar (importar los toasts de Windows al historial, mostrar las notificaciones wnotify como toasts nativos, o ambas): [NATIVO-VS-WNOTIFY.md](NATIVO-VS-WNOTIFY.md).

## Tests

[`tests/run-tests.ps1`](tests/) — 75 pruebas end-to-end de todos los ports y de notifyd, incluido el soporte de íconos (ruta, `file://`, `image-path`, `image-data`, toasts) verificado por captura de pantalla.

## La guía de porteo

Lo aprendido está en la guía general, a continuación de los módulos de
coreutils:

| Módulo | Tema |
|---|---|
| [14](../N1-coreutils/Docs/porter-guide/14-dbus-en-windows-notificaciones-sobre-named-pipes.md) | D-Bus → named pipes: dueño del nombre, overlapped I/O, señales, identidad del emisor |
| [15](../N1-coreutils/Docs/porter-guide/15-ventanas-sin-x11-herbe-y-backends-win32.md) | X11 → Win32: popups sin foco, layered windows + cairo, DPI, señales, mutex vs semáforo |
| [16](../N1-coreutils/Docs/porter-guide/16-arqueologia-tiramisu-c-vala-odin.md) | Arqueología con `git`: tres generaciones de tiramisu, matriz de features, Vala → Odin |
| [17](../N1-coreutils/Docs/porter-guide/17-porteo-de-un-proyecto-glib-dunst.md) | Un proyecto GLib grande: compat headers, shim de GDBus, main loop |

## Build rápido

```bat
wnotify\build.bat        :: notify-send.exe
notifyd\build.bat        :: notifyd.exe (+ sqlite3.dll de vcpkg)
```

```powershell
.\notifyd\notifyd.exe --exec ..\..\gitclones\herbe\win32\herbe.exe
.\wnotify\notify-send.exe -a build -u critical "Build FAILED" "3 tests"
.\notifyd\notifyd.exe history
```

## dunst como pantalla de notifyd

```powershell
$env:WNOTIFY_PIPE='\\.\pipe\dunst-down'; Start-Process dunst.exe
Remove-Item Env:WNOTIFY_PIPE
notifyd.exe --forward \\.\pipe\dunst-down --windows
```

Las apps hablan con notifyd (que guarda todo, incluidos los toasts de
Windows) y dunst dibuja. Detalle del port en el módulo 17.
