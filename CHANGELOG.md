# Changelog

## [Unreleased] - 2026-10-28

### Features
- Add a direct PicoFacialBridge gaze subscription client, combined-ray projection and normalized UDP output for the common provider.
- Add PICO source selection, saved headset IPv4 settings, source switching and client cleanup to Tuner.
- Include the self-contained NativeAOT PICO client and setup guide in portable/installer packaging.
- Add an opt-in exact NVIDIA 616.92 driver contract and a read-only contract checker. The default driver selection is unchanged.

### Design Rationale
- Keep gaze and avatar sessions independent; the PC client uses an ephemeral UDP port without VRCFT or a relay.
- Project the measured combined direction into each eye view without fabricating binocular measurements or fixation depth.
- Rotate subscription identity after ACK timeout and reject stale, invalid or unconfirmed samples.

### Notes & Caveats
- Requires PicoFacialBridge 0.6.0 or later, which implements the independent gaze protocol; Bridge 0.5.0 APKs do not support it.
- Two normal EAC-protected VRChat sessions on 616.92 reached one consumer and recorded 3,503 and 8,966 successful real-gaze submissions with zero submission errors. The second started with MSAA x4. The wearer could not identify a visible quality-region change.
- Actual module coexistence and Bridge process restart were exercised separately; real Cymple mouth input and complete data recovery after an internal tracking-service reopen remain unconfirmed.
- Final source integration is compile/static checked; its source-switching changes have not received a new runtime or headset test.
