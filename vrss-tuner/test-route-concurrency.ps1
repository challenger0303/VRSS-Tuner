$ErrorActionPreference='Stop'
$fixture=Join-Path $PSScriptRoot ('build\route-concurrency-'+(Get-Date -Format 'yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Set-Gaze-Route.ps1') -Destination $fixture
[IO.File]::WriteAllText((Join-Path $fixture 'Gaze-Route.ps1'),"function Stop-CommonGazeRoute { }`nfunction Set-PimaxGazeSender { param([string]`$Action) }",[Text.Encoding]::ASCII)
$shell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$mutex=[Threading.Mutex]::new($false,'Local\VRSS-Tuner-Route-Operation')
$owned=$false;$previous=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA=Join-Path $fixture 'local'
    $owned=$mutex.WaitOne(0)
    if(-not $owned) { throw 'A real route operation is active; concurrency fixture refused to interfere.' }
    $child=Start-Process -FilePath $shell -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $fixture 'Set-Gaze-Route.ps1')+'" -Stop') -WindowStyle Hidden -PassThru
    $paused=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\gaze-paused.txt'
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    while(-not (Test-Path -LiteralPath $paused) -and [DateTime]::UtcNow -lt $deadline) { [Threading.Thread]::Sleep(25) }
    if(-not (Test-Path -LiteralPath $paused) -or $child.HasExited) { throw 'Stop failed to cancel before acquiring the operation lock.' }
    $mutex.ReleaseMutex();$owned=$false
    if(-not $child.WaitForExit(5000) -or $child.ExitCode -ne 0) { throw 'Queued Stop failed after the lock was released.' }
    $abandon=Join-Path $fixture 'abandon.ps1'
    [IO.File]::WriteAllText($abandon,'$m=[Threading.Mutex]::new($false,"Local\VRSS-Tuner-Route-Operation");$null=$m.WaitOne();[Environment]::Exit(0)',[Text.Encoding]::ASCII)
    $child=Start-Process -FilePath $shell -ArgumentList ('-NoProfile -File "'+$abandon+'"') -WindowStyle Hidden -PassThru
    if(-not $child.WaitForExit(5000) -or $child.ExitCode -ne 0) { throw 'Abandoned-lock fixture failed.' }
    & (Join-Path $fixture 'Set-Gaze-Route.ps1') -Stop
    Write-Output 'ROUTE_CONCURRENCY_PASS immediate_stop_cancellation queued_stop abandoned_mutex_recovery no_real_provider_access'
} finally {
    if($owned){$mutex.ReleaseMutex()}
    $mutex.Dispose();$env:LOCALAPPDATA=$previous
}
