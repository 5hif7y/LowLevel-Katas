@echo off
rem Build notifyd.exe with MSVC.
rem   SQLite, one of:
rem     set SQLITE_SRC=<dir with sqlite3.c/sqlite3.h>  -> compiled in statically, FTS5 on, no DLL
rem     (default) vcpkg sqlite3 under %VCPKG_INSTALLED% or %VCPKG_ROOT%\installed\x64-windows
rem               FTS5 only if installed as: vcpkg install sqlite3[core,json1,fts5]
setlocal
cd /d "%~dp0"
where cl >nul 2>nul || call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
powershell -NoProfile -ExecutionPolicy Bypass -File gen-schema.ps1 || exit /b 1
set CFLAGS=/nologo /W4 /O2 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS
set LIBS=shell32.lib ole32.lib user32.lib advapi32.lib

if defined SQLITE_SRC goto static

if not defined VCPKG_INSTALLED (
  if defined VCPKG_ROOT ( set "VCPKG_INSTALLED=%VCPKG_ROOT%\installed\x64-windows" ) else ( set "VCPKG_INSTALLED=%~dp0..\..\..\vcpkg\installed\x64-windows" )
)
if not exist "%VCPKG_INSTALLED%\include\sqlite3.h" ( echo sqlite3 not found under %VCPKG_INSTALLED% ^(vcpkg install sqlite3[core,json1,fts5] or set SQLITE_SRC^) & exit /b 1 )
cl %CFLAGS% /I"%VCPKG_INSTALLED%\include" notifyd.c ..\wnotify\wnotify.c /Fo:%TEMP%\ /Fe:notifyd.exe /link /LIBPATH:"%VCPKG_INSTALLED%\lib" sqlite3.lib %LIBS% || exit /b 1
copy /y "%VCPKG_INSTALLED%\bin\sqlite3.dll" . >nul
exit /b 0

:static
if not exist "%SQLITE_SRC%\sqlite3.c" ( echo %SQLITE_SRC%\sqlite3.c not found ^(SQLite amalgamation^) & exit /b 1 )
if not exist "%TEMP%\notifyd-sqlite3.obj" (
  cl /nologo /O2 /c /DSQLITE_ENABLE_FTS5 /DSQLITE_THREADSAFE=1 /DSQLITE_DQS=0 "%SQLITE_SRC%\sqlite3.c" /Fo:"%TEMP%\notifyd-sqlite3.obj" || exit /b 1
)
cl %CFLAGS% /I"%SQLITE_SRC%" notifyd.c ..\wnotify\wnotify.c "%TEMP%\notifyd-sqlite3.obj" /Fo:%TEMP%\ /Fe:notifyd.exe /link %LIBS% || exit /b 1
if exist sqlite3.dll del sqlite3.dll
