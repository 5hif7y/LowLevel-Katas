# Módulo 02: Diferencias Arquitectónicas entre UNIX/POSIX y Windows (Win32 / NT)

## 1. Dos Filosofías de Diseño del Sistema Operativo

Para portear software escrito en C para sistemas Unix-like (como OpenBSD o Linux) hacia Windows utilizando el compilador MSVC, es fundamental comprender las discrepancias a nivel de arquitectura y modelo de ejecución entre ambos mundos.

```
       +---------------------------------------------+
       |             Aplicación en C                 |
       +---------------------------------------------+
              |                               |
       (Llamadas POSIX)               (Win32 / UCRT)
              v                               v
+-----------------------------+ +-----------------------------+
|    Unix Kernel (OpenBSD)    | | Windows Subsystem (kernel32)|
| - Monolítico                | | - Capa de abstracción Win32 |
| - Todo es un descriptor/fd  | | - Objetos NT / HANDLEs      |
| - fork() + execve()         | | - CreateProcess()           |
| - Unlink en uso permitido   | | - Bloqueo de archivos abiertos
+-----------------------------+ +-----------------------------+
```

---

## 2. Modelo de Procesos: `fork` / `exec` vs `CreateProcess`

### 2.1. El Enfoque UNIX (OpenBSD)
En UNIX, la creación de un nuevo proceso se bifurca en dos operaciones atómicas:
1. **`fork()`**: Clona el proceso actual por completo (copia en escritura del espacio de memoria, descriptores de archivo abiertos, variables de entorno y estado de señales).
2. **`execve()`**: Reemplaza la imagen en memoria del proceso recién clonado con un nuevo ejecutable.

Esta división otorga gran flexibilidad a herramientas como `xargs` o shells para redirigir descriptores (`dup2`), modificar variables de entorno y cambiar directorios en el proceso hijo antes de ejecutar el binario destino.

### 2.2. El Enfoque Windows NT
En Windows, **no existe `fork()` nativo**. La creación de procesos es una operación monolítica mediante la API `CreateProcessW`:
- Requiere especificar la ruta del ejecutable, los argumentos completos en una única cadena (*command line string*), atributos de seguridad, banderas de creación y la estructura `STARTUPINFO` con los handles estándar heredados (`hStdInput`, `hStdOutput`, `hStdError`).
- Las aplicaciones estándar de OpenBSD que realizan procesamiento por lotes en un solo proceso (`cat`, `wc`, `head`, `ls`, etc.) no requieren `fork()`, por lo que compilan de forma limpia sin la sobrecarga de emular clonado de memoria.

---

## 3. Identificadores de Recursos: Descriptores de Archivo (`int`) vs `HANDLE`

| Propiedad | UNIX / OpenBSD | Windows NT |
| :--- | :--- | :--- |
| **Tipo primitivo** | Entero pequeño (`int fd >= 0`) | Puntero opaco (`HANDLE`, tipado como `void *`) |
| **Tabla de descriptores** | Índice directo en la tabla por proceso | Handle table index gestionada por el kernel NT |
| **Entrada estándar** | `0` (`STDIN_FILENO`) | `GetStdHandle(STD_INPUT_HANDLE)` |
| **Salida estándar** | `1` (`STDOUT_FILENO`) | `GetStdHandle(STD_OUTPUT_HANDLE)` |
| **Error estándar** | `2` (`STDERR_FILENO`) | `GetStdHandle(STD_ERROR_HANDLE)` |
| **Capa CRT** | El CRT y el Kernel usan el mismo `int fd` | El CRT (UCRT) mapea `int fd` hacia un `HANDLE` subyacente |

> [!NOTE]
> **Interoperabilidad en MSVC**:
> Si se necesita obtener el `HANDLE` de Win32 a partir de un descriptor POSIX de C (`int fd`), se utiliza la función interna del CRT de Microsoft:
> ```c
> intptr_t osfhandle = _get_osfhandle(fd);
> HANDLE hFile = (HANDLE)osfhandle;
> ```
> Y a la inversa, para envolver un `HANDLE` en un descriptor de C:
> ```c
> int fd = _open_osfhandle((intptr_t)hFile, _O_RDONLY);
> ```

---

## 4. Semántica de Eliminación y Bloqueo de Archivos (`unlink` vs `DeleteFile`)

Uno de los choques arquitectónicos más severos entre Unix y Windows es la semántica de borrado de archivos abiertos:

- **En UNIX**: Si un proceso tiene abierto un archivo y otro proceso ejecuta `unlink("archivo")`, el nombre del archivo se elimina de la tabla del directorio de inmediato. El inodo y los bloques de datos siguen vivos en disco hasta que el último descriptor sea cerrado (`close()`). Las aplicaciones UNIX aprovechan frecuentemente este comportamiento para crear archivos temporales seguros.
- **En Windows (NTFS)**: Tradicionalmente, si un archivo está abierto sin la bandera de compartición `FILE_SHARE_DELETE`, cualquier llamada a `DeleteFile()` o `_unlink()` fallará con `ERROR_SHARING_VIOLATION` o `EACCES`.
- **Estrategia de Porteo**: Al portar o implementar funciones de creación/inspección de archivos, se deben abrir siempre con flags de compartición completos:
  ```c
  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
  ```

---

## 5. El Sistema de Permisos: Modos Octales POSIX vs ACLs de NT

- **POSIX**: Utiliza una máscara de bits simple de 9 posiciones (`rwxrwxrwx`) dividida en Usuario (*Owner*), Grupo (*Group*) y Otros (*World*), complementada por los bits especiales SetUID (`04000`), SetGID (`02000`) y Sticky (`01000`).
- **Windows**: Emplea descriptores de seguridad (SD) con Listas de Control de Acceso Discrecionales (**DACL**), compuestas por Entradas de Control de Acceso (**ACE**) asociadas a Identificadores de Seguridad (**SID**).
- **Adaptación en la Capa de Compatibilidad (`bsd_compat`)**:
  - En la estructura `stat` de MSVC, solo existe el bit de sólo lectura (`_S_IWRITE`).
  - En `compat/include/sys/stat.h`, definimos las constantes octales estándar (`S_IRUSR`, `S_IWUSR`, `S_IXUSR`, `S_IRGRP`, etc.) y la función `strmode()` para generar la clásica cadena de permisos (`drwxr-xr-x`) requerida por `ls -l`.
