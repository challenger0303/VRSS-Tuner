@echo off
"%~dp0steamvr_autostart.exe" --install
if errorlevel 1 (
  echo Registration failed. Start SteamVR normally from Steam, then retry from the installed VRSS Tuner folder.
) else (
  echo VRSS Tuner will start with SteamVR. VRChat remains under your control.
)
pause
