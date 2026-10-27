$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\pico-source-' + (Get-Date -Format 'yyyyMMddTHHmmssfff'))
$run = Join-Path $fixture 'run'
New-Item -ItemType Directory -Path $run -Force | Out-Null
. (Join-Path $PSScriptRoot 'portable/Pico-Source.ps1')
function Assert($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Start-Process {
    [CmdletBinding()]param([string]$FilePath,[string]$ArgumentList,[string]$WindowStyle,[switch]$PassThru,
        [string]$RedirectStandardOutput,[string]$RedirectStandardError)
    Assert ($FilePath.EndsWith('pico-gaze-bridge\PicoGazeBridge.exe')) 'Wrong PICO executable'
    Assert ($ArgumentList.Contains('--headset-ip 192.0.2.10 ') -and $ArgumentList.Contains('--headset-port 9030 ')) 'Wrong headset endpoint'
    Assert ($ArgumentList.Contains('--port 5556 ') -and $ArgumentList.Contains('input-stop.request')) 'PICO must use the shared input stop lifecycle'
    Assert ($PassThru -and $RedirectStandardOutput -and $RedirectStandardError) 'Missing process tracking or logs'
    $global:PicoSourceMockStarted = $true
    return [pscustomobject]@{Id=42;Handle=1}
}
try {
    Save-PicoHeadsetIp $run '192.0.2.10' | Out-Null
    Assert ((Get-PicoHeadsetIp $run '') -eq '192.0.2.10') 'Saved IP did not reload'
    foreach ($invalid in @('192.0.2','192.0.2.256','192.0.2.01','example.com','::1','192.0.2.1;echo x')) {
        Assert (-not (Test-PicoHeadsetIp $invalid)) ('Invalid IP accepted: ' + $invalid)
    }
    $global:PicoSourceMockStarted = $false
    $child = Start-PicoGazeClient $fixture $run (Join-Path $fixture 'calibration.txt') ''
    Assert ($global:PicoSourceMockStarted -and $child.Id -eq 42) 'Client handoff failed'
    Assert (([IO.File]::ReadAllText((Join-Path $run 'pico-bridge.pid'))).Trim() -eq '42') 'Client ownership was not recorded'
    Write-Output 'PICO_SOURCE_PASS saved_ip endpoint projection_arguments input_stop_file owned_pid mocked_process_only'
} finally {
    Remove-Item Function:\Start-Process -ErrorAction SilentlyContinue
    Remove-Variable PicoSourceMockStarted -Scope Global -ErrorAction SilentlyContinue
}
