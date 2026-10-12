# Módulo 09: Estudios de Caso: Utilidad por Utilidad

Este módulo analiza los desafíos técnicos de porteo, el código fuente original y las soluciones adoptadas para cada una de las 21 utilidades de OpenBSD portadas a Windows nativo, contrastando el diseño modular de OpenBSD con la aproximación monolítica de BusyBox.

---

## 1. `cat` (`bin/cat/cat.c`)
- **Desafío Técnico**: En OpenBSD, `cat` finaliza con `if (fclose(stdout)) err(1, "stdout");`. En Windows UCRT, llamar a `fclose` sobre `stdout` devuelve `EOF` con `errno = EINVAL`. Además, `cat` utiliza `st_blksize` para dimensionar el búfer de E/S óptimo.
- **Solución en `bsd_compat`**: 
  - `compat_fclose(fp)`: Si `fp == stdout || fp == stderr || fp == stdin`, ejecuta `fflush(fp)` en lugar de cerrar el descriptor.
  - `struct bsd_stat` provee `st_blksize = 65536`.
- **Contraste con BusyBox**: En `busybox/coreutils/cat.c`, la función está envuelta en un applet que utiliza `bb_copyfd_eof()`. En OpenBSD, `cat.c` implementa un `raw_cat()` de alto rendimiento con llamadas directas a `read()` y `write()`, y un `cook_buf()` para el formateo de saltos de línea y numeración (`-n`, `-b`, `-v`).

---

## 2. `wc` (`usr.bin/wc/wc.c`)
- **Desafío Técnico**: `wc` utiliza `getline()` para leer entradas de longitud arbitraria y `fmt_scaled()` (`<util.h>`) para la opción `-h` (formato legible por humanos en KB/MB/GB).
- **Solución en `bsd_compat`**:
  - Implementación de `getline()` estándar POSIX en `compat/src/getline.c`.
  - Porteo canónico de `fmt_scaled()` y `scan_scaled()` en `compat/src/fmt_scaled.c`.
- **Contraste con BusyBox**: En BusyBox, `wc` reduce la máquina de estados de K&R al mínimo para ahorrar código. En OpenBSD, `wc` soporta tanto streams byte a byte como lectura multibyte con `mbrtowc()` para contar columnas de caracteres UTF-8 correctamente.

---

## 3. `head` (`usr.bin/head/head.c`)
- **Desafío Técnico**: Utiliza `strtonum()` de OpenBSD para validar el argumento `-n <lineas>` o `-c <bytes>`.
- **Solución en `bsd_compat`**:
  - Implementación canónica de `strtonum()` de OpenBSD adaptada con `_strtoi64()` para soportar enteros de 64 bits en Windows x64.
- **Contraste con BusyBox**: BusyBox utiliza macros auxiliares `xatoul_range()`. OpenBSD usa `strtonum()`, considerado el estándar de oro en parsing numérico defensivo.

---

## 4. `tail` (`usr.bin/tail/`)
- **Desafío Técnico**: 
  - Es una utilidad multifuente (`forward.c`, `read.c`, `reverse.c`, `misc.c`, `tail.c`).
  - Para la lectura hacia atrás (`tail -r`), utiliza `fseeko()` con desplazamientos de 64 bits.
  - Para la gestión de memoria dinámica de líneas recientes, utiliza `recallocarray()` de Todd C. Miller.
  - Para el seguimiento en tiempo real (`tail -f`), utiliza la API de eventos del kernel de BSD: `kqueue(2)` (`sys/event.h`).
- **Solución en `bsd_compat`**:
  - Mapeo de `fseeko` a `_fseeki64` y `ftello` a `_ftelli64`.
  - Implementación de `recallocarray()` en `compat/src/id_compat.c`.
  - Stub funcional de `kqueue()` y `kevent()` en `compat/src/event_compat.c` que emula el ciclo de polling mediante intervalos temporales de Win32.
- **Contraste con BusyBox**: BusyBox implementa `tail` en un único archivo de 200 líneas sin soporte de `kqueue`. OpenBSD ofrece una arquitectura desacoplada donde `reverse.c` maneja archivos que caben o no en memoria de forma transparente.

---

## 5. `ls` (`bin/ls/`)
- **Desafío Técnico**: La utilidad más compleja del conjunto (`cmp.c`, `ls.c`, `main.c`, `print.c`, `utf8.c`, `util.c`):
  - Recorrido de árboles con `fts(3)` (`<fts.h>`).
  - Obtención de dimensiones de la terminal para formateo en columnas mediante `ioctl(fd, TIOCGWINSZ, &ws)`.
  - Traducción de IDs de usuario y grupo a nombres mediante `user_from_uid()` y `group_from_gid()`.
  - Formateo de permisos con `strmode()`.
  - Detección de ancho de glifos UTF-8 mediante `wcwidth()`.
- **Solución en `bsd_compat`**:
  - Implementación de `fts(3)` autónoma sobre `dirent.h` y `stat` (`compat/src/fts_compat.c`).
  - `ioctl(TIOCGWINSZ)` implementado con la API de consola de Windows `GetConsoleScreenBufferInfo`.
  - `user_from_uid` mapeado al usuario actual de Windows mediante `GetUserNameA` (Advapi32).
  - `strmode()` portado de OpenBSD libc.
- **Contraste con BusyBox**: BusyBox `ls` utiliza una ordenación simple por burbuja/qsort en memoria y `readdir`. OpenBSD `ls` utiliza la biblioteca `fts` con ordenación secundaria personalizable y manejo estricto de enlaces simbólicos.

---

## 6. `tr` (`usr.bin/tr/`)
- **Desafío Técnico**: Utilidad multifuente (`tr.c`, `str.c`) que procesa secuencias de caracteres, rangos (`[a-z]`), clases de caracteres (`[:alnum:]`) y repeticiones. Utiliza tipos clásicos BSD (`u_char`) y macros de memoria tradicionales (`bzero`, `bcopy`).
- **Solución en `bsd_compat`**:
  - Definición de `u_char`, `u_short`, `u_int`, `u_long` en `compat/include/sys/types.h`.
  - Mapeo de `bzero` a `memset` y `bcopy` a `memmove` en `compat/include/compat.h`.

---

## 7. `tee` (`usr.bin/tee/tee.c`)
- **Desafío Técnico**: Requiere la cabecera `<sys/queue.h>` para manejar listas enlazadas simples (`SLIST_ENTRY`, `SLIST_HEAD`, `SLIST_FOREACH`) donde almacena la lista de descriptores de salida.
- **Solución en `bsd_compat`**:
  - Porteo directo de `openbsd-src/sys/sys/queue.h` a `compat/include/sys/queue.h`, junto con `sys/_null.h` para compatibilidad limpia con C++ y C11.

---

## 8. `touch` (`usr.bin/touch/touch.c`)
- **Desafío Técnico**: Admite marcas de tiempo en formatos POSIX y llamadas a `utimensat()` / `futimens()` con `struct timespec`.
- **Solución en `bsd_compat`**:
  - Conversión bidireccional entre `struct timespec` (época Unix 1970) y `FILETIME` de Windows (época 1601 en unidades de 100ns).
  - Implementación de `strptime()` para procesar fechas de línea de comandos en formatos `%Y%m%d%H%M.%S`.

---

## 9. `basename` y `dirname` (`usr.bin/basename/`, `usr.bin/dirname/`)
- **Desafío Técnico**: Las implementaciones clásicas de Unix solo reconocen `/` como separador. En Windows, las rutas pueden contener barras invertidas (`\`) y prefijos de unidad (`C:`).
- **Solución en `bsd_compat`**:
  - Implementación robusta en `compat/src/libgen_compat.c` que analiza ambos separadores (`/` y `\`) simultáneamente, preservando la compatibilidad con rutas de Windows y rutas Unix.

---

## 10. `printf` (`usr.bin/printf/printf.c`)
- **Desafío Técnico**: Utiliza `warnc(code, fmt, ...)` de OpenBSD para reportar errores basados en códigos de error explícitos en lugar de depender únicamente de la variable global `errno`.
- **Solución en `bsd_compat`**:
  - Implementación de `warnc()`, `vwarnc()`, `errc()` y `verrc()` en `compat/src/err.c`.

---

## 11. `cut` (`usr.bin/cut/cut.c`)
- **Desafío Técnico**: Depende de la constante POSIX `_POSIX2_LINE_MAX` (2048 bytes) para dimensionar buffers de corte de columnas.
- **Solución en `bsd_compat`**:
  - Definición de `_POSIX2_LINE_MAX` y `LINE_MAX` en `compat/include/compat.h`.

---

## 12. `mkdir` y `rmdir` (`bin/mkdir/`, `bin/rmdir/`)
- **Desafío Técnico**:
  - `mkdir` de Unix toma dos argumentos: `int mkdir(const char *path, mode_t mode)`. En el CRT de Windows (`<direct.h>`), `_mkdir` toma solo un argumento: `int _mkdir(const char *path)`.
  - Soporte de `-m <modo>` con `setmode()` y `getmode()`.
- **Solución en `bsd_compat`**:
  - Función inline `compat_mkdir(path, mode)` que ignora el modo octal no aplicable a NTFS y llama a `_mkdir(path)`.
  - Implementación de `bsd_setmode()` y `bsd_getmode()` para parsear modos numéricos como `0755` o `777`.

---

## 13. `pwd`, `sleep`, `yes`, `true`, `false`, `echo`, `uniq`
- **`pwd`**: Utiliza `getcwd()` mapeado limpiamente a `_getcwd()`.
- **`sleep`**: Soporta argumentos fraccionarios (`sleep 0.5`) utilizando `nanosleep()` implementado sobre Win32 `Sleep()`.
- **`yes`**: Bucle infinito de escritura directa sobre `STDOUT_FILENO` optimizado para buffers de consola.
- **`true` y `false`**: Retornan códigos de salida `0` y `1` respectivamente.
- **`echo`**: Recorrido directo de `argv` sin `getopt` respetando la semántica POSIX.
- **`uniq`**: Conteo y deduplicación de líneas continuas con soporte para ignorar mayúsculas mediante `strcasecmp` mapeado a `_stricmp`.

---

## 14. Resumen de Ejecutables Generados

Todos los binarios generados se encuentran en `build\bin\Release\`:

```
basename.exe (11.7 KB)   cat.exe      (14.3 KB)   cut.exe      (16.3 KB)
dirname.exe  (11.7 KB)   echo.exe     (11.7 KB)   false.exe    ( 9.7 KB)
head.exe     (13.8 KB)   ls.exe       (30.2 KB)   mkdir.exe    (13.8 KB)
printf.exe   (15.3 KB)   pwd.exe      (12.8 KB)   rmdir.exe    (11.7 KB)
sleep.exe    (12.8 KB)   tail.exe     (22.5 KB)   tee.exe      (12.2 KB)
touch.exe    (17.9 KB)   tr.exe       (17.9 KB)   true.exe     ( 9.7 KB)
uniq.exe     (15.8 KB)   wc.exe       (16.8 KB)   yes.exe      (10.7 KB)
```
*Total combinado de las 21 utilidades: ~310 KB.*
