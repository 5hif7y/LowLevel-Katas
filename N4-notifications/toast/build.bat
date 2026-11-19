@echo off
rem Build wnotify-toast.exe (C++/WinRT from the Windows SDK, MSVC).
setlocal
cd /d "%~dp0"
where cl >nul 2>nul || call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /permissive- wnotify-toast.cpp /Fo:%TEMP%\ /Fe:wnotify-toast.exe /link windowsapp.lib user32.lib
