@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /W4 /std:c11 /Isrc /Fo:build\ /Fe:build\scrollkey.exe src\main.c src\scroll.c src\windows.c user32.lib
if errorlevel 1 exit /b 1
rem Compile the two scroll.c files separately to avoid colliding .obj names.
cl /nologo /O2 /W4 /std:c11 /Isrc /c /Fo:build\scroll-test.obj tests\scroll.c
if errorlevel 1 exit /b 1
cl /nologo /Fe:build\scroll-test.exe build\scroll-test.obj build\scroll.obj
if errorlevel 1 exit /b 1
build\scroll-test.exe
exit /b %errorlevel%
