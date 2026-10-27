# VRSS Tuner

[日本語ガイド](README.ja.md) · [English setup guide](docs/SETUP.md) · [日本語セットアップガイド](docs/SETUP.ja.md)

VRSS Tuner is an experimental Windows tool for configuring NVIDIA VRSS2 and sending binocular gaze data to a common VRSS provider. It can receive Baballonia OSC gaze data and uses OpenVR headset geometry for per-eye mapping. It does not modify VRChat files or inject a DLL into the game.

**Reported visual tracking:** Pimax Dream Air (maintainer, NVIDIA 610.88) and Steam Frame (PR contributor, NVIDIA 596.02). PICO / NVIDIA 616.92 also reached a consumer and successfully submitted real gaze in normally EAC-protected VRChat; its rendered-region tracking was not visually confirmed. This is an independent project, not an NVIDIA, Pimax, Valve, or VRChat product.

The common gaze provider accepts Baballonia OSC, a direct [PSVR2 Toolkit CAPI](docs/SETUP.md#psvr2-input-route-unverified) bridge, SRanibro binocular gaze, and experimental VIVE Streaming/Pro Eye bridges. The SRanibro route is shared across headsets and uses the connected HMD's OpenVR geometry. VRSS tracking on PSVR2, VIVE, and other new HMDs remains unverified.

The installer includes the tuner, common gaze provider, OpenVR geometry helper, bilingual instructions, and a read-only setup check. It has built-in exact NVIDIA driver contracts for 610.88 (standard route) and 596.02 (Frame session). Other exact image pairs can be tested only with an explicitly selected [experimental driver contract](docs/DRIVER-CONTRACTS.md); the Frame session remains 596.02-only. VRSS must be allowed in the VRChat NVIDIA profile, SteamVR and gaze input must be available, and MSAA must be enabled in VRChat. Unmatched driver builds are refused because the provider uses an undocumented driver interface. Passing a setup check does not establish that the foveated region tracks correctly in a headset.

For headset-specific eye SDKs and wireless PCVR routes, see [HMD gaze compatibility](docs/HMD-GAZE-COMPATIBILITY.md). The generic SteamVR eye-action bridge remains experimental; the Frame session uses a separate OpenVR foveation-center API and a Frame-specific registration path.

[PICO direct input](docs/PICO.md) uses an independent subscription to PicoFacialBridge 0.6.0 or later without VRCFT or a relay, alongside the existing avatar session. Tuner includes source selection and saved headset IPv4 settings; development packaging includes a self-contained NativeAOT client. Bridge 0.5.0 lacks this protocol. The included [616.92 exact driver contract](docs/DRIVER-CONTRACTS.md#included-61692-experimental-contract) requires explicit opt-in; default driver selection and existing profile prerequisites are unchanged.

## Steam Frame (596.02, experimental)

Use the [Frame-specific instructions](docs/STEAM-FRAME.md), not the legacy EDID setup check or Start-Provider. Enable SteamVR eye-tracking sharing, VR layout 4 and MSAA; close VRChat and Unity, select **Steam Frame > Connect / Retry**, then **Launch VRChat (Frame)**. Vertical conversion is built in; start with Common provider gains X=1, Y=1.

## Get started

1. Download the Windows installer from [Releases](https://github.com/challenger0303/VRSS-Tuner/releases). It installs per-user to a fixed location and registers SteamVR autostart if SteamVR is already running. It never launches SteamVR itself. The portable ZIP is retained for development and manual diagnostics.
2. For the standard route, connect the HMD, start SteamVR, select your gaze source in Tuner, then click **Start gaze path**. Tuner checks the setup and starts the provider without launching VRChat. For Steam Frame, follow the Frame-specific guide above.
3. Launch VRChat normally through Steam with MSAA enabled and follow the [setup guide](docs/SETUP.md) to verify gaze reception.

To use Steam's normal VRChat Play button with a fixed HMD ID, set the launch option shown by `Install-Steam-Launch-Bridge.cmd` once. The installer copies that bridge to a stable location. On upgrades while VRChat is running, its active bridge is preserved and refreshed at the next SteamVR start after VRChat exits. On the developer's PC, SteamVR started Tuner and SRanibro automatically; a normal Steam VRChat launch then connected to the provider. Other PCs remain unverified.

If the check reports an unsupported driver or missing VRSS allow setting, do not force the provider to start. Open an issue with the diagnostic output, driver version, and HMD model; remove personally identifying information first.

## Build from source

The managed clients require the .NET SDK. PICO projects default to .NET 9; `-PicoTargetFramework net10.0` selects .NET 10 for the PICO client in the packaging scripts.

Install Visual Studio 2022 with **Desktop development with C++** and Inno Setup 6. From PowerShell run `vrss-tuner\test.ps1` for isolated tests or `vrss-tuner\Build-Portable.ps1` for a development ZIP. For a full release candidate, run `vrss-tuner\Build-ReleaseCandidate.ps1`: it runs the tests, requires both the locally built Varjo bridge and Pimax eye-device DLL, verifies every file inside the ZIP against `bundle-manifest.json`, then builds the installer and writes checksums and a build report. The Pimax DLL must be at `pimax-eye-bridge/bin/driver_sranibro_eye.dll` or supplied with `-PimaxBridgeDll`; the Varjo executable must be at `varjo-gaze-bridge/build/varjo_gaze_bridge.exe` or built with `-VarjoSdk`. Both are ignored build outputs, so a clean clone needs these SDK-dependent inputs. The build scripts locate `vcvars64.bat` with `vswhere`; `VCVARS64_BAT` can override it. The repository contains the necessary NVAPI and OpenVR build files under `third_party` with their own notices.

The project code is MIT licensed. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). NVIDIA and Valve components retain their respective licenses.

Switching common inputs keeps NVIDIA registration alive. Stop fully exits gaze providers; restarting after a full Stop may require a normal VRChat restart. Baballonia OSC reception continues when Tuner is closed. See the setup guide for beta.5 verification and remaining limits.
