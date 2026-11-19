# wnotify-toast

Muestra una notificación como **toast nativo de Windows** con el contrato de
proceso de herbe, para usarlo como pantalla de `notifyd --exec`:

| exit | cuándo | notifyd registra |
|---|---|---|
| 0 | clic en el toast o en su botón | `ActionInvoked(id, "default")` + cerrado (2) |
| 2 | descartado, vencido, `WM_CLOSE` (`CloseNotification`, `taskkill`) | cerrado (2, o 3 si lo pidió un cliente) |
| 1 | error (XML, AUMID inexistente) | cerrado (4) |

Entrada: `NOTIFYD_SUMMARY/BODY/APP/ICON/URGENCY/ID` (los pone notifyd), o
argumentos como líneas igual que herbe. Opciones: `--aumid ID`, `--keep`.

```bat
toast\build.bat           :: C++/WinRT del Windows SDK, sin dependencias extra
```

Por qué C++ y no un script: Windows PowerShell 5.1 no puede suscribirse a
eventos WinRT, así que no distingue un clic de un descarte. Por qué una ventana
oculta de nivel superior: `notifyd` cierra mandando `WM_CLOSE` a las ventanas
del proceso y `EnumWindows` no ve las *message-only*. Por qué limpiar dentro
del handler de Ctrl: con `CTRL_CLOSE_EVENT` Windows mata el proceso apenas el
handler retorna (`STATUS_CONTROL_C_EXIT`), y el toast quedaba huérfano en el
Centro de actividades.

Guía completa: [../NATIVO-VS-WNOTIFY.md](../NATIVO-VS-WNOTIFY.md).
