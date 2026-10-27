param([ValidateSet('Baballonia','PSVR2Toolkit','SRanibro','ViveProEye','ViveStreaming','SteamVREye','Varjo')]
      [string]$InputRoute='Baballonia',
      [ValidateRange(0,30)][int]$HotAttachWaitSeconds=8,
      [switch]$ProviderOnly,[switch]$WaitForHeadset,[switch]$RequireExistingSteamVR,
      [ValidateRange(1,65535)][int]$OscPort=9020,
      [ValidateRange(0,65535)][int]$OscForward=8888)
$ErrorActionPreference = 'Stop'
$bundle = $PSScriptRoot
$gameWasRunning = [bool](Get-Process VRChat -ErrorAction SilentlyContinue)
$gameClosed = $false
$providerStarted = $false
$gameLaunched = $false
$oscSettings = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\osc-settings.txt'
if ($InputRoute -eq 'Baballonia' -and (Test-Path -LiteralPath $oscSettings -PathType Leaf)) {
    $values = @(Get-Content -LiteralPath $oscSettings)
    if ($values.Count -eq 2 -and $values[0] -match '^[0-9]{1,5}$' -and $values[1] -match '^[0-9]{1,5}$') {
        if (-not $PSBoundParameters.ContainsKey('OscPort') -and [int]$values[0] -ge 1 -and [int]$values[0] -le 65535) { $OscPort = [int]$values[0] }
        if (-not $PSBoundParameters.ContainsKey('OscForward') -and [int]$values[1] -le 65535) { $OscForward = [int]$values[1] }
    }
}

function Wait-ForProcess([string]$name, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        if (Get-Process $name -ErrorAction SilentlyContinue) { return $true }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Start-SteamApp([int]$appId) {
    Start-Process -FilePath ('steam://rungameid/' + $appId) | Out-Null
}

function Close-VRChatNormally {
    Write-Output 'Closing VRChat normally...'
    foreach ($game in @(Get-Process VRChat -ErrorAction SilentlyContinue)) {
        if (-not $game.CloseMainWindow()) {
            throw 'VRChat did not accept a normal close. Close it in Steam, then retry.'
        }
    }
    $deadline = (Get-Date).AddSeconds(45)
    while (Get-Process VRChat -ErrorAction SilentlyContinue) {
        if ((Get-Date) -ge $deadline) {
            throw 'VRChat is still running. Close it normally in Steam, then retry.'
        }
        Start-Sleep -Milliseconds 500
    }
    $script:gameClosed = $true
}

function Test-NvidiaConsumer {
    $active = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\active-run.txt'
    if (-not (Test-Path -LiteralPath $active -PathType Leaf)) { return $false }
    $run = (Get-Content -LiteralPath $active -Raw).Trim()
    $allowed = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'VRSS Tuner\runs')).TrimEnd('\') + '\'
    if (-not [IO.Path]::GetFullPath($run).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Invalid active provider run path.'
    }
    $status = Join-Path $run 'status.json'
    if (-not (Test-Path -LiteralPath $status -PathType Leaf)) { return $false }
    try { return ([int]((Get-Content -LiteralPath $status -Raw | ConvertFrom-Json).consumerCount) -gt 0) }
    catch { return $false }
}

try {
    if ($WaitForHeadset -and $InputRoute -eq 'SRanibro') {
        $deadline = (Get-Date).AddSeconds(120)
        while (-not (Get-Process SRanibro -ErrorAction SilentlyContinue)) {
            if ((Get-Date) -ge $deadline) { throw 'SRanibro did not start within 120 seconds.' }
            Start-Sleep -Seconds 1
        }
    }
    if (-not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
        if ($RequireExistingSteamVR) { throw 'SteamVR exited before the selected gaze path started.' }
        Write-Output 'Starting SteamVR...'
        Start-SteamApp 250820
        if (-not (Wait-ForProcess 'vrserver' 60)) {
            throw 'SteamVR did not start within 60 seconds.'
        }
    }

    $setup = $null
    $attempts = if ($WaitForHeadset) { 40 } else { 3 }
    for ($attempt = 0; $attempt -lt $attempts; $attempt++) {
        $setup = & (Join-Path $bundle 'Test-Setup.ps1') -AllowRunningVRChat:$gameWasRunning -AllowOfficialHandoff | ConvertFrom-Json
        if ($setup.readyForProviderLaunch) { break }
        $transient = @($setup.reasons | Where-Object {
            $_ -in @('Start SteamVR with a connected HMD.',
                     'Could not read and validate the connected HMD geometry.')
        })
        if ($attempt -eq ($attempts - 1) -or $transient.Count -ne @($setup.reasons).Count) { break }
        Start-Sleep -Seconds 3
    }
    if (-not $setup.readyForProviderLaunch) {
        $reasons = if ([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ja') {
            @($setup.reasonsJapanese)
        } else { @($setup.reasons) }
        throw ('Setup is not ready: ' + ($reasons -join '; '))
    }

    # The audited HMD test branch reads its ID from the VRChat process environment
    # at startup. A running game cannot receive that ID through a hot attachment.
    $needsKeyedLaunch = [bool]$setup.hmdTestBranch
    Write-Output ('Starting common gaze provider from ' + $InputRoute + '...')
    & (Join-Path $bundle 'Start-Provider.ps1') -InputRoute $InputRoute `
        -AllowRunningVRChat:([bool](Get-Process VRChat -ErrorAction SilentlyContinue)) -OscPort $OscPort -OscForward $OscForward
    if (-not (Get-Process common_vrss_provider -ErrorAction SilentlyContinue)) {
        throw 'The common gaze provider did not stay running.'
    }
    $providerStarted = $true

    if ($ProviderOnly) {
        if ($needsKeyedLaunch) {
            Write-Output 'Gaze provider ready. To launch VRChat later, use Start-VRChat-HMD-Key.cmd or configure Install-Steam-Launch-Bridge.cmd once and launch through Steam.'
        } else {
            Write-Output 'Gaze provider ready. You can launch VRChat later through Steam.'
        }
        return
    }

    if ($needsKeyedLaunch -and (Get-Process VRChat -ErrorAction SilentlyContinue)) {
        Close-VRChatNormally
    }

    if (Get-Process VRChat -ErrorAction SilentlyContinue) {
        $deadline = (Get-Date).AddSeconds($HotAttachWaitSeconds)
        do {
            if (Test-NvidiaConsumer) {
                $gameLaunched = $true
                Write-Output 'NVIDIA found the already-running VRChat. No restart was needed.'
                return
            }
            if ((Get-Date) -ge $deadline) { break }
            Start-Sleep -Milliseconds 500
        } while ($true)
        Write-Output 'No NVIDIA consumer appeared in the running VRChat. Restarting it with the provider ready...'
        Close-VRChatNormally
    }

    if ($needsKeyedLaunch) {
        Write-Output 'Launching protected VRChat with the registered HMD identity...'
        & (Join-Path $bundle 'Start-VRChat-HMD-Key.ps1') | Out-Null
    } else {
        Write-Output 'Launching VRChat through Steam...'
        Start-SteamApp 438100
    }
    if (-not (Wait-ForProcess 'VRChat' 90)) {
        throw 'VRChat did not start within 90 seconds.'
    }
    $gameLaunched = $true
    Write-Output 'Common gaze provider and VRChat are running. Verify live gaze and VRSS in the headset.'
} catch {
    if ($providerStarted -and -not $gameLaunched) {
        try { & (Join-Path $bundle 'Stop-Provider.ps1') | Out-Null } catch { }
    }
    if ($gameClosed -and -not (Get-Process VRChat -ErrorAction SilentlyContinue)) {
        try {
            Write-Output 'Restoring VRChat after the failed start...'
            Start-SteamApp 438100
            if (-not (Wait-ForProcess 'VRChat' 45)) {
                Write-Warning 'VRChat did not restart within 45 seconds. Open it through Steam.'
            }
        } catch { Write-Warning ('Could not restore VRChat: ' + $_.Exception.Message) }
    }
    throw
}
