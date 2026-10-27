$ErrorActionPreference = 'Stop'
$research = Split-Path -Parent $PSScriptRoot
. (Join-Path $research 'vrss-tuner\Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$build = Join-Path $research 'vrss-tuner\build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$testExe = Join-Path $build 'driver_identity_test.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 "' +
    (Join-Path $PSScriptRoot 'driver_identity_test.cpp') + '" /Fo"' +
    (Join-Path $build 'driver_identity_test.obj') + '" /Fe"' + $testExe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Driver identity test compilation failed.' }
& $testExe (Join-Path $PSScriptRoot 'driver-contracts\616.92.txt')
if ($LASTEXITCODE -ne 0) { throw 'Driver identity tests failed.' }
