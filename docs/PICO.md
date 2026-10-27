# PICO direct native-gaze input

PICO headsets with eye-tracking hardware can send a measured combined gaze direction through PicoFacialBridge 0.6.0 or later. The PC client subscribes directly, projects that head-relative ray into the current OpenVR eye views, and sends the common provider's 44-byte normalized UDP input. It does not require VRCFT or a relay.

Bridge 0.5.0 APKs do not implement the independent gaze protocol. Use [PicoFacialBridge 0.6.0 or later](https://github.com/WolalaQAQ/PicoFacialBridge/releases). The ordinary PICO 4 has no built-in eye tracker; Neo3 Pro Eye has not been hardware-validated with this adapter.

## Setup

1. Run the compatible Bridge on the headset with eye transmission enabled. Tuner does not deploy or upgrade the APK over ADB.
2. Connect the headset to PCVR and start SteamVR. The common-provider setup requirements still apply: a matching driver contract, VRSS-permitted VRChat profile, enabled VRSS/MSAA, readable HMD geometry and matching provider/game identity.
3. For the included experimental 616.92 pair, explicitly select the absolute path to `common-vrss-provider/driver-contracts/616.92.txt` through `VRSS_EXPERIMENTAL_DRIVER_CONTRACT`. See [driver contracts](DRIVER-CONTRACTS.md#included-61692-experimental-contract). This does not add 616.92 to the default driver list.
4. In **Gaze input**, select **PICO (direct Bridge 0.6.0+)**, enter the headset IPv4 address, and click **Save IP**, then **Start gaze path**. The address is stored in Tuner's configuration. The existing common-provider preflight and profile requirements are unchanged.
5. Launch VRChat using the existing common-provider workflow. Stop or switch the route with Tuner's source controls. They stop only the owned PICO client and send `PXR_GAZE_STOP`, not the avatar session's STOP command. Restart the PICO input after calibration changes.

`Start-PICO.cmd` runs the common-session workflow, which can start SteamVR and launch VRChat. It accepts a headset IPv4 address as its first argument or uses the saved address. For provider-only operation:

```powershell
.\Start-Common-Session.ps1 -InputRoute PICO -ProviderOnly -HeadsetIp 192.0.2.10
```

The 616.92 test-identity route requires the existing HMD test branch to be enabled and the same identity at provider registration and game launch. The adapter does not configure that driver profile; setup reports unmet prerequisites.

## Data path and coexistence

```text
PicoFacialBridge -- existing avatar session --> UnifiedPicoModule --> VRCFT
        |
        +-- independent gaze subscription --> PicoGazeBridge --> common provider
                 headset UDP9030               PC ephemeral       UDP5556
```

The two consumers use independent IDs, endpoints, leases and stop commands. The gaze client never binds PC9030. Headset replay covered independent start/stop, face-only continuity and the local eye-transmission switch. Separate runs exercised the actual PICO/Cymple modules and Bridge process restart. Real Cymple mouth input and complete new-data recovery after an internal tracking-service reopen remain unconfirmed.

The old `pico-gaze-probe --exclusive-test` still uses the avatar protocol and needs the normal PICO receiver stopped. It is separate from this direct subscriber.

## Protocol and projection

Bridge accepts ASCII datagrams without a trailing newline:

```text
PXR_GAZE_SUB id=<16 lowercase hex digits>
PXR_GAZE_ACK id=<same id> epoch=<16 lowercase hex digits> enabled=<0|1>
PXR_GAZE_STOP id=<same id>
```

The client renews once per second, requires a matching ACK before accepting data, and stops output after three seconds without a valid ACK. On timeout it changes subscription ID once before reconnecting, preventing delayed old ACKs from re-enabling the previous stream. Disabled, stale and duplicate native samples do not produce fresh output. Bridge's full protocol is documented in its `GAZE-PROTOCOL.md`.

The 83-byte native `E` packet stores combined validity at offset 27 and combined direction at offset 55. The client uses native measured gaze, not Avatar EyeLook expressions. PICO uses right/up/back axes with forward along negative Z. The exported eye-to-head rotation and projection map the combined direction into each eye view. This is one measured ray projected twice, not independent binocular gaze; fixation depth and parallax are not invented. Provider timestamps use Windows QPC while native source identity controls deduplication.

## Build

The PICO projects default to .NET 9. A .NET 10 developer can use:

```powershell
dotnet build pico-gaze-bridge/PicoGazeBridge.csproj -c Release -p:PicoTargetFramework=net10.0
dotnet run --project pico-gaze-tests/PicoGazeTests.csproj -c Release -p:PicoTargetFramework=net10.0
```

`Build-Portable.ps1` and `Build-ReleaseCandidate.ps1` accept `-PicoTargetFramework net10.0` for that client. Packaging uses self-contained NativeAOT, so end users need no separate .NET runtime for it.

## Validation

On 2026-10-28, current PICO/OpenVR geometry, NVIDIA 616.92, SteamVR 2.18.4 and normally EAC-protected VRChat 2026.4.1 produced `consumerCount=1` in two sessions. They recorded 3,503 and 8,966 successful real-gaze submissions with zero submission errors; the second session started with MSAA x4. Both normally unregistered.

Those sessions used matching manual `0xFEEE:0x6169` identities. The normal common-session key `0xFEEE:0x0001` has fixture coverage, not a separate live PICO game test. The wearer could not identify a visible quality-region change. Alignment, four-direction tracking and an Off/On rendered comparison remain unconfirmed. No game/EAC injection, hooking, debugger attachment, memory access, file modification or anti-cheat disabling was used.
