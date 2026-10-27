$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\gaze-route-' + (Get-Date -Format 'yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Set-Gaze-Route.ps1') -Destination $fixture
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Gaze-Route.ps1') -Destination (Join-Path $fixture 'Actual-Gaze-Route.ps1')
[IO.File]::WriteAllText((Join-Path $fixture 'Gaze-Route.ps1'), @'
. (Join-Path $PSScriptRoot 'Actual-Gaze-Route.ps1')
function Set-PimaxGazeSender([string]$Action) {
    $global:RouteEvents.Add('pimax-' + $Action)
    $global:RouteNative = $Action -eq 'open'
}
function Stop-OwnedGazeProvider {
    $global:RouteEvents.Add('force-owned')
    if (-not $global:RouteAllowForce) { throw 'Fixture: provider belongs to another installation.' }
    $global:RouteCommon=$false
}
'@,[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Stop-Provider.ps1'), @'
$global:RouteEvents.Add('stop-common')
$global:RouteCommon = $false
'@,[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Start-Common-Session.ps1'), @'
param([string]$InputRoute,[switch]$ProviderOnly,[int]$OscPort,[int]$OscForward)
if (-not $ProviderOnly) { throw 'Route switching must not restart VRChat.' }
if ($global:RouteFailStart) { throw 'Fixture start failure' }
$global:RoutePorts = "$OscPort/$OscForward"
$global:RouteEvents.Add('start-' + $InputRoute)
$global:RouteCommon = $true
$InputRoute | Set-Content -LiteralPath (Join-Path $global:RouteRun 'input-route.txt') -Encoding ASCII
'3' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'route-runtime-version.txt') -Encoding ASCII
'@,[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Start-Provider.ps1'), @'
param([switch]$InputOnly,[string]$RunDirectory,[string]$InputRoute,[int]$OscPort,[int]$OscForward)
if (-not $InputOnly -or $RunDirectory -ne $global:RouteRun) { throw 'Input-only fixture received invalid arguments.' }
if ($global:RouteFailStart) { throw 'Fixture input failure' }
$global:RoutePorts = "$OscPort/$OscForward"
$global:RouteEvents.Add('input-' + $InputRoute)
$InputRoute | Set-Content -LiteralPath (Join-Path $global:RouteRun 'input-route.txt') -Encoding ASCII
[IO.File]::WriteAllText((Join-Path $global:RouteRun 'status.json'),('{"pid":777,"registered":true,"inputRoute":"'+$InputRoute+'","inputPort":'+$OscPort+',"oscForward":'+$OscForward+'}'))
'@,[Text.Encoding]::ASCII)
function Get-Process {
    [CmdletBinding()]param([string]$Name,[int]$Id)
    if ($Id -eq 111 -and $global:RouteBridge) {
        $process=[pscustomobject]@{Id=111;Path=(Join-Path $fixture 'steamvr-gaze-bridge\steamvr_gaze_bridge.exe');StartTime=(Get-Date).AddMinutes(-1)}
        $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param([int]$Milliseconds);return -not $global:RouteBridge }
        return $process
    }
    if ($Name -eq 'common_vrss_provider' -and $global:RouteCommon) { return [pscustomobject]@{Id=777} }
    if ($Name -eq 'vrss_gaze_provider' -and $global:RouteNative) { return [pscustomobject]@{Id=888} }
    if ($Name -eq 'SRanibro' -and $global:RouteSranibro) { return [pscustomobject]@{Id=999} }
}
function Stop-Process {
    [CmdletBinding()]param([int]$Id,[switch]$Force)
    if($Id -ne 111){throw 'Fixture refused an unknown process.'}
    if($global:RouteBridgeDeny){throw 'Fixture bridge stop denied'}
    $global:RouteBridge=$false
}
function Start-Sleep { param([int]$Milliseconds) }
$previous = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = Join-Path $fixture 'local'
    $data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
    $global:RouteRun = Join-Path $data 'runs\current'
    $stale = Join-Path $data 'runs\stale'
    New-Item -ItemType Directory -Path $global:RouteRun,$stale -Force | Out-Null
    $stale | Set-Content -LiteralPath (Join-Path $data 'active-run.txt')
    '{"pid":777,"registered":true}' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'status.json')
    'Baballonia' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'input-route.txt')
    $global:RouteEvents = [Collections.Generic.List[string]]::new()
    $global:RouteCommon = $true; $global:RouteNative = $false; $global:RouteSranibro = $true
    $global:RouteAllowForce=$false
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute SRanibro
    if (($global:RouteEvents -join ',') -ne 'stop-common,start-SRanibro') { throw 'Stale run recovery / source switch failed.' }
    $global:RouteEvents.Clear()
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute SRanibro
    if ($global:RouteEvents.Count) { throw 'An unchanged route was needlessly restarted.' }
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Baballonia
    if (($global:RouteEvents -join ',') -ne 'input-Baballonia' -or -not $global:RouteCommon) { throw 'Hot input switch restarted the provider.' }
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute SRanibro
    $global:RouteEvents.Clear()
    $global:RouteSranibro = $false
    $failed = $false
    try { & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute SRanibro } catch { $failed=$true }
    if (-not $failed -or -not $global:RouteCommon) { throw 'Unavailable input destroyed the active route.' }
    $global:RouteNative=$true; $global:RouteEvents.Clear()
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop -InputRoute Baballonia
    if ($global:RouteCommon -or $global:RouteNative -or
        ($global:RouteEvents -join ',') -ne 'stop-common,pimax-close' -or
        -not (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt'))) { throw 'Unconditional Stop failed.' }
    $global:RouteEvents.Clear()
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Pimax
    if (-not $global:RouteNative -or (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt'))) { throw 'Native route did not resume explicitly.' }
    $global:RouteCommon=$true; $global:RouteNative=$true; $global:RouteEvents.Clear()
    '{"pid":123,"registered":true}' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'status.json')
    $failed=$false
    try { & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop } catch { $failed=$true }
    if (-not $failed -or $global:RouteNative -or -not $global:RouteCommon) { throw 'Unknown process was falsely reported stopped / blocked native Stop.' }
    $global:RouteAllowForce=$true
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop
    if ($global:RouteCommon) { throw 'Unresponsive owned provider was left running.' }
    # A missing/out-of-tree active pointer must never prevent recovery or Stop.
    'C:\missing-provider-run' | Set-Content -LiteralPath (Join-Path $data 'active-run.txt')
    $global:RouteNative=$false; $global:RouteSranibro=$true
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop
    $global:RouteFailStart=$true
    [IO.File]::WriteAllText((Join-Path $data 'osc-settings.txt'),"39030`n0`n")
    try { & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Baballonia -OscPort 39031 } catch { }
    if (-not (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt')) -or
        (Get-Content -LiteralPath (Join-Path $data 'osc-settings.txt') -Raw) -ne "39030`n0`n") {
        throw 'Failed start lost pause state or saved failed port settings.'
    }
    $global:RouteFailStart=$false
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Baballonia
    if ($global:RoutePorts -ne '39030/0') { throw 'Manual launcher ignored saved OSC ports.' }
    # Recover an invalid pointer to an already-live run without restarting it.
    '{"pid":777,"registered":true,"inputRoute":"SRanibro","inputPort":5555,"oscForward":8888}' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'status.json')
    'C:\missing-provider-run' | Set-Content -LiteralPath (Join-Path $data 'active-run.txt')
    $global:RouteEvents.Clear()
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Baballonia
    if (($global:RouteEvents -join ',') -ne 'input-Baballonia') { throw 'Live run pointer recovery restarted the provider.' }
    $global:RouteEvents.Clear()
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute Baballonia
    if ($global:RouteEvents.Count) { throw 'Unchanged hot OSC route was needlessly restarted.' }
    'SteamVREye' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'input-route.txt')
    '{"pid":777,"registered":true,"inputRoute":"SteamVREye","inputPort":5556,"oscForward":8888}' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'status.json')
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -InputRoute SteamVREye
    if (($global:RouteEvents -join ',') -ne 'input-SteamVREye') { throw 'Start did not repair a crashed source bridge.' }
    $global:RouteCommon=$false;$global:RouteNative=$false;$global:RouteBridge=$true
    $global:RouteBridgeDeny=$true
    '111' | Set-Content -LiteralPath (Join-Path $global:RouteRun 'steamvr-bridge.pid')
    'C:\missing-provider-run' | Set-Content -LiteralPath (Join-Path $data 'active-run.txt')
    $failed=$false
    try { & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop } catch { $failed=$true }
    if(-not $failed -or -not $global:RouteBridge){throw 'Bridge stop failure was falsely reported successful.'}
    $global:RouteBridgeDeny=$false
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop
    if($global:RouteBridge){throw 'Orphan source bridge survived Stop after provider exit.'}
    Write-Output 'GAZE_ROUTE_SWITCH_PASS stale_run_recovery source_switch idempotent unavailable_preserves_active stop_all native_resume foreign_process_refused owned_fallback invalid_pointer saved_ports failure_preserves_pause_and_settings hot_osc_idempotent no_VRChat_restart no_UAC'
} finally {
    $env:LOCALAPPDATA=$previous
    foreach ($name in @('RouteEvents','RouteCommon','RouteNative','RouteSranibro','RouteRun','RouteAllowForce','RouteFailStart','RoutePorts','RouteBridge','RouteBridgeDeny')) {
        Remove-Variable -Name $name -Scope Global -ErrorAction SilentlyContinue
    }
}
