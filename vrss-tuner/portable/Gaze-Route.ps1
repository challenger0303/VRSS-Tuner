# Shared route controls. Only the Pimax VRSS sender is controlled, never its
# camera runtime, headset service, SteamVR, VRChat, or face-tracking applications.
function Get-ActiveGazeRun {
    $data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
    $record = Join-Path $data 'active-run.txt'
    $allowed = [IO.Path]::GetFullPath((Join-Path $data 'runs')).TrimEnd('\') + '\'
    $run = $null
    if (Test-Path -LiteralPath $record -PathType Leaf) {
        try {
            $candidate = [IO.Path]::GetFullPath((Get-Content -LiteralPath $record -Raw).Trim())
            if ($candidate.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -and
                (Test-Path -LiteralPath $candidate -PathType Container)) { $run = $candidate }
        } catch { }
    }
    $live = @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    if (-not $live.Count) { return $run }
    $candidates = @($run)
    if (Test-Path -LiteralPath $allowed -PathType Container) {
        $candidates += @(Get-ChildItem -LiteralPath $allowed -Directory | Sort-Object LastWriteTimeUtc -Descending | ForEach-Object { $_.FullName })
    }
    foreach ($candidate in @($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        try {
            $file = Get-Item -LiteralPath (Join-Path $candidate 'status.json') -ErrorAction Stop
            $age = ((Get-Date).ToUniversalTime() - $file.LastWriteTimeUtc).TotalSeconds
            $state = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            if ($age -ge 0 -and $age -lt 5 -and [int]$state.pid -in $live) {
                if ($candidate -ne $run) { [IO.File]::WriteAllText($record,$candidate,[Text.UTF8Encoding]::new($true)) }
                return $candidate
            }
        } catch { }
    }
    return $run
}
function Get-ActiveGazeRoute {
    $run = Get-ActiveGazeRun
    if (-not $run) { return '' }
    $path = Join-Path $run 'input-route.txt'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return '' }
    return (Get-Content -LiteralPath $path -Raw).Trim()
}
function Test-GazeSourceBridge([string]$Run,[string]$Route) {
    $bridge = switch ($Route) {
        'PSVR2Toolkit' { @('psvr2-bridge.pid','psvr2-gaze-bridge\psvr2_gaze_bridge.exe') }
        'ViveProEye' { @('vive-bridge.pid','vive-streaming-gaze-bridge\ViveStreamingGazeBridge.exe') }
        'ViveStreaming' { @('vive-bridge.pid','vive-streaming-gaze-bridge\ViveStreamingGazeBridge.exe') }
        'SteamVREye' { @('steamvr-bridge.pid','steamvr-gaze-bridge\steamvr_gaze_bridge.exe') }
        'Varjo' { @('varjo-bridge.pid','varjo-gaze-bridge\varjo_gaze_bridge.exe') }
        'PICO' { @('pico-bridge.pid','pico-gaze-bridge\PicoGazeBridge.exe') }
    }
    if (-not $bridge) { return $true }
    try {
        $record = Join-Path $Run $bridge[0]
        $bridgeId = [int](Get-Content -LiteralPath $record -Raw)
        $process = Get-Process -Id $bridgeId -ErrorAction Stop
        $expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $bridge[1]))
        return $process.Path -and [IO.Path]::GetFullPath($process.Path).Equals($expected,[StringComparison]::OrdinalIgnoreCase) -and
            $process.StartTime.ToUniversalTime() -le (Get-Item -LiteralPath $record).LastWriteTimeUtc.AddSeconds(1)
    } catch { return $false }
}
function Set-PimaxGazeSender([ValidateSet('open','close')][string]$Action,[switch]$NoElevation) {
    if ($Action -eq 'close' -and -not (Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue)) { return }
    $launcher = Join-Path $env:ProgramFiles 'Pimax\Runtime\launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
        throw 'The installed Pimax runtime launcher could not be found.'
    }
    & $launcher PiServiceLauncher sendCommand ('vrss_gaze_provider_ctrl ' + $Action) | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($NoElevation -or $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw "Pimax VRSS sender control failed: $Action"
        }
        $shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $PSScriptRoot 'Set-Gaze-Route.ps1') + '" -PimaxSenderAction ' + $Action
        $control = Start-Process -FilePath $shell -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if (-not $control -or $control.ExitCode -ne 0) { throw 'Pimax sender control requires administrator approval or failed.' }
    }
    $deadline = (Get-Date).AddSeconds(8)
    do {
        $running = [bool](Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue)
        if ($running -eq ($Action -eq 'open')) { return }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Pimax VRSS sender did not acknowledge: $Action"
}
function Stop-OwnedGazeProvider([switch]$NoElevation) {
    $expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'common-vrss-provider\common_vrss_provider.exe'))
    $needsElevation = $false
    foreach ($process in @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $process.Path } catch { }
        if (-not $path) { $needsElevation = $true; continue }
        if (-not [IO.Path]::GetFullPath($path).Equals($expected,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'A provider from another installation is running; its executable was not terminated.'
        }
        try { Stop-Process -Id $process.Id -Force -ErrorAction Stop }
        catch { $needsElevation = $true }
    }
    if ($needsElevation) {
        if ($NoElevation) { throw 'Could not stop the provider after elevation.' }
        $shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $PSScriptRoot 'Set-Gaze-Route.ps1') + '" -ForceStopOwnedProvider'
        $control = Start-Process -FilePath $shell -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if (-not $control -or $control.ExitCode -ne 0) { throw 'Stopping the unresponsive provider requires administrator approval or failed.' }
    }
}
function Stop-GazeInputBridges([string]$Run) {
    'Switching input' | Set-Content -LiteralPath (Join-Path $Run 'input-stop.request') -Encoding ASCII
    foreach ($item in @(
        @('psvr2-bridge.pid','psvr2-gaze-bridge\psvr2_gaze_bridge.exe'),
        @('vive-bridge.pid','vive-streaming-gaze-bridge\ViveStreamingGazeBridge.exe'),
        @('steamvr-bridge.pid','steamvr-gaze-bridge\steamvr_gaze_bridge.exe'),
        @('varjo-bridge.pid','varjo-gaze-bridge\varjo_gaze_bridge.exe'),
        @('pico-bridge.pid','pico-gaze-bridge\PicoGazeBridge.exe'))) {
        $record = Join-Path $Run $item[0]
        if (-not (Test-Path -LiteralPath $record -PathType Leaf)) { continue }
        $id = 0
        if (-not [int]::TryParse((Get-Content -LiteralPath $record -Raw).Trim(),[ref]$id) -or $id -le 0) { continue }
        $process = Get-Process -Id $id -ErrorAction SilentlyContinue
        if (-not $process) { Remove-Item -LiteralPath $record -ErrorAction SilentlyContinue; continue }
        if ($process.StartTime.ToUniversalTime() -gt (Get-Item -LiteralPath $record).LastWriteTimeUtc.AddSeconds(1)) {
            Remove-Item -LiteralPath $record -ErrorAction SilentlyContinue
            continue # PID was reused after this bridge's launch record.
        }
        $expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $item[1]))
        if (-not $process.Path -or -not [IO.Path]::GetFullPath($process.Path).Equals($expected,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Cannot verify the previous source bridge executable.'
        }
        if (-not $process.WaitForExit(3000)) {
            Stop-Process -Id $id -Force -ErrorAction Stop
            if (-not $process.WaitForExit(3000)) { throw 'The previous source bridge is still running.' }
        }
        Remove-Item -LiteralPath $record -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath (Join-Path $Run 'input-stop.request') -ErrorAction SilentlyContinue
}
function Stop-CommonGazeRoute {
    $processes = @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue)
    $liveIds = @($processes | ForEach-Object { $_.Id })
    $runs = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\runs'
    $candidates = @()
    try { $active = Get-ActiveGazeRun; if ($active) { $candidates += $active } } catch { }
    if (Test-Path -LiteralPath $runs -PathType Container) {
        $candidates += @(Get-ChildItem -LiteralPath $runs -Directory | ForEach-Object { $_.FullName })
    }
    $matched = @()
    $failures = @()
    foreach ($run in @($candidates | Select-Object -Unique)) {
        $status = Join-Path $run 'status.json'
        if (Test-Path -LiteralPath $status -PathType Leaf) { try {
            $state = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
            $file = Get-Item -LiteralPath $status
            $age = ((Get-Date).ToUniversalTime() - $file.LastWriteTimeUtc).TotalSeconds
            if ([int]$state.pid -in $liveIds -and $age -ge 0 -and $age -lt 5) {
                'User requested stop' | Set-Content -LiteralPath (Join-Path $run 'stop.request') -Encoding ASCII
                $matched += [int]$state.pid
                # Preserve the existing bridge cleanup, but only with the
                # validated run selected. A stale pointer is never sufficient.
                $run | Set-Content -LiteralPath (Join-Path (Split-Path -Parent $runs) 'active-run.txt') -Encoding UTF8
                & (Join-Path $PSScriptRoot 'Stop-Provider.ps1') | Out-Null
            }
        } catch { $failures += $_.Exception.Message } }
        # A crashed provider or stale pointer must not leave a source bridge
        # running. PID and executable checks are performed before termination.
        if (@(Get-ChildItem -LiteralPath $run -Filter '*-bridge.pid' -File -ErrorAction SilentlyContinue).Count) {
            try { Stop-GazeInputBridges $run } catch { $failures += $_.Exception.Message }
        }
    }
    $unmatched = @($liveIds | Where-Object { $_ -notin $matched }).Count -gt 0
    if ($unmatched) { Stop-OwnedGazeProvider }
    $deadline = (Get-Date).AddSeconds(8)
    while (Get-Process common_vrss_provider -ErrorAction SilentlyContinue) {
        if ((Get-Date) -ge $deadline) {
            Stop-OwnedGazeProvider
            $deadline = (Get-Date).AddSeconds(2)
            while (Get-Process common_vrss_provider -ErrorAction SilentlyContinue) {
                if ((Get-Date) -ge $deadline) { throw 'The gaze provider is still running.' }
                Start-Sleep -Milliseconds 100
            }
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($failures.Count) { throw ($failures -join "`n") }
}
