@echo off
set "route=%~1"
if "%route%"=="" set "route=Baballonia"
set "existing="
if /i "%~3"=="existing-steamvr" set "existing=-RequireExistingSteamVR"
if /i "%~2"=="wait" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Common-Session.ps1" -InputRoute "%route%" -ProviderOnly -WaitForHeadset %existing%
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Common-Session.ps1" -InputRoute "%route%" -ProviderOnly
  pause
)
