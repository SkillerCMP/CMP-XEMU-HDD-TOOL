@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Windows-Docker.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo BUILD FAILED - see the logs folder for the saved Docker output.
pause
exit /b %RESULT%
