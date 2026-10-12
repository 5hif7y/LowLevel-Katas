# Guía Maestra de Porteo: De OpenBSD / UNIX a Windows Nativo (MSVC + CMake + VCPKG)

Bienvenido a la **Guía Maestra de Porteo de Coreutils**, un compendio técnico, arquitectónico y pedagógico diseñado para correlacionar el desarrollo de software en lenguaje C —desde los fundamentos expuestos por Brian W. Kernighan y Dennis M. Ritchie (*The C Programming Language*, K&R)— hasta la implementación de un sistema de utilidades de usuario (*userland*) de grado de producción proveniente de **OpenBSD**, compilado de forma 100% nativa en **Windows x64** utilizando **MSVC (Visual Studio)**, **CMake** y **VCPKG**.

---

## 🧭 Mapa de Contenidos

Esta guía está estructurada en módulos secuenciales y temáticos:

| Módulo | Documento | Temas Clave |
| :--- | :--- | :--- |
| **01** | [Correlación K&R C y Filosofía UNIX](01-correlacion-kr-c-y-unix.md) | Genealogía K&R, flujo estándar, KNF de BSD, pipes y modularidad. |
| **02** | [Arquitectura POSIX vs Win32](02-diferencias-arquitectonicas-unix-vs-windows.md) | Procesos vs Threads, syscalls vs Win32 API, descriptores vs HANDLEs. |
| **03** | [El Sistema de I/O: Binario vs Texto](03-el-sistema-io-y-modos-binario-texto.md) | CRLF vs LF, `_O_BINARY`, `_setmode`, buffering, inicializadores CRT `.CRT$XCU`. |
| **04** | [Cabeceras y Capa de Compatibilidad](04-cabeceras-y-libreria-de-compatibilidad.md) | `err.h`, `unistd.h`, `sys/cdefs.h`, `strlcpy`, `strtonum`, `getopt`, Forced Include (`/FI`). |
| **05** | [Sistema de Archivos, Rutas y `dirent`](05-sistema-de-archivos-rutas-y-dirent.md) | Separadores `/` y `\`, case sensitivity, `dirent.h`, `struct stat` (`st_atim`, `st_blksize`), timestamps (`utimensat`). |
| **06** | [Seguridad en OpenBSD: `pledge` y `unveil`](06-seguridad-en-openbsd-pledge-y-unveil.md) | Promesas de syscalls, aislamiento de filesystem, stubs en Windows vs AppContainer. |
| **07** | [Redes y Abstracción Multiplataforma](07-redes-y-abstraccion-multiplataforma.md) | `INetworking_Win32.h`, WinSock2, sockets vs FDs, mapeo de errores `errno` vs `WSA`. |
| **08** | [Entorno de Compilación (CMake, MSVC, VCPKG)](08-entorno-de-compilacion-cmake-msvc-vcpkg.md) | `devmode.bat`, CMake C11, MSVC flags (`/utf-8`, `/W3`), VCPKG, scripts desatendidos. |
| **09** | [Estudios de Caso: Utilidad por Utilidad](09-estudios-de-caso-utilidad-por-utilidad.md) | Análisis de código fuente línea a línea de `cat`, `wc`, `head`, `ls`, `tail`, y contraste con BusyBox. |
| **10** | [Estudio de Casos de Depuración](10-estudio-de-casos-de-depuracion.md) | Tres bugs que la suite descubrió: `SIGPIPE` inexistente, memoria solapada en FTS, y el inicializador CRT que la biblioteca estática se comía. |

---

## 🛠️ Tecnologías Empleadas

- **Código Base Upstream**: OpenBSD `src` (`bin/`, `usr.bin/`, `lib/libc/`, `lib/libutil/`) y BusyBox `src` (para análisis comparativo de tamaño vs modularidad).
- **Compilador**: Microsoft Visual C++ (MSVC 19.50 / Visual Studio Developer Prompt vía `devmode.bat`).
- **Sistema de Construcción**: CMake 3.20+ configurado para builds reproducibles en modo `Release` y `Debug`.
- **Gestor de Paquetes**: VCPKG en `C:\Users\Shifty\DEV\vcpkg`.
- **Capa de Compatibilidad**: `bsd_compat` (`libbsd_compat.lib`), biblioteca estática sin dependencias externas pesadas que proporciona una ABI POSIX/BSD transparente.
