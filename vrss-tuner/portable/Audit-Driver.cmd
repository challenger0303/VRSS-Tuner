@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Audit-Driver.ps1"
if errorlevel 1 pause
