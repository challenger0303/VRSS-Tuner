param([ValidateSet('Pimax','Baballonia','PSVR2Toolkit','SRanibro','ViveProEye','ViveStreaming','SteamVREye','Varjo')]
      [string]$InputRoute='Pimax', [switch]$Stop,
      [ValidateRange(1,65535)][int]$OscPort=9020,
      [ValidateRange(0,65535)][int]$OscForward=8888,
      [ValidateSet('open','close')][string]$PimaxSenderAction,[switch]$ForceStopOwnedProvider)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Gaze-Route.ps1')
if ($PimaxSenderAction) { Set-PimaxGazeSender $PimaxSenderAction -NoElevation; return }
if ($ForceStopOwnedProvider) { Stop-OwnedGazeProvider -NoElevation; return }
$data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
New-Item -ItemType Directory -Path $data -Force | Out-Null
$paused = Join-Path $data 'gaze-paused.txt'
$errorFile = Join-Path $data 'route-error.txt'
# Stop must cancel an in-progress start even while that operation owns the lock.
if ($Stop) { [IO.File]::WriteAllText($paused,'User requested stop',[Text.Encoding]::ASCII) }
$wasPaused = Test-Path -LiteralPath $paused -PathType Leaf
$started = $false
$mutex = [Threading.Mutex]::new($false,'Local\VRSS-Tuner-Route-Operation')
$owned = $false
try {
    try { $owned = $mutex.WaitOne($(if ($Stop) { 20000 } else { 0 })) }
    catch [Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { throw 'A gaze route operation is already in progress.' }
    [IO.File]::WriteAllText($errorFile,'',[Text.Encoding]::UTF8)
    if ($Stop) {
        [IO.File]::WriteAllText($paused,'User requested stop',[Text.Encoding]::ASCII)
        # Attempt both independently; a stale run record must not prevent the
        # native sender from stopping as well.
        $failures = @()
        try { Stop-CommonGazeRoute } catch { $failures += $_.Exception.Message }
        try { Set-PimaxGazeSender close } catch { $failures += $_.Exception.Message }
        if ($failures.Count) { throw ($failures -join "`n") }
        return
    }
    if ($InputRoute -eq 'SRanibro' -and -not (Get-Process SRanibro -ErrorAction SilentlyContinue)) {
        throw 'SRanibro is not running. Start its gaze stream first.'
    }
    if ($InputRoute -eq 'Baballonia') {
        $settingsPath = Join-Path $data 'osc-settings.txt'
        if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
            $saved = @(Get-Content -LiteralPath $settingsPath)
            if ($saved.Count -eq 2 -and $saved[0] -match '^[0-9]{1,5}$' -and $saved[1] -match '^[0-9]{1,5}$') {
                if (-not $PSBoundParameters.ContainsKey('OscPort') -and [int]$saved[0] -ge 1 -and [int]$saved[0] -le 65535) { $OscPort = [int]$saved[0] }
                if (-not $PSBoundParameters.ContainsKey('OscForward') -and [int]$saved[1] -le 65535) { $OscForward = [int]$saved[1] }
            }
        }
    }
    if ($InputRoute -eq 'Baballonia' -and ($OscPort -eq $OscForward -or $OscPort -eq 5556 -or $OscForward -eq 5556)) {
        throw 'OSC receive, forward, and provider ports must be different.'
    }
    Remove-Item -LiteralPath $paused -ErrorAction SilentlyContinue
    $sameRoute = (Get-ActiveGazeRoute) -eq $InputRoute
    $activeRun = Get-ActiveGazeRun
    $runtimeVersion = if ($activeRun) { Join-Path $activeRun 'route-runtime-version.txt' } else { '' }
    $sameRuntime = $runtimeVersion -and (Test-Path -LiteralPath $runtimeVersion -PathType Leaf) -and
        ((Get-Content -LiteralPath $runtimeVersion -Raw).Trim() -eq '3')
    $activeReady = $false
    if ($activeRun) {
        $statusFile = Join-Path $activeRun 'status.json'
        if (Test-Path -LiteralPath $statusFile -PathType Leaf) {
            try {
                $status = Get-Content -LiteralPath $statusFile -Raw | ConvertFrom-Json
                $age = ((Get-Date).ToUniversalTime() - (Get-Item -LiteralPath $statusFile).LastWriteTimeUtc).TotalSeconds
                $activeReady = $status.registered -and [int]$status.pid -in @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue | ForEach-Object { $_.Id }) -and $age -ge 0 -and $age -lt 5
                if ($status.inputRoute) { $sameRoute = $status.inputRoute -eq $InputRoute }
            } catch { }
        }
    }
    if ($InputRoute -eq 'Baballonia') {
        $settings = "$OscPort`n$OscForward`n"
        $sameRoute = $sameRoute -and $activeReady -and [int]$status.inputPort -eq $OscPort -and [int]$status.oscForward -eq $OscForward
    }
    if ((Get-Process common_vrss_provider -ErrorAction SilentlyContinue) -and
        $sameRoute -and $sameRuntime -and $activeReady -and $InputRoute -ne 'Pimax' -and
        (Test-GazeSourceBridge $activeRun $InputRoute) -and
        -not (Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue)) {
        $started = $true
        return
    }
    if ($InputRoute -ne 'Pimax' -and $sameRuntime -and $activeReady) {
        & (Join-Path $PSScriptRoot 'Start-Provider.ps1') -InputOnly -RunDirectory $activeRun -InputRoute $InputRoute `
            -OscPort $OscPort -OscForward $OscForward | Out-Null
        $started = $true
        return
    }
    Stop-CommonGazeRoute
    if ($InputRoute -eq 'Pimax') {
        Set-PimaxGazeSender open
    } else {
        & (Join-Path $PSScriptRoot 'Start-Common-Session.ps1') -InputRoute $InputRoute -ProviderOnly `
            -OscPort $OscPort -OscForward $OscForward | Out-Null
    }
    $started = $true
    if (Test-Path -LiteralPath $paused -PathType Leaf) {
        Stop-CommonGazeRoute
        Set-PimaxGazeSender close
    }
} catch {
    if ($owned -and $wasPaused -and -not $started) { [IO.File]::WriteAllText($paused,'Previous start failed; remain stopped',[Text.Encoding]::ASCII) }
    if ($owned) { [IO.File]::WriteAllText($errorFile,$_.Exception.Message,[Text.UTF8Encoding]::new($true)) }
    throw
} finally {
    if ($owned -and $started -and $InputRoute -eq 'Baballonia' -and -not $Stop) {
        [IO.File]::WriteAllText((Join-Path $data 'osc-settings.txt'),"$OscPort`n$OscForward`n",[Text.Encoding]::ASCII)
    }
    if ($owned) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
