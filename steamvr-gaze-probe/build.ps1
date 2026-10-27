$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here '..\vrss-tuner\Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$sdk = Join-Path $here '..\third_party\openvr'
$output = Join-Path $here 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$command = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /W4 "' +
    (Join-Path $here 'probe.cpp') + '" /link /out:"' + (Join-Path $output 'steamvr-gaze-probe.exe') +
    '" "' + (Join-Path $sdk 'openvr_api.lib') + '"'
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'SteamVR gaze probe build failed.' }
Copy-Item -LiteralPath (Join-Path $sdk 'openvr_api.dll') -Destination $output -Force
Copy-Item -LiteralPath (Join-Path $here 'eye_actions.json') -Destination $output -Force
Write-Output (Join-Path $output 'steamvr-gaze-probe.exe')
