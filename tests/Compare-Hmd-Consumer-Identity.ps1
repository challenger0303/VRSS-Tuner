param([switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$research=Join-Path $PSScriptRoot '..\..\..\work\dfr-research\nvwgf2umx-vrss-ghidra-20261023'
$probe=Join-Path $research 'output\read_vrss_manual_reset_events.exe'
$watch=Join-Path $research 'output\probe_global_vrss_objects.exe'
$verify=Join-Path $research 'verify_live_bridge_names.py'
$launcher='C:\Program Files (x86)\Steam\steamapps\common\VRChat\start_protected_game.exe'
foreach($file in @($probe,$watch,$verify,$launcher)) {
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing prerequisite: $file"}
}
if((Get-AuthenticodeSignature -LiteralPath $launcher).Status -ne 'Valid') {throw 'Protected launcher signature invalid'}
if(Get-Process VRChat -ErrorAction SilentlyContinue) {throw 'VRChat is already running; nothing changed.'}
$provider=@(Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue)
if($provider.Count -ne 1 -or -not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
    throw 'One official provider and SteamVR are required.'
}
$snapshot=& $watch --snapshot
if($LASTEXITCODE -ne 0){throw 'NVIDIA bridge snapshot failed'}
$bridge=($snapshot | python $verify - | Out-String | ConvertFrom-Json)
if($LASTEXITCODE -ne 0 -or -not $bridge.all_derived_names_present -or -not $bridge.edid_match) {
    throw 'NVIDIA bridge names do not match the live HMD.'
}
$realVendor=[int]$bridge.name_edid_vid_pid_decimal[0]
$realProduct=[int]$bridge.name_edid_vid_pid_decimal[1]
$providerName='Global\'+$bridge.provider_handshake_event
$consumerName='Global\'+$bridge.state_event
$plan=[ordered]@{realVendor=$realVendor;realProduct=$realProduct;
    providerPid=$provider[0].Id;cases=@('mismatch','match');
    launcher=$launcher;launchesProtectedGame=$true;changesDriverProfile=$false}
if($ValidateOnly){$plan|ConvertTo-Json -Depth 4;return}
$output=Join-Path $PSScriptRoot ('hmd-id-ab-'+(Get-Date -Format 'yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $output -ErrorAction Stop|Out-Null
$plan|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $output 'plan.json') -Encoding UTF8
function Run-Case([string]$name,[int]$vendor,[int]$product) {
    if(Get-Process VRChat -ErrorAction SilentlyContinue){throw 'VRChat must be closed before each case.'}
    $oldVendor=[Environment]::GetEnvironmentVariable('TEST_VENDOR_ID','Process')
    $oldProduct=[Environment]::GetEnvironmentVariable('TEST_PRODUCT_ID','Process')
    try {
        [Environment]::SetEnvironmentVariable('TEST_VENDOR_ID',('0x{0:X}' -f $vendor),'Process')
        [Environment]::SetEnvironmentVariable('TEST_PRODUCT_ID',('0x{0:X}' -f $product),'Process')
        $started=Start-Process -FilePath $launcher -ArgumentList @('--profile=0') -WorkingDirectory (Split-Path -Parent $launcher) -PassThru
    } finally {
        [Environment]::SetEnvironmentVariable('TEST_VENDOR_ID',$oldVendor,'Process')
        [Environment]::SetEnvironmentVariable('TEST_PRODUCT_ID',$oldProduct,'Process')
    }
    $deadline=(Get-Date).AddSeconds(60)
    do {
        $game=@(Get-Process VRChat -ErrorAction SilentlyContinue)
        if($game.Count -eq 1){break}
        Start-Sleep -Milliseconds 500
    } while((Get-Date)-lt $deadline)
    if($game.Count -ne 1){throw "$name VRChat did not launch"}
    try {
        # Allow the protected client to finish its anti-cheat and renderer startup.
        Start-Sleep -Seconds 35
        if(-not (Get-Process -Id $provider[0].Id -ErrorAction SilentlyContinue)) {throw 'Provider changed during test'}
        $observed=& $probe --watch 5 --names $providerName $consumerName
        if($LASTEXITCODE -ne 0){throw "$name event probe failed"}
        $observed|Set-Content -LiteralPath (Join-Path $output "$name.events.txt") -Encoding UTF8
        [ordered]@{name=$name;vendor=$vendor;product=$product;vrchatPid=$game[0].Id;
            launcherPid=$started.Id;eventOutput=$observed[-1]} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $output "$name.json") -Encoding UTF8
    } finally {
        $live=Get-Process -Id $game[0].Id -ErrorAction SilentlyContinue
        if($live) {
            $null=$live.CloseMainWindow()
            if(-not $live.WaitForExit(30000)){throw "$name VRChat did not close normally; second case skipped"}
        }
    }
}
Run-Case mismatch 65262 31268
Run-Case match $realVendor $realProduct
Write-Output $output
