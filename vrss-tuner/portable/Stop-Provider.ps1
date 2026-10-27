$ErrorActionPreference = 'Stop'
$data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
$record = Join-Path $data 'active-run.txt'
if (-not (Test-Path -LiteralPath $record)) { throw 'No active run recorded' }
$run = (Get-Content -LiteralPath $record -Raw).Trim()
$allowed = [IO.Path]::GetFullPath((Join-Path $data 'runs')).TrimEnd('\') + '\'
$resolved = [IO.Path]::GetFullPath($run)
if (-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $resolved -PathType Container)) { throw 'Invalid run directory' }
'User requested stop' | Set-Content -LiteralPath (Join-Path $resolved 'stop.request') -Encoding ASCII
'User requested stop' | Set-Content -LiteralPath (Join-Path $resolved 'input-stop.request') -Encoding ASCII
$varjoPidFile = Join-Path $resolved 'varjo-bridge.pid'
if (Test-Path -LiteralPath $varjoPidFile -PathType Leaf) {
    $pidText = (Get-Content -LiteralPath $varjoPidFile -Raw).Trim()
    $bridgePid = 0
    if ([int]::TryParse($pidText, [ref]$bridgePid) -and $bridgePid -gt 0) {
        $candidate = Get-Process -Id $bridgePid -ErrorAction SilentlyContinue
        $expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'varjo-gaze-bridge\varjo_gaze_bridge.exe'))
        if ($candidate -and $candidate.Path -and
            [IO.Path]::GetFullPath($candidate.Path).Equals($expected,[StringComparison]::OrdinalIgnoreCase)) {
            Stop-Process -Id $bridgePid -Force
        }
    }
}
if ([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ja')
    { Write-Output '停止を要求しました。次の更新後にプロバイダーが終了します。' }
else { Write-Output 'Stop requested. The provider will exit after its next update.' }
