# Procedencia del código upstream

`upstream/` **no se versiona**: se reconstruye pinneado con `fetch-upstream.ps1` /
`fetch-upstream.sh`. Lo que sí se versiona es la copia parcheada en `ports/<programa>/`
y, al lado, `WIN32.diff` con el delta exacto contra el commit de abajo.

A diferencia de N1 (overlay puro, cero cambios en los `.c` de OpenBSD), acá el
porteo **no puede** ser 100 % out-of-tree: `fork()` no se emula, y los tres
programas tienen una rama "hijo" después de `fork()`/`forkpty()` que hay que
reescribir. Los cambios quedan acotados con `#ifdef _WIN32` y comentados con
`WIN32:`; todo lo demás vive en `compat/`.

| Programa | Repositorio | Commit | Fecha | Licencia | Delta (`WIN32.diff`) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| dtach | <https://github.com/crigler/dtach> | `b027c27b2439081064d07a86883c8e0b20a183c9` | 2025-06-20 | GPL-2.0+ | +119 / −0 (+`config.h`) |
| abduco | <https://github.com/martanne/abduco> | `8c32909a159aaa9484c82b71f05b7a73321eb491` | 2020-04-30 | ISC | +129 / −1 (+`config.h`) |
| dvtm | <https://github.com/martanne/dvtm> | `7bcf43f8dbd5c4a67ec573a1248114caa75fa3c2` | 2021-03-09 | MIT | +314 / −48 (incluye `config.h` = `config.def.h`) |
| PDCursesMod | <https://github.com/Bill-Gray/PDCursesMod> | `8df2949d368f21e290a25280bdd5f826f6c9f648` | 2026-11-14 | dominio público | sin cambios: se compila tal cual (backend `wincon`) |

Las licencias originales se conservan en `ports/*/COPYING|LICENSE`.

## Forks publicados

Cada port vive también como fork autocontenido, en la rama `port/windows`
(creada desde el commit pinneado de arriba). Los fuentes parcheados están en la
raíz, como en upstream, y todo lo de Windows en `win32/`: la capa `compat`, el
CMake, `build.bat`, `tests/` y un README. Así se compila sin esta kata.

| Fork | Rama | Commit | Pruebas propias |
| :--- | :--- | :--- | :--- |
| <https://github.com/5hif7y/dtach> | `port/windows` | `96c846d` | 11/11 |
| <https://github.com/5hif7y/abduco> | `port/windows` | `1e5ee8b` | 10/10 |
| <https://github.com/5hif7y/dvtm> | `port/windows` | `f186d55` | 5/5 (PDCursesMod vía `FetchContent`, pinneado) |

Clones locales: `DEV/gitclones/<programa>`, con remotos `origin` = upstream y
`fork` = el fork. **`ports/` de esta kata es la fuente:** si cambia un parche o
la capa `compat/`, hay que regenerar el `win32/` del fork correspondiente.

## Qué se tocó en cada programa

**dtach** (`ports/dtach`)
- `master.c`: `init_pty()` usa `compat_spawn_pty()` (ConPTY) en vez de `forkpty()+execvp()`.
- `master.c`: el `fork()` de demonización se reemplaza por `win32_spawn_master()`, que
  re-ejecuta el mismo `.exe` desacoplado de la consola; el pipe de estado reemplaza al
  pipe `FD_CLOEXEC`.
- `master.c`: método de redibujado por defecto `winch` (ConPTY repinta al redimensionar;
  `^L` se tipearía dentro de `cmd.exe`).
- `main.c`: el proceso re-ejecutado detecta su rol y va directo a `master_main()`.
- `config.h`: reemplaza la salida de `configure`.

**abduco** (`ports/abduco`)
- `create_session()`: el doble `fork()` + `forkpty()` se reemplaza por re-ejecución
  (`win32_server_main()`); el tamaño de ventana viaja en `ABDUCO_WIN32_WINSIZE`.
- VLA `char buf[maxlen]` → array de tamaño constante.
- Rutas absolutas `C:\...`, nombre `abduco.exe` → `abduco`, `%COMSPEC% /c` en vez de `/bin/sh -c`.
- `config.h`: `USERPROFILE`/`LOCALAPPDATA` como directorio de sockets si no hay `HOME`.

**dvtm** (`ports/dvtm`)
- `\e` → `\033` (38 apariciones) y `case 30 ... 37:` → etiquetas explícitas (extensiones GNU).
- `debug(format, args...)` → `debug(...)`; dos VLA; inicializador vacío `keytable[] = {}`.
- `vt_forkpty()` usa `compat_spawn_pty()`; `TERM=xterm-256color` para los hijos.
- `vt_process()`: `read()==0` en el master se traduce a `EIO` (semántica Linux).
- Shell por defecto `%COMSPEC%`; `/proc/<pid>/cwd` no existe → `NULL`.
- `compat_console_events_mode(1)`: `select()` no debe consumir eventos que lee PDCurses.
