@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Building OpenBSD Coreutils on Windows (MSVC + CMake)
echo =========================================================

call "C:\Workbench\PKGMGR\bin\devmode.bat"

if not exist build mkdir build

cmake -B build -S .
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %ERRORLEVEL%
)

cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo =========================================================
echo  Build Succeeded! All binaries are in build\bin\Release\
echo =========================================================
dir build\bin\Release\*.exe
