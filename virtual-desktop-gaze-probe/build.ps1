$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here '..\vrss-tuner\Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$output = Join-Path $here 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$command = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /W4 "' +
    (Join-Path $here 'probe.cpp') + '" /link /out:"' + (Join-Path $output 'virtual-desktop-gaze-probe.exe') + '"'
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Virtual Desktop gaze probe build failed.' }
Write-Output (Join-Path $output 'virtual-desktop-gaze-probe.exe')
