@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Set-Gaze-Route.ps1" -InputRoute SteamVREye
pause
