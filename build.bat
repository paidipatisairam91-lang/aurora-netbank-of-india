@echo off
setlocal
cd /d "%~dp0"
echo Building Aurora Bank C++17 server...
g++ -std=c++17 -O2 -pthread src\main.cpp src\http_server.cpp src\bank_service.cpp src\fraud_engine.cpp -lws2_32 -o aurora_bank.exe
if errorlevel 1 (
  echo.
  echo Build failed. Check that MinGW-w64 / MSYS2 GCC is installed and on PATH.
  pause
  exit /b 1
)
echo Build successful: aurora_bank.exe
endlocal
