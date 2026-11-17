@echo off
rem Installs labitop for the current user, so that "labitop" works in any new terminal.
rem No administrator rights needed. The work is done by install.ps1.
rem   install.bat              build, copy to %LOCALAPPDATA%\Programs\labitop, add it to PATH
rem   install.bat uninstall    remove the PATH entry and the installed file
setlocal
set ARGS=
if /i "%~1"=="uninstall" set ARGS=-Uninstall
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %ARGS%
set RC=%errorlevel%
rem Double-clicked (cmd /c): keep the window open until a key is pressed.
echo %cmdcmdline% | find /i "/c" >nul && pause
exit /b %RC%
