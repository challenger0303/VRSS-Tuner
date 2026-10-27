$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
New-Item -ItemType Directory -Path (Join-Path $here 'build') -Force | Out-Null
$inputExe = Join-Path $here 'build\gaze_input_test.exe'
$inputBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $here 'gaze_input_test.cpp') + '" /Fo"' + (Join-Path $here 'build\gaze_input_test.obj') + '" /Fe"' + $inputExe + '"'
& cmd.exe /d /c $inputBuild
if ($LASTEXITCODE -ne 0) { throw 'Gaze input compilation failed.' }
& $inputExe
if ($LASTEXITCODE -ne 0) { throw 'Gaze input network regression tests failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-start-provider.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell provider launch smoke test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-gaze-route.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell gaze route switch test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-input-recovery.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell input recovery test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-route-concurrency.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell route concurrency test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-start-common-session.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell common-session smoke test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-portable-hash.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Windows PowerShell portable hash test failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'test-steam-launch-bridge.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Steam launch bridge smoke test failed.' }
& (Join-Path (Split-Path -Parent $here) 'common-vrss-provider\test-driver-identity.ps1')
& (Join-Path $here 'test-frame-runtime.ps1')
& (Join-Path $here 'test-frame-gate.ps1')
& (Join-Path $here 'test-frame-launch.ps1')
$research = Split-Path -Parent $here
$identityExe = Join-Path $here 'build\hmd_candidate_identity_test.exe'
$identityBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /I"' +
    $research + '" "' + (Join-Path $research 'common-vrss-provider\hmd_candidate_identity_test.cpp') +
    '" /Fo"' + (Join-Path $here 'build\hmd_candidate_identity_test.obj') + '" /Fe"' + $identityExe + '"'
& cmd.exe /d /c $identityBuild
if ($LASTEXITCODE -ne 0) { throw 'HMD identity test compilation failed.' }
& $identityExe
if ($LASTEXITCODE -ne 0) { throw 'HMD identity tests failed.' }
$exe = Join-Path $here 'build\gaze_settings_test.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /DUNICODE /D_UNICODE "' +
    (Join-Path $here 'gaze_settings_test.cpp') + '" /Fo"' + (Join-Path $here 'build\gaze_settings_test.obj') +
    '" /Fe"' + $exe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Gaze test compilation failed.' }
$fixture = Join-Path $here ('build\gaze-tests-' + (Get-Date -Format 'yyyyMMddTHHmmssfff'))
& $exe $fixture
if ($LASTEXITCODE -ne 0) { throw 'Gaze settings tests failed.' }
$profileExe = Join-Path $here 'build\VRSS Tuner.profile-test.exe'
& (Join-Path $here 'build.ps1') -OutputPath $profileExe | Out-Null
$monitorRun = Start-Process -FilePath $profileExe -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
if ($monitorRun.ExitCode -ne 0) { throw "Gaze route self-test failed: $($monitorRun.ExitCode)" }
Write-Output 'GAZE_ROUTE_PASS all_sources unknown_route'
$profileFolder = Join-Path $fixture 'isolated-profile-data'
$profileRun = Start-Process -FilePath $profileExe -ArgumentList ('--profile-self-test "' + $profileFolder + '"') -WindowStyle Hidden -Wait -PassThru
if ($profileRun.ExitCode -ne 0) { throw "Profile round-trip failed: $($profileRun.ExitCode)" }
Write-Output 'PROFILE_STORAGE_PASS isolated_roundtrip names values'
$localizationExe = Join-Path $here 'build\localization_test.exe'
$localizationBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $here 'localization_test.cpp') + '" /Fo"' + (Join-Path $here 'build\localization_test.obj') +
    '" /Fe"' + $localizationExe + '"'
& cmd.exe /d /c $localizationBuild
if ($LASTEXITCODE -ne 0) { throw 'Localization test compilation failed.' }
& $localizationExe
if ($LASTEXITCODE -ne 0) { throw 'Localization test failed.' }
$regionExe = Join-Path $here 'build\region_estimate_test.exe'
$regionBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $here 'region_estimate_test.cpp') + '" /Fo"' + (Join-Path $here 'build\region_estimate_test.obj') + '" /Fe"' + $regionExe + '"'
& cmd.exe /d /c $regionBuild
if ($LASTEXITCODE -ne 0) { throw 'Region model compilation failed.' }
& $regionExe
if ($LASTEXITCODE -ne 0) { throw 'Region model tests failed.' }
$relayExe = Join-Path $here 'build\baballonia_relay_test.exe'
$relayBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $here 'baballonia_relay_test.cpp') + '" /Fo"' + (Join-Path $here 'build\baballonia_relay_test.obj') +
    '" /Fe"' + $relayExe + '"'
& cmd.exe /d /c $relayBuild
if ($LASTEXITCODE -ne 0) { throw 'Baballonia relay compilation failed.' }
& $relayExe
if ($LASTEXITCODE -ne 0) { throw 'Baballonia relay tests failed.' }
$telemetryExe = Join-Path $here 'build\provider_telemetry_test.exe'
$telemetryBuild = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $here 'provider_telemetry_test.cpp') + '" /Fo"' + (Join-Path $here 'build\provider_telemetry_test.obj') +
    '" /Fe"' + $telemetryExe + '"'
& cmd.exe /d /c $telemetryBuild
if ($LASTEXITCODE -ne 0) { throw 'Provider telemetry test compilation failed.' }
& $telemetryExe
if ($LASTEXITCODE -ne 0) { throw 'Provider telemetry tests failed.' }
. (Join-Path $here 'StartupState.ps1')
$research = Join-Path $fixture 'startup-fixture'
$tuner = Join-Path $research 'vrss-tuner'
New-Item -ItemType Directory -Path $tuner -Force | Out-Null
if ($null -ne (Get-VrssTunerStartupState -ResearchRoot $research)) { throw 'Missing state must retain legacy startup.' }
$calibration = "left_deg=-2.000000`nright_deg=2.000000`nx_gain=0.500000`ny_gain=0.550000`n"
$temp = Join-Path $tuner 'temp-calibration.txt'
[IO.File]::WriteAllText($temp, $calibration, [Text.Encoding]::ASCII)
$sha = (Get-FileHash -LiteralPath $temp).Hash.ToLowerInvariant()
$snapshot = Join-Path $tuner ('startup-pimax-calibration-' + $sha + '.txt')
Copy-Item -LiteralPath $temp -Destination $snapshot
$body = "format=1`nmode=1`narea=1084227584`nlayout=4`ncalibration_sha256=$sha`n"
$stateFile = Join-Path $tuner 'startup-state.ini'
[IO.File]::WriteAllText($stateFile, $body, [Text.Encoding]::ASCII)
$state = Get-VrssTunerStartupState -ResearchRoot $research
if ($state.nvidia.autovrs_settings -ne 1 -or $state.nvidia.fovea_area_percent -ne 1084227584 -or
    $state.nvidia.vr_only_mode -ne 4 -or $state.calibrationSha256 -ne $sha) { throw 'Startup values did not round-trip.' }
foreach ($invalid in @($body.Replace('mode=1','mode=4'), $body.Replace('area=1084227584','area=2143289344'), ($body + "format=1`n"), $body.Replace('layout=4','layout=4294967296'))) {
    [IO.File]::WriteAllText($stateFile, $invalid, [Text.Encoding]::ASCII)
    $rejected = $false
    try { $null = Get-VrssTunerStartupState -ResearchRoot $research } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid startup state accepted.' }
}
[IO.File]::WriteAllText($stateFile, $body, [Text.Encoding]::ASCII)
[IO.File]::AppendAllText($snapshot, 'tamper', [Text.Encoding]::ASCII)
$rejected = $false
try { $null = Get-VrssTunerStartupState -ResearchRoot $research } catch { $rejected = $true }
if (-not $rejected) { throw 'Tampered snapshot accepted.' }
Write-Output 'STARTUP_STATE_PASS missing_state roundtrip bounds duplicate_fields tampered_snapshot'
Write-Output "Fixtures retained: $fixture"
