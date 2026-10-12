# Módulo 05: Sistema de Archivos, Rutas y Recorrido de Directorios (`dirent`)

## 1. Divergencias en la Sintaxis de Rutas

En el mundo Unix / OpenBSD:
- La jerarquía de archivos comienza en una única raíz absoluta: `/`.
- El único separador de directorios válido es la barra inclinada hacia adelante (`/`).
- Las rutas son estrictamente sensibles a mayúsculas y minúsculas (*case-sensitive*).

En Windows:
- Cada unidad física o lógica posee su propia raíz (`C:\`, `D:\`).
- Se admiten dos separadores: la barra invertida (`\`) canónica de DOS/Windows y la barra inclinada (`/`).
- El sistema de archivos predeterminado (NTFS) preserva mayúsculas y minúsculas pero es indiferente a ellas (*case-preserving, case-insensitive*).
- Existen rutas UNC de red (`\\servidor\recurso`) y rutas de dispositivo (`\\?\C:\...`).

> [!WARNING]
> **Colisiones por Insensibilidad a Mayúsculas en NTFS**:
> Al clonar repositorios de Unix como OpenBSD o el kernel de Linux en Windows, git puede advertir colisiones si existen archivos en el mismo directorio cuyos nombres difieren únicamente por mayúsculas (por ejemplo, `Makefile` y `makefile`, o directorios `T/` y `t/`). Es esencial configurar `core.protectNTFS = true` en git o activar el atributo de sensibilidad a mayúsculas por directorio en NTFS mediante `fsutil.exe file setCaseSensitiveInfo <directorio> enable`.

---

## 2. Recorrido de Directorios: De K&R a `tronkko/dirent` y `fts(3)`

### 2.1. La Interfaz POSIX `dirent.h`
En Unix, recorrer directorios se basa en cuatro funciones estandarizadas por POSIX.1:
- `DIR *opendir(const char *name);`
- `struct dirent *readdir(DIR *dirp);`
- `void rewinddir(DIR *dirp);`
- `int closedir(DIR *dirp);`

El runtime de MSVC **no** incluye `<dirent.h>`. Para habilitarlo nativamente sin dependencias pesadas tipo Cygwin o MinGW, integramos el port de [tronkko/dirent](https://github.com/tronkko/dirent) mediante **VCPKG**:
```cmd
C:\Users\Shifty\DEV\vcpkg\vcpkg.exe install dirent:x64-windows
```
Esta biblioteca traduce las llamadas `opendir/readdir` directamente a las llamadas nativas de Win32:
`FindFirstFileW` / `FindNextFileW` y `FindClose`.

### 2.2. La Jerarquía Completa: `fts(3)` en `ls(1)`
Mientras que herramientas simples recorren un solo directorio con `opendir`, utilidades avanzadas como `ls -R` (recursivo) emplean la API BSD `fts(3)`:
- `fts_open()`: Inicializa el árbol a partir de un arreglo de rutas y una función de comparación de ordenamiento.
- `fts_read()`: Retorna cada nodo en recorrido en profundidad (DFS) tanto al entrar (`FTS_D`) como al salir (`FTS_DP`).
- `fts_children()`: Obtiene la lista enlazada de todos los hijos directos de un directorio.
- `fts_set()`: Permite instruir al motor omitir un subárbol (`FTS_SKIP`).

En nuestra biblioteca `bsd_compat`, implementamos una versión autónoma de `fts(3)` (`compat/src/fts_compat.c`) construida sobre `dirent.h` y `stat`, permitiendo compilar el `ls.c` original de OpenBSD sin requerir ninguna modificación.

---

## 3. Metadatos de Archivos: La Estructura `stat`

En UNIX moderno, `struct stat` almacena marcas de tiempo con precisión de nanosegundos mediante `struct timespec`:
```c
struct timespec st_atim;  /* Último acceso */
struct timespec st_mtim;  /* Última modificación */
struct timespec st_ctim;  /* Último cambio de estado/inodo */
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
```
Además, provee `st_blksize` (tamaño óptimo de bloque de E/S, típicamente 4096 o 65536) y `st_blocks` (cantidad de bloques de 512 bytes asignados).

En MSVC CRT (`<sys/stat.h>`):
- `struct _stat64i32` o `struct _stat64` solo tiene campos enteros simples (`time_t st_atime;`).
- Carece totalmente de nanosegundos (`st_atim`), de `st_blksize` y de `st_blocks`.

### La Abstracción `struct bsd_stat`
En `compat/include/sys/stat.h`, definimos una estructura que cumple al 100% con la especificación de OpenBSD:
```c
struct bsd_stat {
    dev_t           st_dev;
    ino_t           st_ino;
    unsigned short  st_mode;
    short           st_nlink;
    short           st_uid;
    short           st_gid;
    dev_t           st_rdev;
    int64_t         st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long            st_blksize;
    long            st_blocks;
    unsigned int    st_flags;
    unsigned int    st_gen;
};

#define stat bsd_stat
#define fstat bsd_fstat
```
La función `bsd_stat()` llama a `_stat64i32()`, calcula los bloques (`(st_size + 511) / 512`), fija un tamaño óptimo de bloque de 64 KB y rellena los nanosegundos a cero si no se dispone de precisión submilisegundo.

---

## 4. Modificación de Tiempos: `touch(1)` y `utimensat`

OpenBSD `touch` utiliza `futimens()` o `utimensat()` con la constante especial `UTIME_NOW` y `UTIME_OMIT`. En Windows, estas llamadas no existen en la CRT.

En `compat/src/utimens_compat.c`, mapeamos `futimens` y `utimensat` directamente a la API de Win32 `SetFileTime`:

```c
static void timespec_to_filetime(const struct timespec *ts, FILETIME *ft) {
    /* Época de Windows inicia el 1 de enero de 1601 */
    /* Diferencia con época Unix (1970) = 116444736000000000 intervalos de 100ns */
    ULARGE_INTEGER uli;
    uli.QuadPart = ((ULONGLONG)ts->tv_sec * 10000000ULL) +
                   ((ULONGLONG)ts->tv_nsec / 100ULL) +
                   116444736000000000ULL;
    ft->dwLowDateTime = uli.LowPart;
    ft->dwHighDateTime = uli.HighPart;
}

int futimens(int fd, const struct timespec times[2]) {
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    FILETIME atim, mtim;
    timespec_to_filetime(&times[0], &atim);
    timespec_to_filetime(&times[1], &mtim);
    return SetFileTime(hFile, NULL, &atim, &mtim) ? 0 : -1;
}
```
Esto permite a `touch.exe` crear archivos y actualizar marcas de tiempo de modificación y acceso con total precisión en NTFS.
