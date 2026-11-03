@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Building OpenBSD Coreutils on Windows (MSVC + CMake)
echo =========================================================

rem Optional custom environment (set DEVMODE to a .bat/.cmd).
if defined DEVMODE (
    call "%DEVMODE%"
) else (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" call "%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not exist build mkdir build

cmake -B build -S . -A x64 %*
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %ERRORLEVEL%
)

cmake --build build --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo =========================================================
echo  Build Succeeded! All binaries are in build\bin\Release\
echo =========================================================
dir build\bin\Release\*.exe
