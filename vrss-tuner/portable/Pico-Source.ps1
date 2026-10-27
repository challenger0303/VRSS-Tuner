# PICO source configuration and client launch; no headset deployment.
function Test-PicoHeadsetIp([string]$HeadsetIp) {
    if ($HeadsetIp -notmatch '^(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})$') { return $false }
    foreach ($part in $HeadsetIp.Split('.')) { if ([int]$part -gt 255) { return $false } }
    return $true
}

function Get-PicoHeadsetIp([string]$DataDirectory,[string]$HeadsetIp) {
    $path = Join-Path $DataDirectory 'pico-headset-ip.txt'
    if (-not $HeadsetIp -and (Test-Path -LiteralPath $path -PathType Leaf)) {
        $HeadsetIp = ([IO.File]::ReadAllText($path)).TrimEnd("`r", "`n")
    }
    if (-not (Test-PicoHeadsetIp $HeadsetIp)) {
        throw 'Enter and save the PICO headset IPv4 address in Tuner, or supply -HeadsetIp. PicoFacialBridge 0.6.0 or later with direct gaze subscriptions is required; Bridge 0.5.0 is not supported.'
    }
    return $HeadsetIp
}

function Save-PicoHeadsetIp([string]$DataDirectory,[string]$HeadsetIp) {
    $value = Get-PicoHeadsetIp $DataDirectory $HeadsetIp
    New-Item -ItemType Directory -Path $DataDirectory -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $DataDirectory 'pico-headset-ip.txt'),$value + "`n",[Text.Encoding]::ASCII)
    return $value
}

function Start-PicoGazeClient([string]$Bundle,[string]$RunDirectory,[string]$Calibration,[string]$HeadsetIp) {
    $bridge = Join-Path $Bundle 'pico-gaze-bridge\PicoGazeBridge.exe'
    $arguments = '--headset-ip ' + (Get-PicoHeadsetIp $RunDirectory $HeadsetIp) +
        ' --headset-port 9030 --geometry "' + (Join-Path $RunDirectory 'geometry.txt') +
        '" --config "' + $Calibration + '" --port 5556 --seconds 86400 --stop-file "' +
        (Join-Path $RunDirectory 'input-stop.request') + '"'
    $child = Start-Process -FilePath $bridge -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $RunDirectory 'pico.stdout.txt') `
        -RedirectStandardError (Join-Path $RunDirectory 'PICO-bridge.stderr.txt')
    $null = $child.Handle
    $child.Id | Set-Content -LiteralPath (Join-Path $RunDirectory 'pico-bridge.pid') -Encoding ASCII
    return $child
}
