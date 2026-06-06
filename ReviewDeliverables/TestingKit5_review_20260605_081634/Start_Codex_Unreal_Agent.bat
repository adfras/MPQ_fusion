@echo off
setlocal
set "PROJECT_ROOT=%~dp0"
set "BRIDGE_DIR=%PROJECT_ROOT%AgentBridge"
set "PROJECT_FILE=%PROJECT_ROOT%TestingKit5.uproject"
set "UE_EDITOR=D:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"

where npm.cmd >nul 2>nul
if errorlevel 1 (
  echo npm.cmd was not found on PATH. Install Node.js or start the bridge from AgentBridge manually.
  pause
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$c=Get-NetTCPConnection -LocalPort 8765 -ErrorAction SilentlyContinue | ? State -eq Listen | Select -First 1; if(-not $c){ Start-Process -FilePath 'npm.cmd' -ArgumentList 'start' -WorkingDirectory '%BRIDGE_DIR%' -WindowStyle Hidden }"

if exist "%UE_EDITOR%" (
  start "" "%UE_EDITOR%" "%PROJECT_FILE%"
) else (
  start "" "%PROJECT_FILE%"
)

endlocal
