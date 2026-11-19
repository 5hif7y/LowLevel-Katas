# Módulo 15: Ventanas sin X11 — herbe y el backend Win32 de dunst

`herbe` (≈200 líneas, Xlib + Xft) y el backend de salida de `dunst` (X11 o
Wayland detrás de una vtable) son dos formas de dibujar un popup en Linux. Este
módulo mapea cada concepto de X11 a Win32 y documenta las trampas encontradas
al portarlos.

Código: `herbe` rama `port/windows` (`win32/herbe.c`), `dunst` rama
`port/windows` (`src/win32/win32.c`).

---

## 1. La ventana: `override_redirect` → estilos extendidos

En X11 un popup de notificación es una ventana con `override_redirect = True`:
el window manager no la decora, no la mueve, no le da foco. En Win32 eso son
**cuatro** decisiones separadas:

```c
CreateWindowExW(
    WS_EX_TOPMOST        /* encima de todo                       */
  | WS_EX_TOOLWINDOW     /* fuera de la barra de tareas y Alt+Tab */
  | WS_EX_NOACTIVATE,    /* no roba el foco al aparecer           */
    cls, L"herbe",
    WS_POPUP,            /* sin borde, sin barra de título        */
    x, y, w, h, NULL, NULL, inst, NULL);
ShowWindow(hwnd, SW_SHOWNOACTIVATE);
```

…y una quinta en el `WndProc`, porque `WS_EX_NOACTIVATE` no impide que un
**clic** active la ventana:

```c
case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
```

Sin eso, cerrar una notificación con un clic le saca el foco a la aplicación
en la que estabas escribiendo.

### Dónde ponerla: área de trabajo, no pantalla

`DisplayWidth/Height` de X11 → `GetMonitorInfoW(...).rcWork`, no `rcMonitor`.
En Linux no hay una barra de tareas que esquivar; en Windows una
notificación en `BOTTOM_RIGHT` con `rcMonitor` queda debajo de ella.

### DPI

`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` y
`GetDpiForMonitor(MDT_EFFECTIVE_DPI)`. Las medidas del `config.h` se tratan
como píxeles a 96 DPI y se escalan con `MulDiv(v, dpi, 96)`; el tamaño de
fuente en puntos con `-MulDiv(pt, dpi, 72)`. dunst ya tenía el concepto
(`get_scale()`), sólo hubo que alimentarlo con el DPI del monitor activo.

---

## 2. Texto: Xft UTF-8 → GDI UTF-16

`XftTextExtentsUtf8` → `GetTextExtentPoint32W`. El argumento llega como
`wchar_t*` usando `wmain` (nada de convertir desde la página ANSI, módulo 12).

Dos detalles al portar el *word wrap* de herbe:

* **No partir un par sustituto.** UTF-8 upstream podía cortar a mitad de un
  code point; en UTF-16 el equivalente es cortar entre un high y un low
  surrogate (`IS_HIGH_SURROGATE(s[eol-1])` → retroceder uno).
* **Bug heredado.** El bucle de upstream se detiene cuando el ancho es `>=` al
  máximo y después retrocede un carácter. Con fuentes monoespaciadas, una línea
  que llena *exactamente* el ancho (muy común: 420 px / 7 px = 60) pierde un
  carácter y corta una palabra al medio. Se reescribió como “el prefijo más
  largo cuyo ancho es `<=` al máximo”. Se detectó con una captura de pantalla,
  no leyendo el código.

---

## 3. Transparencia: ventanas *layered* y el formato de cairo

dunst dibuja cada frame en una `cairo_image_surface` `ARGB32` y la entrega a
`output->display_surface()`. En X11 eso se pinta sobre un visual ARGB si hay
compositor. En Win32, el equivalente directo es:

```c
CreateWindowExW(WS_EX_LAYERED | ..., ...);
/* cada frame: */
CreateDIBSection(... 32bpp, top-down ...);   /* copiar las filas respetando el stride de cairo */
BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
UpdateLayeredWindow(hwnd, screen_dc, &pos, &size, mem_dc, &zero, 0, &blend, ULW_ALPHA);
```

La coincidencia que lo hace trivial: **cairo ARGB32 es BGRA premultiplicado en
little-endian**, que es exactamente lo que `UpdateLayeredWindow` exige. No hay
conversión de píxeles. Bordes redondeados y sombras salen gratis: el alfa por
píxel ya está en la superficie. La opción `transparency` (que en X11 era
`_NET_WM_WINDOW_OPACITY`) es `BLENDFUNCTION.SourceConstantAlpha`.

---

## 4. Integrar mensajes de ventana en un `GMainLoop`

dunst no tiene su propio bucle de eventos: X11 entra al `GMainLoop` como un
`GSource` sobre el file descriptor de la conexión. En Windows, GLib tiene un fd
mágico para la cola de mensajes del hilo:

```c
s->fd.fd = G_WIN32_MSG_HANDLE;   /* g_poll usa MsgWaitForMultipleObjectsEx */
s->fd.events = G_IO_IN;
g_source_add_poll(source, &s->fd);
/* dispatch: */
while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
```

La ventana tiene que crearse **en el hilo que corre el main loop**, porque la
cola de mensajes es por hilo.

---

## 5. Señales Unix → mecanismos Win32

| Upstream | Uso | Windows |
|---|---|---|
| `SIGUSR1`/`SIGUSR2` a herbe (`pkill`) | descartar / aceptar la notificación activa | eventos con nombre `Local\herbe-dismiss` / `-action` (`herbe --dismiss`) |
| `SIGALRM` + `alarm(duration)` | timeout | `SetTimer` + `WM_TIMER` |
| `SIGTERM` | terminar ordenadamente | `WM_CLOSE` (`taskkill` sin `/F`) |
| `SIGINT` | Ctrl+C | `SetConsoleCtrlHandler` (corre en **otro hilo**: sólo `PostMessage`/`SetEvent`, o `g_main_context_invoke` en dunst) |
| `SIGUSR1/2` a dunst (pausa) | pausar | propiedad `paused` por el pipe (`dunstctl set-paused`) |

Un detalle de los eventos con nombre: si alguien hace `herbe --dismiss`
cuando no hay nada en pantalla, el evento queda señalizado y **mataría a la
próxima notificación**. `pkill` sin procesos no tiene ese efecto. Solución:
`ResetEvent` al tomar el turno.

---

## 6. La cola de herbe: semáforo POSIX → mutex con nombre

Upstream serializa los popups con `sem_open("/herbe")` + `sem_wait`. Si un
herbe muere con el semáforo tomado, la cola queda trabada para siempre (de
ahí el `sem_unlink` escondido en el camino de “uso incorrecto”).

Un **mutex** con nombre resuelve lo que el semáforo no puede: si el dueño
muere, el próximo `Wait` devuelve `WAIT_ABANDONED` y el mutex pasa a ser suyo.

```c
HANDLE m = CreateMutexW(NULL, FALSE, L"Local\\herbe");
/* esperar sin congelar la ventana: */
r = MsgWaitForMultipleObjects(1, &m, FALSE, INFINITE, QS_ALLINPUT);
/* WAIT_OBJECT_0 o WAIT_ABANDONED_0 -> es nuestro; WAIT_OBJECT_0 + 1 -> bombear mensajes */
```

Ojo: un mutex es **de un hilo**; se libera con `ReleaseMutex` desde el mismo
hilo que lo tomó. `Local\` lo limita a la sesión, como el SessionId del pipe.

---

## 7. Subsistema consola vs GUI: quién espera a quién

`herbe "x" && accion` depende de que el shell **espere** a herbe y lea su
exit code. `cmd.exe` y PowerShell **no esperan** a los programas del
subsistema GUI. Por eso herbe es un programa de consola, y cuando lo lanza
algo sin consola (Explorer, un atajo) oculta la consola nueva que le creó
Windows:

```c
DWORD procs[2];
if (GetConsoleProcessList(procs, 2) == 1)   /* soy el único: la consola es mía */
    ShowWindow(GetConsoleWindow(), SW_HIDE);
```

---

## 8. Qué se verificó y cómo

* Exit codes de herbe: timeout → 2, `--action` → 0, `--dismiss` sin nadie → 1,
  cola: el segundo popup arranca su timer cuando el primero se va.
* Render: capturas de pantalla (`CopyFromScreen`) de herbe con ñ, acentos,
  emoji y una palabra más ancha que la ventana.
* herbed-tiramisu: `notify-send -t 1500` → popup visible a los 0.7 s, cerrado
  a los 3.2 s.
