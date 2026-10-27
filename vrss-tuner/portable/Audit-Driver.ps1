param()
$ErrorActionPreference = 'Stop'
$provider = Join-Path $PSScriptRoot 'common-vrss-provider\common_vrss_provider.exe'
if (-not (Test-Path -LiteralPath $provider -PathType Leaf)) { throw 'Provider executable is missing.' }
$folder = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\driver-audits'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$output = Join-Path $folder ('driver-audit-' + (Get-Date -Format 'yyyyMMddTHHmmssfff') + '.txt')
$result = & $provider --driver-audit 1 2>&1 | Out-String
[IO.File]::WriteAllText($output, $result, [Text.UTF8Encoding]::new($false))
Write-Output $result.Trim()
Write-Output "Saved to: $output"
if ($LASTEXITCODE -ne 0 -or -not $result.Contains('DRIVER_AUDIT_PASS')) {
    throw 'Read-only driver audit did not complete.'
}
