# Audited driver contracts

The common provider selects an exact NVAPI/UMD image pair before creating the
gaze handler or calling private registration methods. Unknown or mixed images
are rejected by default; the marketing version alone is not sufficient.

## Additional driver builds

### Included 616.92 experimental contract

`common-vrss-provider/driver-contracts/616.92.txt` contains an opt-in contract
for one exact 616.92 NVAPI implementation/UMD pair. Its eight method addresses,
structure layouts and UMD challenge-response algorithm have been checked.
A separate process successfully obtained a challenge, registered a test routing
key, queried zero consumers and unregistered; its registration event appeared
and was removed. A follow-up passed listener setup and one center-point
UpdateGazeData ABI call. The provider active flag returned to zero on unregister,
and UMD closed the borrowed listener handle. The existing XOR mask is unchanged.

From a development portable bundle, run the read-only check:

```powershell
.\Test-Driver-Contract.cmd
```

This selects the contract only in that check process. It does not change the
shell environment, start a normal provider, or register anything. In a source
checkout, supply `-ContractFile` and `-ProviderExe` with the corresponding paths.
To use the contract in an actual session, select its absolute path through
`VRSS_EXPERIMENTAL_DRIVER_CONTRACT` in that session's process tree as described
below. The default driver list is unchanged.

Live PICO testing on 2026-10-28 also connected one consumer in normally
EAC-protected VRChat with SteamVR 2.18.4 and actual OpenVR HMD geometry.
Two sessions recorded 3,503 and 8,966 successful gaze submissions with zero
submission errors; the second session started with MSAA x4. Both normally
unregistered.
The wearer could not identify a clear moving quality region, so visual
rendering and alignment remain unconfirmed. UpdateGazeData does not propagate
the lower dispatch status in this driver; a zero return alone is not rendered
output evidence. See [PICO validation](PICO.md#validation).

### Auditing another image pair

Run `Audit-Driver.cmd` from the portable folder on a PC with the target driver.
It saves the loaded NVAPI and UMD SHA-256 hashes, the gaze-handler vtable RVA,
and eight method RVAs under `%LOCALAPPDATA%\VRSS Tuner\driver-audits`. This
uses the public gaze-handler initialization and reads addresses only; it never
registers a provider or calls the private methods. A successful audit is **not**
evidence that those private methods still have the same ABI or that VRSS2 renders
correctly in VRChat.

The standard build continues to allow only the audited image pairs below. For
an independently reviewed additional build, an advanced tester can opt in by
setting `VRSS_EXPERIMENTAL_DRIVER_CONTRACT` to an **absolute path** containing
these seven UTF-8/ASCII fields (one per line, no comments or duplicate keys):

```text
format=1
version=<displayed driver version>
module=<nvapi64.dll or nvapi64_impl.dll>
api_sha256=<64 lowercase hex characters for that module>
umd_sha256=<64 lowercase hex characters for nvwgf2umx.dll>
vtable_rva=<nonzero 0x-prefixed hex from audit>
method_rvas=<eight comma-separated nonzero 0x-prefixed hex RVAs from audit>
```

For example, launch PowerShell with the variable set in the **same process
tree** as the tuner or provider. `common_vrss_provider.exe --driver-check-only 1`
then checks the exact images, initializes the public gaze handler, and verifies
the vtable and all eight method addresses before any private call. A mismatch
stops the provider. This opt-in does not certify the private method semantics;
verify the challenge/registration ABI independently and test VRChat with
Force/Off before treating a new contract as supported. The EDID-free Steam
Frame session remains restricted to its separately verified 596.02 contract.

| Driver | Implementation | SHA-256 |
| --- | --- | --- |
| 596.02 | `nvapi64.dll` | `a902fe11d59166285c264aea4695d1978eb6ce6cf0d4c477fcddcac1527ddfb2` |
| 596.02 | `nvwgf2umx.dll` | `ec0cb8ade4bf81e6b6835832e7e4d665d59600c616967577fae162455edb505c` |
| 610.88 | `nvapi64_impl.dll` | `df6455ccf83e43cfe68f405af1eec4e053c7f95da998bf358053b7583980c2f4` |
| 610.88 | `nvwgf2umx.dll` | `04c01cfc849e5b58ad50da89dc83bff15f15847684de7964ff5900fedfab18fe` |

The 610.88 hashes and eight method RVAs are retained from upstream. The 596.02
contract was traced in the installed binaries and checked at runtime; its
vtable RVA is additionally pinned to `0x480280`. Both paths check that the
complete table lies within the selected module before reading its eight slots,
then compare every method against that version's audited RVAs.

`common-vrss-provider/test-driver-identity.ps1` tests selection, mixed/missing
images, changed methods, ASLR, table bounds, arithmetic overflow, and strict
experimental-contract parsing without loading NVIDIA. Built-in constants are
in `driver_identity.h`; no driver binaries are distributed or modified.
