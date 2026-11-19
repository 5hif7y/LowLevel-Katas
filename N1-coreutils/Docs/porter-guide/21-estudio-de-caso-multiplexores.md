# Módulo 21: Estudio de caso — dtach, abduco y dvtm

Bitácora del porteo de `N5-multiplexers`: el orden de ataque, qué cambió en
cada programa, cómo se probó algo que necesita una terminal real, y los bugs
que aparecieron, cada uno con su síntoma, su causa y su corrección. Los
módulos 18–20 explican la teoría; acá está la práctica.

---

## 1. Orden de ataque: de menor a mayor superficie

| Programa | Líneas | Superficie Unix | Por qué en este orden |
| :--- | ---: | :--- | :--- |
| dtach | ~1.700 | pty, AF_UNIX, `select`, señales, `fork` para demonio | todo el núcleo, nada de UI |
| abduco | ~1.400 | lo de dtach + doble fork, `scandir`, `getopt`, bits del socket, `pselect` | mismo núcleo, más API |
| dvtm | ~4.900 | pty × N, curses, emulador VT propio, `wcwidth`, extensiones GNU | reutiliza todo y suma curses |

La capa `compat/` se escribió **completa antes** de compilar dtach, a partir de
un inventario de las llamadas de los tres programas (`grep` de `fork|exec|select|
signal|ioctl|tc[gs]etattr|...`). Resultado: dtach enlazó al primer intento salvo
`fork` (código inalcanzable en Windows, ahora `#define fork() (errno=ENOSYS,-1)`),
abduco compiló sin errores, dvtm necesitó dos rondas de extensiones GNU.

## 2. Qué cambió en el código upstream

El principio de N1 (overlay sin tocar upstream) no se puede sostener: la rama
"hijo" de `fork()` es código que no tiene a dónde ir. Se aceptó un delta
mínimo, siempre bajo `#ifdef _WIN32` con comentario `WIN32:`, auditado en
`ports/*/WIN32.diff`:

| | líneas agregadas | qué |
| :--- | ---: | :--- |
| dtach | 119 | spawn ConPTY, re-ejecución como master, redibujado `winch` |
| abduco | 129 | re-ejecución como servidor, VLA, rutas `C:\`, `%COMSPEC%`, `.exe` |
| dvtm | ~100 + `config.h` | extensiones GNU, spawn ConPTY, `TERM`, `EIO`, shell, modo eventos |

### Extensiones GNU que MSVC (C11) rechaza

| Construcción | Dónde | Reemplazo |
| :--- | :--- | :--- |
| `"\e["` | dvtm, 38 veces | `"\033["` — MSVC sólo avisa C4129 y **descarta la barra**: compila y emite `e[` |
| `case 30 ... 37:` | vt.c, 4 veces | etiquetas explícitas |
| `#define debug(fmt, args...)` | dvtm.c | `#define debug(...)` |
| `char buf[n]` (VLA) | abduco, vt.c | tamaño constante o `malloc` |
| `char buf[MB_CUR_MAX + 1]` | vt.c | `MB_LEN_MAX` (en el UCRT `MB_CUR_MAX` es una llamada) |
| `= { }` | config.h de dvtm | `{ NULL }` |
| `__attribute__((const))` | vt.c | `#define __attribute__(x)` en `compat.h` |

El caso `\e` merece atención: es el único que **no rompe la compilación**.

## 3. Probar programas que exigen una terminal

`dtach -a` sin terminal responde "Attaching to a session requires a terminal".
No alcanza con redirigir stdin. La solución fue escribir la herramienta de
prueba con la misma tecnología que se estaba portando: `tests/ptyrun.c`, ~150
líneas, una "terminal guionada" sobre ConPTY:

```bat
ptyrun -c 80 -r 25 -t 6000 -k "1500:echo marker123\r" -k "1500:\x1c" -- dtach -a s1
```

Cada `-k ms:teclas` espera y teclea; `RESIZE CxR` redimensiona; stdout recibe
el flujo VT que el programa pintó y termina con `[exit N]`. `run-tests.ps1`
encadena escenarios y busca texto en ese flujo: 30 pruebas, entre ellas:

- detach con `Ctrl+\`, re-attach que **repinta** la pantalla anterior;
- `mode con` dentro de la sesión informa el ancho nuevo tras `RESIZE` (cadena
  completa: consola → `SIGWINCH` emulado → paquete → `ResizePseudoConsole`);
- códigos de salida de punta a punta (`exit 3` → "exit status 3");
- dvtm dentro de abduco: desacoplar y volver a ver las ventanas de dvtm.

## 4. Bitácora de bugs

| # | Síntoma | Causa | Corrección | Módulo |
| ---: | :--- | :--- | :--- | :--- |
| 1 | la salida de `ping` apareció en `out.txt`, no en la sesión | hijo de ConPTY hereda los handles redirigidos del padre | `STARTF_USESTDHANDLES` siempre | 18 |
| 2 | `[read returned an error]` en vez de `[EOF ...]` | al terminar el proceso los sockets se cierran con RST | `atexit` que cierra sockets ordenadamente | 20 |
| 3 | "could not execute" no llegaba al cliente | demonio sin stdio: `stdout` atado al fd -2; `freopen` da fd 4 | el padre pasa `NUL` como stdio | 20 |
| 4 | servidor de abduco nunca termina tras attach tardío | cerrar con datos sin leer = RST, el par pierde su cola | `shutdown` + vaciar + `closesocket` | 20 |
| 5 | (evitado) redimensionado de abduco se enviaba tarde | `pselect` entregaba la señal antes de dormir | intercambio de máscara sin entrega | 19 |
| 6 | (evitado) lecturas no bloqueantes mal interpretadas | `EWOULDBLOCK` 140 ≠ `EAGAIN` 11 | igualarlos en `compat.h` | 19 |
| 7 | `nc_getmouse` redefinido al compilar PDCurses | `PDC_NCMOUSE` es para la *aplicación*, la biblioteca define ambas APIs | definirlo sólo en dvtm | 21 §5 |
| 8 | "dvtm no sale cuando cierro la última ventana" | **no era bug**: si el comando no es `$SHELL`, dvtm abre un shell nuevo | la prueba lanza `dvtm` sin argumentos | — |
| 9 | "dtach colgado" con un `cmd.exe` hijo vivo | **no era bug**: sesión huérfana de una corrida abortada; la limpieza borró su socket | ninguna; mismo comportamiento que Unix | — |

Los dos últimos son el recordatorio de siempre: **medir antes de concluir**.
Para el #9, el stack con `cdb` mostraba el hilo principal en
`WaitForMultipleObjects` (un `select()` legítimo esperando clientes) y
`Win32_Process` mostró que su línea de comandos venía de la corrida anterior.

### Herramientas de diagnóstico que pagaron

- **`cdb -p <pid> -c ".symfix; ~*k 10; qd"`**: stacks de todos los hilos de un
  proceso colgado, incluso un demonio sin consola y sin PDB propio. Alcanza ver
  `WSPRecv` (pump), `ReadFile` (pipe de ConPTY), `WaitForMultipleObjects`
  (select) para saber quién espera a quién.
- **`COMPAT_TRACE=archivo`**: la capa escribe su arranque (fds de stdio, rol,
  handle de estado). Un demonio `DETACHED_PROCESS` no tiene otra voz.
- **`Get-CimInstance Win32_Process`** con `ParentProcessId` y `CommandLine`:
  quién lanzó a quién.

## 5. dvtm y PDCurses

- **Backend `wincon`** de PDCursesMod, compilado como target CMake propio,
  **sin** `/FI compat.h` (regla del módulo 08: la capa sólo para upstream).
  `PDC_WIDE` + `PDC_FORCE_UTF8` en ambos; `PDC_NCMOUSE` sólo en dvtm.
- `resizeterm` es una extensión de ncurses; PDCurses la llama `resize_term`.
- **¿Quién lee el teclado?** En dtach/abduco, `read(0)` (la capa traduce
  eventos a UTF-8). En dvtm, `getch()` de PDCurses lee la cola de eventos. El
  `select()` de la capa debe entonces *mirar sin consumir* clics y teclas
  especiales: `compat_console_events_mode(1)` (módulo 19 §3).
- `setlocale(LC_CTYPE, "")` en Windows elige el code page **ANSI** del usuario
  (por ejemplo `Spanish_Argentina.1252`), no UTF-8, y dvtm lo llama en `main()`.
  La capa redirige `""` a `".UTF-8"`; sin eso `mbrtowc` del emulador VT rompe
  cada carácter no ASCII que ConPTY emite.
- `wcwidth`/`wcswidth` no existen en el UCRT: versión compacta del
  `mk_wcwidth` de Markus Kuhn. `wchar_t` es UTF-16, así que lo que está fuera
  del BMP llega en dos mitades.
- dvtm + ConPTY = **dos emuladores en serie**: el hijo habla con conhost, conhost
  re-renderiza, dvtm interpreta ese VT y lo vuelve a dibujar con curses, y si
  corre dentro de abduco hay un tercer conhost entre dvtm y tu terminal. Por eso
  el `TERM` de los hijos describe a conhost (módulo 18 §7).

## 6. Lo que quedó afuera

- `-s`/`-c` de dvtm (FIFOs de estado y de comandos): `mkfifo` devuelve `ENOSYS`.
  Camino posible: named pipes en `\\.\pipe\` con un pump a socket.
- cwd heredado por ventanas nuevas de dvtm: requiere leer el PEB del proceso
  (`NtQueryInformationProcess` + `ReadProcessMemory`).
- Control de trabajos (`Ctrl+Z`): no existe en Windows.
- Modo copia de dvtm (editor con stdin desde pipe): compila (`stdin_fd` en
  `compat_spawn_pty`) pero no tiene prueba.

## 7. Trampas del entorno de trabajo

- En Git Bash, `cmd //c "..."` con comillas mal balanceadas abre un `cmd`
  **interactivo** que espera para siempre. Para llamar `.bat` usá PowerShell
  (`cmd /c "`"$bat`" args"`).
- PowerShell 5.1 guarda el stderr redirigido de un ejecutable nativo en UTF-16
  mezclado con registros de error: no lo uses para verificar resultados. Por eso
  `ptyrun` también escribe `[exit N]` en stdout.
- Un ejecutable en uso no se puede sobrescribir: un demonio vivo bloquea el
  enlazado (`LNK1104`). Matá las sesiones antes de recompilar.
- `build.bat` cayó en dos trampas de batch al buscar Visual Studio:
  `%ProgramFiles(x86)%` dentro de un bloque `if ( ... )` cierra el bloque con su
  `)`, y `for /f` ejecuta su comando con `cmd /c`, que **quita las comillas**
  de una ruta que contiene `( )`. Solución: nada de bloques, y `pushd` al
  directorio de `vswhere.exe` para invocarlo sin ruta. (El aviso
  `'vswhere.exe' is not recognized` que puede quedar lo imprime el propio
  `vcvars64.bat` de Visual Studio cuando el instalador no está en `PATH`; es
  inocuo.)
