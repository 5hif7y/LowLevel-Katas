@echo off
setlocal
rem N5 - build dtach, abduco and dvtm with MSVC + CMake.
rem Environment: DEVMODE > vswhere + vcvars64.bat (porter guide, module 08).
cd /d "%~dp0"

where cl >nul 2>nul && goto :env_ok
if defined DEVMODE (
    call "%DEVMODE%"
    goto :env_ok
)
rem Two batch traps live here:
rem  - "%ProgramFiles(x86)%" inside an if ( ... ) block: its ")" closes the block.
rem  - for /f runs its command through "cmd /c", which strips the quotes around
rem    a path containing ( ) - so the quoted vswhere path breaks again.
rem Avoid both: no block, and run vswhere.exe from its own directory.
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" goto :env_ok
pushd "%VSWHERE_DIR%"
for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
popd
if defined VSDIR if exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
:env_ok

if not exist upstream\PDCursesMod\curses.h (
    echo upstream\PDCursesMod missing: running fetch-upstream.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File fetch-upstream.ps1 || exit /b 1
)
cmake -B build -S . -A x64 %*
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1
echo.
echo Binaries in build\bin\Release\
dir /b build\bin\Release\*.exe
