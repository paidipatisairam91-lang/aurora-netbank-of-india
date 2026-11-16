@echo off
setlocal
cd /d "%~dp0"
call build.bat
if errorlevel 1 exit /b 1
echo.
echo Starting Aurora Bank...
aurora_bank.exe "%CD%"
endlocal
