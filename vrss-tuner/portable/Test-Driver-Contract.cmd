@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Driver-Contract.ps1" %*
exit /b %errorlevel%
