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
