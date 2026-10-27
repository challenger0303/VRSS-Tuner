# Setup and verification (English)

[日本語](SETUP.ja.md) · [Project overview](../README.md)

This guide covers the native Pimax and common-provider routes. Steam Frame on NVIDIA 596.02 uses the separate [Frame guide](STEAM-FRAME.md).

## Tested configuration and limits

- Headset confirmed with the developer's live visual check: **Pimax Dream Air**.
- GPU/driver in that setup: GeForce RTX 5090, NVIDIA driver 610.88. The provider checks the exact hashes and gaze-handler methods of the audited `nvapi64_impl.dll` and `nvwgf2umx.dll`, not just the displayed driver version. An explicit experimental contract for another exact image pair is described in [driver contracts](DRIVER-CONTRACTS.md); unmatched builds are refused.
- The common route needs SteamVR/OpenVR headset geometry, an NVIDIA VRChat profile that allows VRSS, valid gaze from the selected source, and MSAA enabled inside VRChat. The native Pimax route uses Pimax eye tracking.
- Other headsets, PCs and driver builds are hardware-unverified. The setup checker can identify prerequisites but cannot prove a visible, moving high-quality region.

## Install

1. Download the Windows installer from [Releases](https://github.com/challenger0303/VRSS-Tuner/releases). It places all files in a fixed per-user location. The portable ZIP is intended for development and manual diagnostics. Open the tuner and choose a language at the top right.
2. **Native Pimax XR5:** Enable headset eye tracking, select **Pimax path (native priority)** in Gaze input, turn VRSS **On** in Graphics and apply it. Start SteamVR and VRChat normally. Do not run the common-provider scripts.
3. **Common provider with SRanibro, PSVR2 Toolkit or another source:** Start the headset and its gaze software, then connect SteamVR. Select one source in Gaze input, turn VRSS **On** in Graphics and apply it, then click **Start gaze path**. This runs setup checks and starts the provider without touching VRChat. Approve the Windows administrator prompt manually. You can launch VRChat yourself afterward.
4. **Steam Frame:** Follow the [Frame guide](STEAM-FRAME.md) instead of the common launch button.
5. Enable MSAA in VRChat's Graphics settings. Start with x4 for comparison. Verify in the headset that a high-quality region appears in both eyes and follows gaze horizontally and vertically. A green status in the tuner alone does not prove the rendered effect.
6. To stop gaze, use **Stop** in Tuner or `Stop-Provider.cmd`. This stops gaze providers and auxiliary gaze bridges regardless of the selected source, including after reopening Tuner. VRChat, SteamVR, camera runtimes and face-tracking applications are left running. Stop pauses automatic gaze startup until an explicit Start.

For a manual, read-only common-route check, run `Test-Setup.cmd`. If `driverSupported` or `vrssAllowed` is false, the common provider cannot start on that setup. `Start-Provider.cmd` is for provider-only diagnostics. Set Baballonia Eye output to **127.0.0.1:9020** to match Tuner's receive port. Baballonia itself defaults to port 8888, so redirect it to Tuner. Tuner forwards the original OSC to port 8888 for VRCFaceTracking; use 0 to disable forwarding. Receive and forward ports must differ. A port conflict is reported instead of silently failing.

For a later launch with a fixed HMD key, start the provider and run `Start-VRChat-HMD-Key.cmd`. To use Steam's normal Play button, run `Install-Steam-Launch-Bridge.cmd` once and paste its launch option into Steam > VRChat > Properties > General. The bridge checks the current HMD at launch and reserves a validated fixed session ID even if the selected common provider is still starting. The HMD ID cannot be changed inside a running VRChat process. Switching inputs within the common provider preserves its PID and NVIDIA registration, without restarting VRChat. **A full Stop exits the provider.** NVIDIA may refuse re-registration while VRChat holds the previous connection; if that happens, close VRChat normally, start the gaze path, then relaunch VRChat. A fixed ID alone does not guarantee reconnect after a full Stop.

For SteamVR autostart, use the installer. It installs to a fixed per-user Programs location and registers only when SteamVR is already running. If registration is pending, start SteamVR normally from Steam and run `Install-SteamVR-Autostart.cmd` from the installed folder once. Setup never launches SteamVR. On later starts, SteamVR opens the tuner and attempts to start the selected common gaze path; a Windows administrator prompt may still need manual confirmation. A fresh Steam-launched SteamVR session automatically started Tuner and the SRanibro provider on the developer's PC. Closing Tuner leaves the connected provider and VRChat running; reopening it restores the live controls and status. An installer upgrade preserves the running provider and VRChat launch bridge. The updated provider is applied at its next start, and the launch bridge is refreshed at the next SteamVR start after VRChat exits.

## Live status in the tuner

Select a different input and press **Switch to selected source** to change the running common input. The saved selection no longer locks the dropdown. If switching fails, the previous common input and its source bridge are restored where possible. A source bridge that crashed can be restarted with the same button. Baballonia reception runs in the provider independently of the Tuner window.

The Gaze Input tab shows the source, gaze handoff, provider registration and NVIDIA gaze updates separately. Gray means stopped or awaiting data; amber means more checks are needed; green means that stage was observed. The left/right dots show **submitted gaze positions**, not the boundary of the high-quality region. Verify actual rendering and alignment in the headset. Available telemetry differs on the native Pimax route.

## Pimax path

**Pimax path (native priority)** uses the native Pimax XR5 runtime while it is available. If SRanibro takes over the camera, a separately installed Pimax eye-device bridge registers only when it receives fresh binocular gaze. The tuner does not start SRanibro or prevent a return to the native path. The tested bridge binary is included under `pimax-eye-bridge/` in the portable ZIP, but it is not installed automatically.

On the tested PC, SRanibro 0.1.10-beta in `pimax_xr5` mode stopped and disabled the native service. If the service does not return after SRanibro exits, click **Restore native service** in the tuner and approve Windows elevation. The button is unavailable while SRanibro is running. The Pimax eye device may still need to be re-registered; if gaze does not return, use **Restart service** in the Pimax app (this may close VR apps). A running service or provider does not prove valid gaze; verify that the high-quality region follows eye movement in the headset.

## PSVR2 input route (unverified)

Two PSVR2 input paths are available. **PSVR2 Toolkit (direct)** reads the installed Toolkit CAPI and sends binocular gaze through the common provider. Select it in Tuner, then run `Start-PSVR2-Toolkit.cmd`. The bridge requires a Toolkit build exposing `psvr2_toolkit_capi.dll`; Toolkit itself is not bundled. **SRanibro (common)** reads the local BrokenEye-format binocular stream on port 5555. Select it in Tuner, start SRanibro, and run `Start-SRanibro.cmd`. This SRanibro path is headset-independent: it uses the connected HMD's OpenVR projection geometry, but each HMD still needs gaze mapping validation and calibration. Do not run both sources together. The Gaze Calibration tab uses the common calibration file; save headset-specific values in named profiles. PSVR2 gaze reception, NVIDIA registration, and a moving VRChat high-quality region remain **unverified on hardware**. Check the [PSVR2 Toolkit installation guide](https://github.com/BnuuySolutions/PSVR2Toolkit/wiki/Installation) for its own version requirements.

## VIVE input routes (hardware unverified)

For **VIVE Pro Eye**, install and calibrate the official SRanipal runtime, choose the Pro Eye source in Tuner, and run `Start-Vive-Pro-Eye.cmd`. For **VIVE Streaming**, enable PC eye-data forwarding in the vendor streamer, choose its source and run `Start-Vive-Streaming.cmd`. Set `VRSS_VIVE_SDK_DLL` to the full path of the matching installed vendor DLL when automatic discovery fails. Neither vendor DLL is bundled. Both bridges were built and passed synthetic mapping tests, but actual gaze reception and VRChat VRSS2 rendering on VIVE hardware are unverified. See the [HMD compatibility audit](HMD-GAZE-COMPATIBILITY.md).

## Position calibration

The bundled `common-vrss-provider/calibration.txt` starts with zero per-eye offsets and horizontal/vertical gain 1. Adjust these using the tuner's gaze controls while watching a suitable high-contrast scene in the headset. Keep your head as still as practical when comparing gaze movement and the rendered region. The initial values are generic and are **not** a Dream Air calibration preset.

## Troubleshooting

| Setup result or symptom | What to check |
| --- | --- |
| `driverSupported: false` | The installed NVIDIA binaries or gaze-handler methods do not match a built-in or explicitly selected experimental contract. Use `Audit-Driver.cmd` to collect a read-only report; do not assume a matching marketing version is sufficient. |
| `vrssAllowed: false` | The NVIDIA VRChat profile lacks the VRSS allow setting. This package does not fabricate or replace that profile. |
| `steamVrRunning: false` or `hmdGeometryValid: false` | Connect the HMD, start SteamVR, then rerun the check. |
| Official provider is running | Selecting a common source and starting it closes only the Pimax VRSS sender, to avoid competing providers. Camera runtimes and face-tracking apps are not stopped. |
| Provider runs but region does not track | Check fresh binocular gaze from the selected source, each tuner input/provider/NVIDIA stage, VRChat MSAA and both eyes in the headset. For Baballonia, start the gaze path and check the receive port, packet count and recognized eye-value count. Keep VRChat MSAA enabled; this release was tested with x4. |

Diagnostic output and run logs are written under `%LOCALAPPDATA%\VRSS Tuner`. They may contain machine and headset information; inspect before sharing. The package does not include NVIDIA or Pimax drivers and makes no guarantee for unsupported configurations.

## beta.5 verification and remaining limits

On Pimax Dream Air / RTX 5090 / NVIDIA 610.88, the installed beta.5 connected SRanibro to protected VRChat with MSAA x4. Both eyes' submitted positions changed horizontally and vertically; 458 submissions succeeded during a 10-second capture, with no submission failures. Closing and reopening Tuner preserved the same provider and VRChat processes and the saved calibration. These backend checks do not independently prove rendered VRSS image quality.

Varjo now reloads gains and per-eye offsets while running; its SDK build and synthetic projection tests passed, but hardware remains unverified. Steam Frame remains a separate experimental GUI-owned session. Standard provider/bridge launches currently have a 24-hour duration limit; longer unattended operation and log retention are not yet implemented.
