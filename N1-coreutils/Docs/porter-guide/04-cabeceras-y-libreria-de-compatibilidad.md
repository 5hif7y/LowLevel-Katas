# Módulo 04: Cabeceras y la Biblioteca de Compatibilidad (`bsd_compat`)

## 1. El Desafío de las Cabeceras Faltantes en MSVC

Al compilar código de OpenBSD o sistemas Unix-like directamente con Microsoft Visual C++ (MSVC), el compilador se detiene inmediatamente ante la ausencia de cabeceras fundamentales de la API estándar de UNIX y de extensiones BSD:

| Cabecera Unix / BSD | Estado en MSVC CRT | Rol en OpenBSD Coreutils | Solución en `bsd_compat` |
| :--- | :--- | :--- | :--- |
| `<err.h>` | Inexistente | Manejo estándar de errores y salida (`err`, `warn`, `errx`, `warnx`) | Implementado sobre `stderr`, `strerror` y `exit()` |
| `<unistd.h>` | Inexistente | Constantes de descriptores (`STDIN_FILENO`), `getopt`, `sleep`, `isatty` | Creado como wrapper sobre `<io.h>`, `<direct.h>` y `Sleep()` |
| `<sys/cdefs.h>` | Inexistente | Atributos del compilador (`__dead`, `__BEGIN_DECLS`, `__pure`) | Mapeado a `__declspec(noreturn)` y macros MSVC |
| `<libgen.h>` | Inexistente | Manipulación de nombres de ruta (`basename`, `dirname`) | Implementado tolerante a `/` y `\` |
| `<strings.h>` | Inexistente | Comparación sin distinción de mayúsculas (`strcasecmp`) | Mapeado a `_stricmp` y `_strnicmp` |
| `<sys/time.h>` | Inexistente | Estructuras de alta precisión temporal (`struct timespec`, `nanosleep`) | Implementado sobre Win32 `Sleep()` y `<time.h>` |
| `<fts.h>` | Inexistente | Recorrido jerárquico de árboles de directorios en `ls` | Implementado de forma autónoma sobre `dirent.h` y `stat` |
| `<sys/queue.h>` | Inexistente | Macros de listas y colas enlazadas BSD (usadas por `tee`) | Portado directamente desde `openbsd-src/sys/sys/queue.h` |

---

## 2. La Técnica de "Forced Include" (`/FI`) de MSVC

En lugar de modificar docenas de archivos de código fuente upstream para agregar `#include "compat.h"`, MSVC proporciona la bandera de línea de comandos **/FI (Forced Include)**:

```cmake
# CMakeLists.txt
if(MSVC)
    add_compile_options(
        /FI "${CMAKE_CURRENT_SOURCE_DIR}/compat/include/compat.h"
    )
endif()
```

Esta opción instruye al preprocesador de MSVC a procesar `compat.h` antes de la primera línea de cualquier archivo `.c` del proyecto. De este modo:
- Todas las declaraciones de funciones auxiliares (`strlcpy`, `strlcat`, `strtonum`, `getprogname`, `fseeko`) quedan disponibles globalmente.
- Los tipos y constantes (`howmany`, `DEFFILEMODE`, `PATH_MAX`) se definen de manera uniforme.
- El código original de OpenBSD en `openbsd-src/` permanece **100% intacto**.

---

## 3. Implementaciones Emblemáticas de OpenBSD

### 3.1. `strlcpy` y `strlcat` (Todd C. Miller)
Diseñadas originalmente por Todd C. Miller y Theo de Raadt en OpenBSD para erradicar las vulnerabilidades de desbordamiento de búfer asociadas a `strcpy` y `strncpy`:

```c
/* compat/src/strlcpy.c */
size_t strlcpy(char *dst, const char *src, size_t dsize) {
    const char *osrc = src;
    size_t nleft = dsize;

    if (nleft != 0) {
        while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0')
                break;
        }
    }
    if (nleft == 0) {
        if (dsize != 0)
            *dst = '\0';
        while (*src++) ;
    }
    return (size_t)(src - osrc - 1);
}
```
A diferencia de `strncpy` de la ISO C (que no garantiza la terminación NUL si la cadena origen es igual o mayor que el búfer de destino, y que rellena con ceros ineficientemente el resto), `strlcpy` siempre garantiza la terminación NUL y devuelve la longitud total que la cadena habría tenido, facilitando la detección de truncamiento.

---

### 3.2. `strtonum` (Ted Unangst y Todd C. Miller)
El parser numérico canónico de OpenBSD que reemplaza de forma segura a `atoi` (sin control de error) y `strtol` (con una interfaz compleja y proclive a errores de comprobación de `errno` y punteros de fin):

```c
/* Uso en OpenBSD head.c */
linecnt = strtonum(optarg, 1, LONG_MAX, &errstr);
if (errstr != NULL)
    errx(1, "count is %s: %s", errstr, optarg);
```
Si el número no es válido, excede el rango superior o inferior, `strtonum` devuelve `0` y fija `errstr` a `"invalid"`, `"too large"` o `"too small"`.

---

### 3.3. Resolución de Colisiones de Nombres: El Caso de `setmode`

En el runtime de MSVC (`<io.h>`), existe una función obsoleta:
```c
_CRTIMP int __cdecl setmode(int _FileHandle, int _Mode);
```
En OpenBSD (`mkdir`, `chmod`), `setmode()` tiene una signatura y propósito completamente distintos:
```c
void *setmode(const char *mode_str);
mode_t getmode(const void *set, mode_t omode);
```
Si se declara `void *setmode(const char *)` directamente, MSVC emite el error `C2040: 'setmode': differs in levels of indirection from 'int (int,int)'` y `C2491: definition of dllimport function not allowed`.

**La Solución Elegante**:
1. Incluir `<io.h>` primero para que el CRT declare su `setmode(int, int)`.
2. Redefinir la macro para redirigir las llamadas de OpenBSD a una función con prefijo:
```c
/* En compat/include/compat.h */
void *bsd_setmode(const char *mode_str);
mode_t bsd_getmode(const void *set, mode_t mode);
#define setmode bsd_setmode
#define getmode bsd_getmode
```
3. Implementar `bsd_setmode()` en `compat/src/setmode.c`. De esta forma, las utilidades de OpenBSD llaman a `bsd_setmode()` sin necesidad de alterar una sola línea de sus archivos fuente.

---

## 4. Colisiones de nombres de cabeceras

El `Forced Include` resuelve el problema de no tocar el upstream, pero también
introduce una trampa: **el directorio del proyecto puede tapar una cabecera del
sistema**.

Caso real, portando rsync: `compat.h` hacía

```c
#include <io.h>
```

y el proyecto tenía su propio `io.h` en la raíz. Como CMake pasaba `/I.` antes
que el SDK, MSVC incluía **el `io.h` de rsync** en lugar del `io.h` de la UCRT.
El resultado eran errores absurdos en `read_varint30`, porque el `io.h`
equivocado no declaraba nada de lo que el CRT espera. La solución fue usar la
ruta explícita de la UCRT:

```c
#include <../ucrt/io.h>
```

Reglas prácticas:

- Nunca pongas en la raíz del proyecto cabeceras con nombres del sistema
  (`io.h`, `config.h`, `string.h`, `compat.h`).
- Si el upstream las tiene, aislalas en un subdirectorio y poné ese
  subdirectorio **después** de las cabeceras del SDK en el include path.
- Para las cabeceras del CRT que chocan con nombres del proyecto, usá la ruta
  explícita `<../ucrt/...>` o una copia con prefijo.
- `compat.h` no debería incluir cabeceras ambiguas con `<>` si el proyecto
  puede tener un archivo homónimo.

## 5. Macros que también son tags o funciones

`#define stat bsd_stat` funciona para el código de OpenBSD, pero se vuelve
peligroso cuando el nombre también existe como **tag de struct** o como
**función**:

```c
#define stat64 win32_stat64   /* rompe: struct stat64 -> struct win32_stat64 */
#define sigaction win32_sigaction /* rompe: struct sigaction -> struct win32_sigaction */
#define flock win32_flock     /* rompe: struct flock -> struct win32_flock */
```

Síntomas típicos:

```text
error C2079: 'st' uses undefined struct 'win32_stat64'
error C2079: 'sigact' uses undefined struct 'win32_sigaction'
error C2079: 'lock' uses undefined struct 'win32_flock'
```

Opciones, de mejor a peor:

1. **Función real** con el nombre estándar (`int sigaction(...)`), después de
   `#undef` si el SDK ya lo declara.
2. **Macro function-like**, que sólo expande cuando va seguido de `(`:

   ```c
   #define sigaction(...) win32_sigaction(__VA_ARGS__)
   #define flock(...)     win32_flock(__VA_ARGS__)
   ```

   Así `struct sigaction` y `struct flock` quedan intactos.
3. Macro object-like sólo si el nombre **no** es un tag ni una función que el
   código necesite declarar.

En rsync, `#define stat stat64` fue intencional (queríamos que `struct stat`
fuera nuestro `struct stat64`), pero `stat64`, `sigaction` y `flock` tuvieron
que ser function-like. La regla es: **si el nombre aparece en `struct <nombre>`,
no uses macro object-like**.

## 6. `_close(-1)` y descriptores inválidos

El CRT de MSVC no garantiza que cerrar un descriptor inválido sea inocuo. En un
caso real, `piped_child()` cerraba dos descriptores que ya habían sido
“detachados” y quedaban en `-1`; el `_close(-1)` disparó el *invalid parameter
handler* y colgó el proceso. La solución es explícita:

```c
int win32_close(int fd)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    ...
}
```

Regla: después de mover/duplicar handles, poné el fd original en `-1` y
**nunca** lo cierres otra vez. Validá `fd < 0` en todos los wrappers.

## 7. El `Forced Include` también afecta a librerías de terceros

`/FI compat.h` se aplica a **todas** las unidades de traducción, incluidas las
librerías embebidas (popt, zlib, etc.). Dos consecuencias:

- Si la librería incluye su propio `config.h`, necesita `HAVE_CONFIG_H` para
  encontrar la configuración correcta.
- Los macros de `compat.h` (`fopen`, `fclose`, `signal`, `open`, ...) también
  afectan a la librería. Si la librería declara o toma la dirección de esas
  funciones, el macro puede romperla.

Recomendación: mantené los wrappers de libc en un header separado y aplicá
`/FI` sólo a las fuentes upstream que lo necesitan, o compilá las librerías de
terceros como targets separados sin el `Forced Include`.
