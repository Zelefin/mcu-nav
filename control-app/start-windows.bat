@echo off
setlocal
cd /d "%~dp0"
if "%NAV_CONTROL_APP_PORT%"=="" set NAV_CONTROL_APP_PORT=8765
set URL=http://127.0.0.1:%NAV_CONTROL_APP_PORT%/index.html

where python >nul 2>nul
if errorlevel 1 (
  echo Python 3 is required. Install it from https://www.python.org/downloads/windows/
  pause
  exit /b 1
)

echo nav-mcu control app running at %URL%
echo Close this window to stop the server.
start "" powershell -NoProfile -Command "Start-Sleep -Seconds 1; Start-Process '%URL%'"
python serve.py --port %NAV_CONTROL_APP_PORT% --host 127.0.0.1
