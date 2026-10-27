$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$fixture = Join-Path $PSScriptRoot ('build\driver contract test-' + (Get-Date -Format 'yyyyMMddTHHmmssfff') + '-' + $PID)
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$exe = Join-Path $fixture 'provider fixture.exe'
$source = Join-Path $PSScriptRoot 'driver_contract_fixture.cpp'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT "' +
    $source + '" /Fo"' + (Join-Path $fixture 'fixture.obj') + '" /Fe"' + $exe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Driver contract fixture compilation failed.' }
$contract = Join-Path $fixture '616.92 contract.txt'
Copy-Item (Join-Path (Split-Path $PSScriptRoot) 'common-vrss-provider\driver-contracts\616.92.txt') $contract
$oldContract = $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT
$oldFailure = $env:VRSS_CONTRACT_FIXTURE_FAIL
$oldBuiltin = $env:VRSS_CONTRACT_FIXTURE_BUILTIN
try {
    $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT = 'parent-sentinel'
    Remove-Item Env:VRSS_CONTRACT_FIXTURE_FAIL -ErrorAction SilentlyContinue
    Remove-Item Env:VRSS_CONTRACT_FIXTURE_BUILTIN -ErrorAction SilentlyContinue
    $output = & (Join-Path $PSScriptRoot 'portable\Test-Driver-Contract.ps1') -ContractFile $contract -ProviderExe $exe
    $lines = ($output -join "`n") -split '\r?\n'
    if (-not ($lines -contains ('contractPath=' + $contract)) -or
        -not ($lines -contains 'EXPERIMENTAL_DRIVER_CHECK_PASS child_only no_registration')) {
        throw 'Child did not receive the exact contract path.'
    }
    if ($env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT -ne 'parent-sentinel') { throw 'Parent environment changed.' }
    $env:VRSS_CONTRACT_FIXTURE_BUILTIN = '1'
    $failed = $false
    try { & (Join-Path $PSScriptRoot 'portable\Test-Driver-Contract.ps1') -ContractFile $contract -ProviderExe $exe | Out-Null }
    catch { $failed = $true }
    if (-not $failed) { throw 'Built-in selection incorrectly verified the experimental file.' }
    Remove-Item Env:VRSS_CONTRACT_FIXTURE_BUILTIN
    $env:VRSS_CONTRACT_FIXTURE_FAIL = '1'
    $failed = $false
    try { & (Join-Path $PSScriptRoot 'portable\Test-Driver-Contract.ps1') -ContractFile $contract -ProviderExe $exe | Out-Null }
    catch { $failed = $true }
    if (-not $failed -or $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT -ne 'parent-sentinel') {
        throw 'Failure was not propagated or parent environment changed.'
    }
    Write-Output 'DRIVER_CONTRACT_LAUNCH_PASS child_environment parent_unchanged spaced_paths builtin_refused failure_propagated no_nvidia'
} finally {
    $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT = $oldContract
    $env:VRSS_CONTRACT_FIXTURE_FAIL = $oldFailure
    $env:VRSS_CONTRACT_FIXTURE_BUILTIN = $oldBuiltin
}
