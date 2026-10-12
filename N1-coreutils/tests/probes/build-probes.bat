@echo off
REM Compila las sondas de diagnóstico de tests/probes/.
REM
REM Cada sonda es un programa chico y autónomo que mide UN comportamiento del
REM CRT de Windows sobre el que el porteo hace alguna suposición. Existen porque
REM varias de esas suposiciones resultaron falsas, y una medición cierra la
REM discusión más rápido que cualquier razonamiento.
REM
REM Ver Docs/porter-guide/10-* para el detalle de cada una.
REM
REM Uso:  build-probes.bat
REM       (requiere el entorno de MSVC ya cargado; build.bat lo hace vía devmode.bat)

setlocal
cd /d "%~dp0"

set DEV=C:\Workbench\PKGMGR\bin\devmode.bat
if exist "%DEV%" call "%DEV%" >nul 2>&1

set ROOT=%~dp0..\..
set INC=%ROOT%\compat\include
set FI=%ROOT%\compat\include\compat.h
set SRC=%ROOT%\compat\src

echo === Sondas autonomas ===
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:probe_broken_pipe.exe   probe_broken_pipe.c   || exit /b 1
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:probe_sigpipe_detect.exe probe_sigpipe_detect.c || exit /b 1
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:probe_peek_healthy.exe  probe_peek_healthy.c  || exit /b 1
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:probe_crt_init.exe      probe_crt_init.c      || exit /b 1

REM probe_fts_name enlaza directo las fuentes de compat que necesita, en vez de
REM contra bsd_compat: esa biblioteca es ahora un OBJECT library de CMake y no
REM deja un .lib suelto que se pueda tomar prestado.
echo.
echo === Sonda de FTS (enlaza fuentes de compat directo) ===
cl /nologo /W3 /I "%INC%" /FI "%FI%" /Fo:.\ /Fe:probe_fts_name.exe ^
   probe_fts_name.c ^
   "%SRC%\fts_compat.c" "%SRC%\stat_compat.c" "%SRC%\id_compat.c" ^
   "%SRC%\progname.c" "%SRC%\compat_init.c" "%SRC%\sigpipe.c" "%SRC%\binary_mode.c" || exit /b 1

echo.
echo Listo. Ejemplos de uso:
echo   probe_broken_pipe.exe ^| head -n 3
echo   probe_peek_healthy.exe ^| cat
echo   probe_crt_init.exe
echo   probe_fts_name.exe C:\ruta\a\un\directorio
