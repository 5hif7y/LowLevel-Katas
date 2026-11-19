# N5 — Multiplexores de terminal en Windows nativo (MSVC)

Porteo de **dtach**, **abduco** y **dvtm** desde C Unix-like a C MSVC, sin Cygwin,
sin MSYS2 y sin WSL: binarios `.exe` nativos que usan ConPTY, sockets AF_UNIX de
Winsock y la consola de Windows.

| Programa | Qué hace | Estado |
| :--- | :--- | :--- |
| `dtach` | desacopla un programa de la terminal (sólo el "detach" de screen) | completo, 11 pruebas |
| `abduco` | sesiones con nombre, listado, códigos de salida | completo, 10 pruebas (`-r`/`-l`/`-p` compilan, sin prueba) |
| `dvtm` | gestor de ventanas en mosaico dentro de la terminal (dwm para la consola) | núcleo completo, 9 pruebas (incluye abduco+dvtm) |

Cada programa también está publicado como fork autocontenido (rama `port/windows`):
[5hif7y/dtach](https://github.com/5hif7y/dtach/tree/port/windows),
[5hif7y/abduco](https://github.com/5hif7y/abduco/tree/port/windows),
[5hif7y/dvtm](https://github.com/5hif7y/dvtm/tree/port/windows). Ver `UPSTREAM.md`.

## Compilar y probar

```bat
build.bat
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

`build.bat` busca Visual Studio con `vswhere`, baja `upstream/` pinneado si falta
(PDCursesMod lo necesita dvtm) y deja todo en `build\bin\Release\`.

Requisitos: Windows 10 1809+ (ConPTY; AF_UNIX desde 1803), Visual Studio con
"Desarrollo para el escritorio con C++", CMake 3.20+, git.

## Uso rápido

```bat
dtach -A %TEMP%\s1 cmd.exe          :: crea o se adjunta; Ctrl+\ desacopla
dtach -a %TEMP%\s1                  :: vuelve
abduco -c trabajo dvtm              :: sesión con nombre corriendo dvtm
abduco                              :: lista sesiones (* = con cliente, + = terminó)
abduco -a trabajo
dvtm powershell.exe                 :: Ctrl+G es el modificador; Ctrl+G c nueva ventana
```

> **Teclado no-US:** `Ctrl+\` (desacoplar) puede ser imposible de tipear en layouts
> españoles/latinoamericanos. Usá `-e ^q` (abduco) o `-e '^q'` (dtach).

## Estructura

```
compat/            capa POSIX -> Win32 compartida (OBJECT library)
  include/         cabeceras sombra: unistd.h, termios.h, sys/socket.h ...
                   compat.h (forzada con /FI) y compat_types.h (sin windows.h)
  src/             fd.c net.c select.c signal.c console.c pty.c proc.c fs.c misc.c init.c
  utf8.manifest    activeCodePage = UTF-8
ports/<prog>/      copia parcheada de upstream + WIN32.diff
tests/ptyrun.c     "terminal" ConPTY guionada para probar attach/detach
tests/run-tests.ps1
upstream/          (no versionado) fuentes originales pinneadas, ver UPSTREAM.md
```

## Qué se aprende acá

La documentación de porteo está en la guía maestra de N1, módulos **18 a 21**:

- [18 — Pseudoterminales: `forkpty` vs ConPTY](../N1-coreutils/Docs/porter-guide/18-pseudoterminales-forkpty-vs-conpty.md)
- [19 — Multiplexar E/S y señales sin `select()` ni kernel de señales](../N1-coreutils/Docs/porter-guide/19-select-senales-y-bucle-de-eventos.md)
- [20 — Demonios sin `fork()`, sockets AF_UNIX y metadatos en el socket](../N1-coreutils/Docs/porter-guide/20-demonios-sockets-af-unix-y-sesiones.md)
- [21 — Estudio de caso: dtach, abduco y dvtm (bitácora de bugs)](../N1-coreutils/Docs/porter-guide/21-estudio-de-caso-multiplexores.md)

## Limitaciones conocidas

- Sin control de trabajos: `Ctrl+Z`/`SIGTSTP` no suspenden (VSUSP deshabilitado).
- `kill()` a otro proceso sólo puede terminarlo (`TerminateProcess`); no hay señales capturables entre procesos.
- dvtm: `-s`/`-c` (FIFOs de estado/comandos) devuelven `ENOSYS`; las ventanas nuevas no heredan el cwd de la seleccionada; el modo copia (editor externo) compila pero no está probado.
- `wchar_t` es UTF-16: caracteres fuera del BMP (muchos emoji) se ven como dos celdas en dvtm.
- Si la terminal pone a sus hijos en un Job sin permiso de *breakaway*, la sesión muere al cerrar la pestaña (ver módulo 20).
- Diagnóstico: `set COMPAT_TRACE=C:\ruta\trace.log` hace que la capa compat (incluido el demonio desacoplado) registre su arranque.
