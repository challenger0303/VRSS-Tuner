$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\provider-launch-smoke-' + (Get-Date -Format 'yyyyMMddTHHmmssfff') + '-' + $PID)
$data = Join-Path $fixture 'local-app-data\VRSS Tuner'
$preflight = Join-Path $data 'preflight'
New-Item -ItemType Directory -Path $preflight -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Start-Provider.ps1') -Destination (Join-Path $fixture 'Start-Provider.ps1')
[IO.File]::WriteAllText((Join-Path $fixture 'Test-Setup.ps1'),
    '[pscustomobject]@{readyForProviderLaunch=$true} | ConvertTo-Json -Compress', [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $preflight 'geometry.txt'), 'fixture', [Text.Encoding]::ASCII)

# Exercise the real launcher through the elevated-worker assignment without
# opening UAC or starting a provider. The mock returns an actual Process object.
function Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string]$ArgumentList, [string]$Verb,
          [string]$WindowStyle, [switch]$PassThru)
    if ($Verb -ne 'RunAs' -or -not $PassThru) { throw 'Unexpected worker launch arguments' }
    $global:VrssSmokeLaunched = $true
    $arguments = [regex]::Match($ArgumentList,'-RunDirectory "([^"]+)"')
    if (-not $arguments.Success) { throw 'Missing provider run directory.' }
    [IO.File]::WriteAllText((Join-Path $arguments.Groups[1].Value 'status.json'),
        ('{"registered":true,"pid":' + $PID + '}'),[Text.Encoding]::ASCII)
    return [Diagnostics.Process]::GetCurrentProcess()
}
function Get-Process {
    [CmdletBinding()]
    param([string[]]$Name)
    if ($global:VrssSmokeLaunched -and $Name.Count -eq 1 -and $Name[0] -eq 'common_vrss_provider') {
        return [Diagnostics.Process]::GetCurrentProcess()
    }
}

$previousLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = Join-Path $fixture 'local-app-data'
    $global:VrssSmokeLaunched = $false
    $output = & (Join-Path $fixture 'Start-Provider.ps1') -InputRoute Baballonia
    if (-not $global:VrssSmokeLaunched -or
        -not (Test-Path -LiteralPath (Join-Path $data 'active-run.txt')) -or
        -not (@($output) -match 'Provider started')) {
        throw 'The launcher did not complete the worker handoff.'
    }
    Write-Output 'PROVIDER_LAUNCH_PASS WindowsPowerShell process_handoff no_UAC'
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Variable -Name VrssSmokeLaunched -Scope Global -ErrorAction SilentlyContinue
}
