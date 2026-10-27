@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Steam-Launch-Bridge.ps1"
if errorlevel 1 pause
