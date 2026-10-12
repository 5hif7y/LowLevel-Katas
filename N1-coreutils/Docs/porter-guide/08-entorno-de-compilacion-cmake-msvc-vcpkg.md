# Módulo 08: El Flujo de Compilación: CMake, MSVC y VCPKG

## 1. El Ecosistema de Compilación en Windows

Para portear software de sistemas Unix a Windows con estándares profesionales de ingeniería, se requiere una cadena de herramientas moderna:

1. **Compilador**: Microsoft Visual C++ (`cl.exe`), integrado en Visual Studio Developer Command Prompt.
2. **Sistema de Generación**: CMake 3.20+, que traduce las dependencias del proyecto en proyectos `.vcxproj` de MSBuild o archivos `build.ninja`.
3. **Gestor de Paquetes**: **VCPKG**, que descarga, compila e instala bibliotecas de C/C++ en arquitecturas nativas (por ejemplo, `x64-windows`).

---

## 2. Activación del Entorno de Desarrollo (`devmode.bat`)

En Windows, las variables de entorno para localizar el compilador (`PATH`, `INCLUDE`, `LIB`) no están configuradas globalmente en el sistema para evitar conflictos entre versiones de SDKs.

Se accede al entorno a través del script `devmode.bat`:
```cmd
call "C:\Workbench\PKGMGR\bin\devmode.bat"
```
Este script:
- Invoca internamente a `vcvars64.bat` de Visual Studio 2026.
- Añade el compilador `cl.exe` y el enlazador `link.exe` al `%PATH%`.
- Configura las rutas de inclusión de VCPKG (`C:\Users\Shifty\DEV\vcpkg\installed\x64-windows\include`) en `%INCLUDE%`.
- Configura las rutas de bibliotecas de VCPKG (`C:\Users\Shifty\DEV\vcpkg\installed\x64-windows\lib`) en `%LIB%`.

---

## 3. Configuración de CMake (`CMakeLists.txt`)

El archivo `CMakeLists.txt` raíz define las directivas para compilar código C de OpenBSD sin necesidad de modificar los archivos `.c`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(openbsd_coreutils C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(MSVC)
    add_compile_options(
        /W3
        /utf-8
        /D_CRT_SECURE_NO_WARNINGS
        /D_CRT_NONSTDC_NO_DEPRECATE
        /FI "${CMAKE_CURRENT_SOURCE_DIR}/compat/include/compat.h"
    )
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/include
    "C:/Users/Shifty/DEV/vcpkg/installed/x64-windows/include"
)

# Biblioteca estática bsd_compat
file(GLOB COMPAT_SOURCES "compat/src/*.c")
add_library(bsd_compat STATIC ${COMPAT_SOURCES})
target_link_libraries(bsd_compat PUBLIC advapi32)
```

### Banderas Críticas de MSVC:
- **/utf-8**: Fuerza a MSVC a interpretar tanto el código fuente como los literales de cadena en UTF-8, indispensable para manejar caracteres especiales en `wcwidth`, `cut` y mensajes de diagnóstico.
- **/FI compat.h**: Realiza la inclusión forzada de la cabecera de compatibilidad antes de procesar cualquier unidad de traducción.
- **/D_CRT_SECURE_NO_WARNINGS**: Desactiva las advertencias obsoletas de Microsoft que recomiendan sustituir funciones ISO C (`fopen`, `sprintf`) por variantes propietarias de Microsoft (`fopen_s`, `sprintf_s`).

---

## 4. Script de Compilación Desatendida (`build.bat`)

Para permitir compilaciones automatizadas, de un solo comando y reproducibles:

```batch
@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Building OpenBSD Coreutils on Windows (MSVC + CMake)
echo =========================================================

call "C:\Workbench\PKGMGR\bin\devmode.bat"

if not exist build mkdir build

cmake -B build -S .
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cmake --build build --config Release
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Succeeded! All binaries are in build\bin\Release\
dir build\bin\Release\*.exe
```
Al ejecutar `build.bat`, el sistema configura el entorno, compila `bsd_compat.lib`, enlaza las 21 utilidades de OpenBSD en modo Release y coloca los binarios en `build\bin\Release\`.
