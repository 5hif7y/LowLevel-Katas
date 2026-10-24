# Módulo 08: El Flujo de Compilación: CMake, MSVC y dependencias

## 1. El Ecosistema de Compilación en Windows

Para portear software de sistemas Unix a Windows con estándares profesionales de ingeniería, se requiere una cadena de herramientas moderna:

1. **Compilador**: Microsoft Visual C++ (`cl.exe`), integrado en Visual Studio Developer Command Prompt.
2. **Sistema de Generación**: CMake 3.20+, que traduce las dependencias del proyecto en proyectos `.vcxproj` de MSBuild o archivos `build.ninja`.
3. **Dependencias**: VCPKG es **opcional**. Las dependencias header-only (por ejemplo `dirent`) conviene vendorizarlas en `compat/include/` para que el build no dependa de una ruta fija ni de una versión de VCPKG.

---

## 2. Activación del Entorno de Desarrollo

En Windows, las variables de entorno para localizar el compilador (`PATH`, `INCLUDE`, `LIB`) no están configuradas globalmente para evitar conflictos entre versiones de SDKs.

La forma canónica y **portable** es:

1. Si existe un script propio (`DEVMODE`), usarlo.
2. Si no, intentar el `devmode.bat` local del Workbench (atajo histórico).
3. Si no, localizar Visual Studio con `vswhere.exe` y llamar a `vcvars64.bat`.

```batch
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * ^
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
    -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat"
)
```

No hardcodees `C:\Workbench\...` ni una versión concreta de Visual Studio en la guía: cambia en cada máquina y en cada actualización.

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
    )
endif()

# bsd_compat: OBJECT library, deliberadamente NO una biblioteca estática.
#
# Una biblioteca estática sólo aporta los .obj que resuelven algún símbolo
# pendiente. compat_init.c no exporta nada que el resto del programa llame: su
# única razón de ser es colgar un inicializador de la sección .CRT$XCU para que
# el CRT lo ejecute antes de main(). Linkeado como .lib, ese objeto se descarta
# y el inicializador nunca corre — ni el modo binario ni el soporte VT100 se
# activan, en silencio y sin ningún error de compilación.
#
# Como OBJECT library los objetos se enlazan siempre.
file(GLOB COMPAT_SOURCES "compat/src/*.c")
add_library(bsd_compat OBJECT ${COMPAT_SOURCES})

target_include_directories(bsd_compat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/include
)
target_link_libraries(bsd_compat PUBLIC advapi32)

if(MSVC)
    # Forced include para las fuentes upstream. Va en la OBJECT library y se
    # propaga a los ejecutables que la linkean.
    target_compile_options(bsd_compat PUBLIC
        /FI${CMAKE_CURRENT_SOURCE_DIR}/compat/include/compat.h
    )
endif()
```

### Banderas críticas de MSVC

- **`/utf-8`**: fuerza a MSVC a interpretar código fuente y literales como UTF-8. Es indispensable para `wcwidth`, `cut` y mensajes con caracteres especiales.
- **`/FI compat.h`**: inclusión forzada de la cabecera de compatibilidad antes de cualquier unidad de traducción. Ponelo en la OBJECT library, no en un `include_directories` global.
- **`/D_CRT_SECURE_NO_WARNINGS`**: desactiva las advertencias que recomiendan `fopen_s`/`sprintf_s`.
- **`/D_CRT_NONSTDC_NO_DEPRECATE`**: evita warnings por `open`, `read`, `close`, etc.

### Por qué `target_*` y no `include_directories`

`include_directories()` y `add_compile_options()` son globales y se filtran a
targets que no deberían verlos. Con `target_include_directories()` y
`target_compile_options()`:

- sólo `bsd_compat` y quienes la linkean heredan `/FI` y `compat/include`;
- las librerías de terceros (popt, zlib) se pueden compilar como targets
  separados **sin** el `Forced Include`;
- el build es más fácil de razonar y de reutilizar.

### Dependencias: vendorizar lo header-only

`dirent` de tronkko es un header-only MIT de ~1.200 líneas. En vez de exigir
`vcpkg install dirent:x64-windows` y hardcodear
`C:/Users/Shifty/DEV/vcpkg/installed/x64-windows/include`, copialo a
`compat/include/dirent.h`. El include path de `compat/include` ya lo encuentra.
Ventajas:

- build reproducible sin VCPKG;
- no depende de una ruta de usuario;
- no hay que explicar cómo instalar VCPKG para compilar coreutils.

Si una dependencia **no** es header-only, usá `VCPKG_ROOT` o `find_path`/
`find_library` con un `HINT`, nunca una ruta absoluta de una máquina.

---

## 4. Script de Compilación Desatendida (`build.bat`)

Para permitir compilaciones automatizadas, de un solo comando y reproducibles:

```batch
@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Building OpenBSD Coreutils on Windows (MSVC + CMake)
echo =========================================================

rem Entorno: DEVMODE > devmode.bat local > vswhere + vcvars64.bat
if defined DEVMODE (
    call "%DEVMODE%"
) else if exist "C:\Workbench\PKGMGR\bin\devmode.bat" (
    call "C:\Workbench\PKGMGR\bin\devmode.bat"
) else (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * ^
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
            -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" ^
                call "%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not exist build mkdir build

rem Dejamos que CMake elija el generador por defecto (Visual Studio si está).
cmake -B build -S . -A x64 %*
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cmake --build build --config Release --parallel
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build Succeeded! All binaries are in build\bin\Release\
dir build\bin\Release\*.exe
```

Notas:

- `-A x64` es necesario para Visual Studio; con Ninja se ignora o se usa el
  toolchain correspondiente.
- No fijes `-G "Visual Studio 18 2026"`: el nombre cambia entre versiones. Si
  necesitás un generador concreto, pasalo por `%*`.
- Si cambiás de Visual Studio y el `build/` viejo apunta a un `cl.exe` que ya
  no existe, borrá `build/` y reconfigurá. CMake cachea el path absoluto del
  compilador y falla con “is not a full path to an existing compiler tool”.
- Lo mismo pasa si el cache se creó sin `-A x64` y después agregás `-A x64`:
  CMake dice “generator platform: x64 does not match the platform used
  previously”. Borrá `build/` y volvé a configurar.

Al ejecutar `build.bat`, el sistema configura el entorno, compila
`bsd_compat` como OBJECT library, enlaza las 21 utilidades y coloca los
binarios en `build\bin\Release\`.
