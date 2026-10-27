@echo off
"%~dp0steamvr_autostart.exe" --remove
if errorlevel 1 (
  echo Could not unregister. Start SteamVR and retry.
) else (
  echo VRSS Tuner SteamVR autostart removed.
)
pause
