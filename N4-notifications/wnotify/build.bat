@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul || call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS notify-send.c wnotify.c /Fe:notify-send.exe /link advapi32.lib
