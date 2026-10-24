# Módulo 13: Matriz de Porteo, Seguridad y Build Reusable

Este módulo cierra la guía con lo que falta para portear herramientas grandes:
cómo probar, qué riesgos de seguridad aparecen y cómo dejar un build que no
dependa de la máquina de quien lo escribió.

---

## 1. Matriz de pruebas de porteo

Una suite que sólo imprime no es una suite. Para herramientas que tocan
archivos, procesos y red, el mínimo es:

| Área | Casos |
|---|---|
| Paths | `C:\dir`, `C:/dir/`, `\\srv\share`, `~\dir`, relativos, espacios, trailing slash/backslash |
| Unicode | acentos, emoji, nombres con combining marks, code page distinta de 1252 |
| Long paths | > 260 caracteres, `\\?\`, `\\?\UNC\` |
| Nombres | `CON`, `NUL`, `AUX`, nombres terminados en punto/espacio |
| Archivos grandes | > 2 GB, sparse, `ftruncate`, offsets de 64 bits |
| Timestamps | mtime/atime/crtime, precisión, FAT vs NTFS, `UTIME_NOW`/`UTIME_OMIT` |
| Modo binario | `cat`/`tee` con `0x00`, `0x1A`, CRLF; pipe vs consola |
| Permisos | read-only, `chmod`, ACLs, owner/group simulados |
| Links | symlink, junction, hardlink, `lstat`, `readlink` |
| Procesos | `CreateProcess`, herencia de handles, EOF, exit code, Ctrl-C |
| Pipes | pipe roto, `SIGPIPE`, lectura/escritura parcial, no bloqueante |
| Red | `WSAPoll`, `POLLIN`/`POLLOUT`/`POLLHUP`, `WSAStartup`, timeouts |
| Protocolo | contra un peer real (rsync server, sshd), no sólo mock |
| Build | build limpio, sin rutas absolutas, con y sin VCPKG |

Para cada bug encontrado, agregá una **sonda** en `tests/probes/` que fije la
afirmación en código ejecutable. Las sondas del módulo 10 son el modelo:
`probe_broken_pipe.c`, `probe_crt_init.c`, `probe_fts_name.c`.

Sondas que faltan y que este porteo necesitó:

- `_stat64` vs `_stat64i32` con archivo > 2 GB.
- `WSAPoll` con `POLLIN | POLLPRI` (debe fallar o traducirse).
- `_close(-1)` y descriptores inválidos.
- `CreateProcessW` con quoting de espacios/comillas/backslash final.
- `socketpair` loopback + pump threads + EOF.
- `poll` sobre pipe CRT vs socket.

---

## 2. Seguridad: lo que cambia al portear una herramienta de archivos

Los stubs de `pledge`/`unveil` del módulo 06 son aceptables para coreutils de
un usuario, pero **no** para un daemon o un sincronizador que acepta paths de
un peer. Ahí importan:

- **Symlinks y TOCTOU**: un path puede cambiar entre `stat` y `open`. En
  Windows, `FILE_FLAG_OPEN_REPARSE_POINT` evita seguir el último componente;
  `CreateFileW` con `FILE_FLAG_BACKUP_SEMANTICS` permite abrir directorios.
- **Confinamiento**: si el programa sirve un directorio, canonicalizá y
  verificá que el path resuelto quede dentro de la raíz. No alcanza con
  comparar strings (`..`, symlinks, junctions, mayúsculas).
- **Handles compartidos**: abrí archivos con `FILE_SHARE_READ |
  FILE_SHARE_WRITE | FILE_SHARE_DELETE` cuando el programa espere semántica
  POSIX; si no, `DeleteFile`/`rename` fallan con `ERROR_SHARING_VIOLATION`.
- **Antivirus/Defender**: puede abrir el archivo recién creado y bloquear
  `rename`/`delete`. Un `MoveFileEx` con reintento suele ser necesario.
- **ACLs**: las ACLs Windows no son POSIX. Mapear sólo permisos básicos o usar
  `--fake-super`/sidecar; no prometas fidelidad total.
- **xattrs**: no hay xattrs POSIX nativos. ADS es una opción no interoperable.
- **`pledge`/`unveil`**: si la herramienta corre como servicio, los stubs
  eliminan una defensa real. Implementá confinamiento con AppContainer,
  restricted token o canonicalización estricta.

Si el objetivo es “subir archivos a un VPS por SSH”, el modelo de confianza es
más simple: el cliente local es del usuario y el servidor es Unix. Aun así,
no aceptes paths del peer sin canonicalizar.

---

## 3. Build reusable

Reglas para que el repo no dependa de tu máquina:

1. **No hardcodees rutas de VCPKG** (`C:/Users/.../vcpkg`). Usá
   `VCPKG_ROOT`, `find_path`/`find_library` o vendorizá dependencias
   header-only.
2. **No hardcodees Visual Studio** (rutas absolutas de tu máquina,
   `Visual Studio 18 2026`). Usá `vswhere`/`vcvars64.bat` y dejá que CMake
   elija el generador; permití `-G` por parámetro.
3. **Usá `target_include_directories`, `target_compile_options`,
   `target_link_libraries`**, no `include_directories` global.
4. **`bsd_compat` debe ser OBJECT library**, no STATIC, para no perder
   `.CRT$XCU` (módulos 03 y 10).
5. **Vendorizá `dirent`** y cualquier header-only MIT/BSD. Es más simple que
   explicar VCPKG y evita rutas de usuario.
6. **No fijes el compilador en el cache**. Si cambiás de VS, borrá `build/`;
   CMake cachea el path absoluto de `cl.exe`.
7. **Compilá también en Debug/RelWithDebInfo** para tener PDB y poder usar
   `cdb.exe` cuando algo cuelga.
8. **CI**: un job Windows con `cmake -B build -S . -A x64`, build Release, y
   `ctest`/`tests/run-suite.sh` (o un equivalente PowerShell que compare bytes,
   no que sólo imprima).

---

## 4. Trampas que ya costaron horas

- Cabecera del proyecto tapando una del SDK (`io.h`, `config.h`). Ver módulo 04.
- Macro object-like que rompe un tag (`struct stat64`, `struct sigaction`,
  `struct flock`). Ver módulo 04.
- `#define st_blksize st_rdev`: `cat`/`ls` leen el device number como tamaño de
  bloque. Ver módulo 05.
- `_stat64i32`: archivos > 2 GB truncados. Ver módulo 05.
- `WSAPoll` con `POLLPRI`: `WSAEINVAL`. Ver módulo 07.
- `_close(-1)` colgando por el invalid parameter handler. Ver módulo 04.
- `read()`/`write()` sobre un `SOCKET` envuelto con `_open_osfhandle`: no
  funciona; hay que usar `recv`/`send`. Ver módulo 07.
- `cmd.exe` + `\"` al final de un path: se come la comilla. Ver módulo 12.
- Forced include aplicado a librerías de terceros: necesitan
  `HAVE_CONFIG_H` y pueden romperse con macros de libc. Ver módulo 04.

---

## 5. Checklist final de porteo

- [ ] ¿El proyecto compila desde cero sin rutas absolutas?
- [ ] ¿`bsd_compat` es OBJECT library?
- [ ] ¿El modo binario está activo en pipes/redirecciones?
- [ ] ¿`_stat64`/`off_t` son de 64 bits?
- [ ] ¿Los paths Windows se normalizan antes de `host:path`?
- [ ] ¿Los paths remotos quedan intactos?
- [ ] ¿El quoting de `CreateProcessW` está probado con espacios y quotes?
- [ ] ¿`poll`/`WSAPoll` traduce eventos y no recibe `POLLPRI`?
- [ ] ¿Los handles se heredan/cierran con un dueño claro?
- [ ] ¿Hay EOF y timeout en pipes/sockets?
- [ ] ¿Se espera el proceso hijo y se propaga el exit code?
- [ ] ¿Las pruebas comparan bytes, no sólo imprimen?
- [ ] ¿Los stubs de seguridad están documentados como tales?
- [ ] ¿El README dice qué funciona y qué no?
