@echo off
rem Builds labitop on Windows with MinGW-w64 g++.
rem   build.bat         build build\labitop.exe
rem   build.bat test    also build and run the tests
setlocal
set RC=0
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ not found. Install MinGW-w64 GCC first, for example: scoop install gcc
    set RC=1& goto :end
)

set SRC=src\term.cpp src\parse.cpp src\nvml.cpp src\sys_win.cpp src\sys_linux.cpp
set FLAGS=-std=c++17 -O2 -Wall -Wextra -static
set LIBS=-lntdll -lpsapi -liphlpapi -lpdh -ldxgi

if not exist build mkdir build

echo Building build\labitop.exe ...
g++ %FLAGS% src\main.cpp %SRC% -o build\labitop.exe %LIBS%
if errorlevel 1 (
    echo Build failed. If the error says "Permission denied", close the running labitop and try again.
    set RC=1& goto :end
)

if /i "%~1"=="test" (
    echo Building and running tests ...
    g++ %FLAGS% tests\test.cpp %SRC% -o build\labitop_test.exe %LIBS%
    if errorlevel 1 set RC=1& goto :end
    build\labitop_test.exe
    if errorlevel 1 set RC=1& goto :end
)

echo Done: build\labitop.exe

:end
rem Double-clicked (cmd /c): keep the window open until a key is pressed.
if not defined LABITOP_NOPAUSE echo %cmdcmdline% | find /i "/c" >nul && pause
exit /b %RC%
