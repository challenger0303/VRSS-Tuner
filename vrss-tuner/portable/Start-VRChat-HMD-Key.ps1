param([string]$VRChatDirectory='C:\Program Files (x86)\Steam\steamapps\common\VRChat',
      [int]$Profile=0,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$bundle=$PSScriptRoot
$preflight=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\preflight\geometry.txt'
if (-not (Test-Path -LiteralPath $preflight -PathType Leaf)) {
    throw 'Run Test-Setup.cmd with the connected HMD first.'
}
$geometry=@{}
foreach ($line in Get-Content -LiteralPath $preflight) {
    $parts=$line -split '=',2
    if ($parts.Count -eq 2) { $geometry[$parts[0]]=$parts[1] }
}
$source=$geometry['identitySource']
if ($source -notin @('openvr-edid','candidate-auto','candidate-manual','tuner-session')) {
    throw 'Preflight does not contain a supported HMD identity.'
}
$vendor=[int]$geometry['vendor'];$product=[int]$geometry['product']
if ($vendor -lt 1 -or $vendor -gt 65535 -or $product -lt 1 -or $product -gt 65535) {
    throw 'Invalid HMD identity.'
}
if ($source -eq 'candidate-manual' -and
    $env:VRSS_HMD_ID_CANDIDATE -ne ('{0}:{1}' -f $vendor,$product)) {
    throw 'Candidate environment does not match the last preflight. Rerun Test-Setup.cmd.'
}
if ($source -eq 'candidate-auto' -and $env:VRSS_HMD_ID_CANDIDATE) {
    throw 'Manual candidate override appeared after auto preflight. Rerun Test-Setup.cmd.'
}
$exporter=Join-Path $bundle 'common-vrss-provider\export_openvr_geometry.exe'
$currentPath=Join-Path (Split-Path -Parent $preflight) ("current-identity-$PID.txt")
try {
    $exportArgs=@($currentPath)
    if ($source -eq 'candidate-manual') {$exportArgs+=@('--candidate-id',$vendor,$product)}
    if ($source -eq 'tuner-session') {$exportArgs+='--common-session-key'}
    $exportOutput=& $exporter @exportArgs 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $currentPath -PathType Leaf)) {
        throw "Could not recheck the connected HMD: $exportOutput"
    }
    $current=@{}
    foreach ($line in Get-Content -LiteralPath $currentPath) {
        $parts=$line -split '=',2
        if ($parts.Count -eq 2) {$current[$parts[0]]=$parts[1]}
    }
    if ($current['identitySource'] -ne $source -or
        $current['vendor'] -ne "$vendor" -or $current['product'] -ne "$product") {
        throw 'The connected HMD identity changed after preflight. Rerun Test-Setup.cmd.'
    }
    if ($source -eq 'candidate-auto' -and -not $geometry['identityFingerprint']) {
        throw 'Automatic candidate preflight lacks a stable HMD fingerprint.'
    }
    if ($geometry['identityFingerprint'] -and
        $current['identityFingerprint'] -ne $geometry['identityFingerprint']) {
        throw 'A different HMD is connected. Rerun Test-Setup.cmd.'
    }
    foreach ($field in @('projection0','projection1','eyeToHead0','eyeToHead1',
                         'eyeTranslation0','eyeTranslation1')) {
        if ($current[$field] -ne $geometry[$field]) {
            throw "The connected HMD geometry changed ($field). Rerun Test-Setup.cmd."
        }
    }
} finally {
    Remove-Item -LiteralPath $currentPath -ErrorAction SilentlyContinue
}
$detailsFile=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\preflight\candidate-readiness.txt'
$tuner=Join-Path $bundle 'VRSS Tuner.exe'
$check=Start-Process -FilePath $tuner -ArgumentList ('--readiness "'+$detailsFile+'"') -WindowStyle Hidden -Wait -PassThru
if ($check.ExitCode -ne 0) {throw 'Could not read NVIDIA profile readiness.'}
$details=@{}
foreach ($line in Get-Content -LiteralPath $detailsFile) {
    $parts=$line -split '=',2
    if ($parts.Count -eq 2) { $details[$parts[0]]=$parts[1] }
}
if ($details['hmd_test_branch'] -ne 'enabled') {
    throw 'The NVIDIA VRChat profile has not enabled the audited HMD test branch.'
}
$game=[IO.Path]::GetFullPath($VRChatDirectory)
$target=Join-Path $game 'start_protected_game.exe'
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {throw 'VRChat protected launcher not found.'}
if ((Get-AuthenticodeSignature -LiteralPath $target).Status -ne 'Valid') {
    throw 'VRChat protected launcher signature invalid.'
}
if (-not $ValidateOnly -and (Get-Process VRChat -ErrorAction SilentlyContinue)) {
    throw 'VRChat is already running; close it normally first.'
}
$plan=[ordered]@{hmdVendor=$vendor;hmdProduct=$product;
    identitySource=$source;identityFingerprint=$geometry['identityFingerprint'];target=$target;
    hmdTestBranch=$true;launchMode='protected';changesDriverProfile=$false}
if ($ValidateOnly) {$plan | ConvertTo-Json;return}
if (Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue) {
    throw 'The official gaze provider is running; do not launch a second route.'
}
if (-not (Get-Process common_vrss_provider -ErrorAction SilentlyContinue)) {
    throw 'Start the matching common gaze provider before VRChat.'
}
$runFile=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\active-run.txt'
if (-not (Test-Path -LiteralPath $runFile -PathType Leaf)) {throw 'No active provider run found.'}
$run=(Get-Content -LiteralPath $runFile -Raw).Trim()
$allowed=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'VRSS Tuner\runs')).TrimEnd('\')+'\'
if (-not [IO.Path]::GetFullPath($run).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Invalid active provider run path.'
}
$activeGeometry=Join-Path $run 'geometry.txt'
if (-not (Test-Path -LiteralPath $activeGeometry -PathType Leaf)) {throw 'Active provider geometry missing.'}
$activeText=Get-Content -LiteralPath $activeGeometry
if ($activeText -notcontains "identitySource=$source" -or
    $activeText -notcontains "vendor=$vendor" -or $activeText -notcontains "product=$product") {
    throw 'The running provider does not use this HMD identity.'
}
if ($geometry['identityFingerprint'] -and
    $activeText -notcontains "identityFingerprint=$($geometry['identityFingerprint'])") {
    throw 'The running provider belongs to a different HMD.'
}
$oldVendor=[Environment]::GetEnvironmentVariable('TEST_VENDOR_ID','Process')
$oldProduct=[Environment]::GetEnvironmentVariable('TEST_PRODUCT_ID','Process')
try {
    [Environment]::SetEnvironmentVariable('TEST_VENDOR_ID',('0x{0:X}' -f $vendor),'Process')
    [Environment]::SetEnvironmentVariable('TEST_PRODUCT_ID',('0x{0:X}' -f $product),'Process')
    $process=Start-Process -FilePath $target -ArgumentList @("--profile=$Profile") -WorkingDirectory $game -PassThru
} finally {
    [Environment]::SetEnvironmentVariable('TEST_VENDOR_ID',$oldVendor,'Process')
    [Environment]::SetEnvironmentVariable('TEST_PRODUCT_ID',$oldProduct,'Process')
}
$plan.launcherPid=$process.Id
$plan | ConvertTo-Json
