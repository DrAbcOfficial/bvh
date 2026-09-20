@echo off
rem Syntax-check the plugin sources inside WSL; see tools/check_syntax.sh.
rem The inner path must survive MSYS/Git-Bash argument rewriting, hence the guard.
setlocal
set "MSYS_NO_PATHCONV=1"
for /f "usebackq delims=" %%i in (`wsl.exe wslpath -a "%~dp0.."`) do set "WDIR=%%i"
wsl.exe -e bash -c "cd '%WDIR%' && bash tools/check_syntax.sh"
