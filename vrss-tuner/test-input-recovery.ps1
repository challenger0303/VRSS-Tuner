$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\input-recovery-' + (Get-Date -Format 'yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Start-Provider.ps1') -Destination $fixture
[IO.File]::WriteAllText((Join-Path $fixture 'Gaze-Route.ps1'), @'
function Get-ActiveGazeRun { return $global:RecoveryRun }
function Stop-GazeInputBridges { param([string]$Run);$global:RecoveryStops++ }
'@,[Text.Encoding]::ASCII)
New-Item -ItemType Directory -Path (Join-Path $fixture 'steamvr-gaze-bridge') -Force | Out-Null
foreach ($name in @('steamvr_gaze_bridge.exe','eye_actions.json')) {
    [IO.File]::WriteAllText((Join-Path $fixture ('steamvr-gaze-bridge\'+$name)),'fixture')
}
function Get-Process {
    [CmdletBinding()]param([string]$Name)
    if ($Name -eq 'common_vrss_provider') { return [pscustomobject]@{Id=777} }
    if ($Name -eq 'SRanibro') { return [pscustomobject]@{Id=888} }
}
function Start-Sleep {
    param([int]$Milliseconds)
    if($global:RecoveryCancel) { 'Stop' | Set-Content -LiteralPath (Join-Path (Split-Path -Parent (Split-Path -Parent $global:RecoveryRun)) 'gaze-paused.txt') }
    $fields=@{}
    foreach($line in Get-Content -LiteralPath (Join-Path $global:RecoveryRun 'input-source.txt')) {
        $pair=$line -split '=',2;if($pair.Count -eq 2){$fields[$pair[0]]=$pair[1]}
    }
    if($global:RecoveryRejectOsc -and $fields['route'] -eq 'Baballonia') {
        'Fixture port occupied' | Set-Content -LiteralPath (Join-Path $global:RecoveryRun 'input-error.txt')
    } else {
        Remove-Item -LiteralPath (Join-Path $global:RecoveryRun 'input-error.txt') -ErrorAction SilentlyContinue
        [IO.File]::WriteAllText((Join-Path $global:RecoveryRun 'status.json'),
            ('{"pid":777,"registered":true,"inputRoute":"'+$fields['route']+'","inputPort":'+$fields['port']+',"oscForward":'+$fields['forward']+'}'))
    }
}
function Start-Process {
    [CmdletBinding()]param([string]$FilePath,[string]$ArgumentList,[string]$WindowStyle,[switch]$PassThru,[string]$RedirectStandardError)
    if($global:RecoveryFailBridge) { throw 'Fixture bridge launch failure' }
    $global:RecoveryBridgeStarts++
    return [Diagnostics.Process]::GetCurrentProcess()
}
$previous=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA=Join-Path $fixture 'local'
    $global:RecoveryRun=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\runs\current'
    New-Item -ItemType Directory -Path $global:RecoveryRun -Force | Out-Null
    $global:RecoveryStops=0;$global:RecoveryBridgeStarts=0
    $global:RecoveryFailBridge=$true;$global:RecoveryRejectOsc=$false
    '{"pid":777,"registered":true,"inputRoute":"SRanibro","inputPort":5555,"oscForward":8888}' | Set-Content -LiteralPath (Join-Path $global:RecoveryRun 'status.json')
    $failed=$false
    try { & (Join-Path $fixture 'Start-Provider.ps1') -InputOnly -RunDirectory $global:RecoveryRun -InputRoute SteamVREye } catch { $failed=$true }
    $status=Get-Content -LiteralPath (Join-Path $global:RecoveryRun 'status.json') -Raw | ConvertFrom-Json
    if(-not $failed -or $status.inputRoute -ne 'SRanibro') { throw 'Bridge startup failure did not restore the previous input.' }
    $global:RecoveryFailBridge=$false;$global:RecoveryRejectOsc=$true
    '{"pid":777,"registered":true,"inputRoute":"SteamVREye","inputPort":5556,"oscForward":8888}' | Set-Content -LiteralPath (Join-Path $global:RecoveryRun 'status.json')
    $failed=$false
    try { & (Join-Path $fixture 'Start-Provider.ps1') -InputOnly -RunDirectory $global:RecoveryRun -InputRoute Baballonia -OscPort 39035 } catch { $failed=$true }
    $status=Get-Content -LiteralPath (Join-Path $global:RecoveryRun 'status.json') -Raw | ConvertFrom-Json
    if(-not $failed -or $status.inputRoute -ne 'SteamVREye' -or $global:RecoveryBridgeStarts -ne 1) {
        throw 'Rejected OSC switch did not restart the previous source bridge.'
    }
    $global:RecoveryCancel=$true;$global:RecoveryRejectOsc=$false
    $failed=$false
    try { & (Join-Path $fixture 'Start-Provider.ps1') -InputOnly -RunDirectory $global:RecoveryRun -InputRoute Baballonia -OscPort 39035 } catch { $failed=$true }
    if(-not $failed -or $global:RecoveryBridgeStarts -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $global:RecoveryRun 'stop.request'))) {
        throw 'Stop during an input switch restarted the old bridge or failed to cancel.'
    }
    Write-Output 'INPUT_RECOVERY_PASS bridge_launch_failure_restores_input rejected_osc_restarts_old_bridge stop_cancels_switch provider_kept_alive no_UAC'
} finally {
    $env:LOCALAPPDATA=$previous
    foreach($name in @('RecoveryRun','RecoveryStops','RecoveryBridgeStarts','RecoveryFailBridge','RecoveryRejectOsc','RecoveryCancel')) {
        Remove-Variable -Name $name -Scope Global -ErrorAction SilentlyContinue
    }
}
