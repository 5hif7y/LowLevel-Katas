@echo off
rem Build notifyd.exe (MSVC + SQLite from vcpkg). Set VCPKG_ROOT or VCPKG_INSTALLED.
setlocal
cd /d "%~dp0"
where cl >nul 2>nul || call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not defined VCPKG_INSTALLED (
  if defined VCPKG_ROOT ( set "VCPKG_INSTALLED=%VCPKG_ROOT%\installed\x64-windows" ) else ( set "VCPKG_INSTALLED=%~dp0..\..\..\vcpkg\installed\x64-windows" )
)
if not exist "%VCPKG_INSTALLED%\include\sqlite3.h" ( echo sqlite3 not found under %VCPKG_INSTALLED% ^(vcpkg install sqlite3^) & exit /b 1 )
rem schema.sql -> schema.inc (C string literal), so the SQL lives in one place
powershell -NoProfile -ExecutionPolicy Bypass -File gen-schema.ps1 || exit /b 1
cl /nologo /W4 /O2 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I"%VCPKG_INSTALLED%\include" notifyd.c ..\wnotify\wnotify.c /Fo:%TEMP%\ /Fe:notifyd.exe /link /LIBPATH:"%VCPKG_INSTALLED%\lib" sqlite3.lib shell32.lib ole32.lib user32.lib advapi32.lib || exit /b 1
copy /y "%VCPKG_INSTALLED%\bin\sqlite3.dll" . >nul
