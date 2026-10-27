@echo off
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Common-Session.ps1" -InputRoute PICO
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Common-Session.ps1" -InputRoute PICO -HeadsetIp "%~1"
)
pause
