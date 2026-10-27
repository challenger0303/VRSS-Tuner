$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\common-session-smoke-' + (Get-Date -Format 'yyyyMMddTHHmmssfff') + '-' + $PID)
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'portable\Start-Common-Session.ps1') -Destination $fixture
[IO.File]::WriteAllText((Join-Path $fixture 'Test-Setup.ps1'), @'
$global:VrssSessionEvents.Add('preflight')
[pscustomobject]@{readyForProviderLaunch=$global:VrssSessionReady;hmdIdentityCandidate=$global:VrssSessionCandidate;
    hmdTestBranch=$global:VrssSessionHmdTestBranch;
    reasons=@('This VRChat profile does not have the VRSS allow gate enabled.');
    reasonsJapanese=@('VRSS is not allowed.')} | ConvertTo-Json -Compress
'@, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Start-Provider.ps1'), @'
param([string]$InputRoute)
if ($InputRoute -ne 'SRanibro') { throw 'Unexpected input route' }
$global:VrssSessionEvents.Add('provider')
if ($global:VrssSessionProviderFails) { throw 'Provider fixture failure' }
$global:VrssSessionProvider = $true
$data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
$run = Join-Path $data 'runs\mock'
New-Item -ItemType Directory -Path $run -Force | Out-Null
$run | Set-Content -LiteralPath (Join-Path $data 'active-run.txt')
(@{consumerCount=$(if ($global:VrssSessionHotConsumer) { 1 } else { 0 })} | ConvertTo-Json) |
    Set-Content -LiteralPath (Join-Path $run 'status.json')
'Provider started'
'@, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Start-VRChat-HMD-Key.ps1'), @'
$global:VrssSessionEvents.Add('keyed-game')
$global:VrssSessionGame = $true
'{}'
'@, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $fixture 'Stop-Provider.ps1'), @'
$global:VrssSessionEvents.Add('stop-provider')
$global:VrssSessionProvider = $false
'@, [Text.Encoding]::ASCII)

$fakeGame = [pscustomobject]@{}
$fakeGame | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value {
    $global:VrssSessionEvents.Add('close-game')
    $global:VrssSessionGame = $false
    return $true
}
function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -eq 'VRChat' -and $global:VrssSessionGame) { return $global:VrssSessionFakeGame }
    if ($Name -eq 'vrserver' -and $global:VrssSessionSteamVr) { return [pscustomobject]@{Id=1} }
    if ($Name -eq 'common_vrss_provider' -and $global:VrssSessionProvider) { return [pscustomobject]@{Id=2} }
}
function Start-Process {
    [CmdletBinding()]
    param([string]$FilePath)
    if ($FilePath -eq 'steam://rungameid/250820') {
        $global:VrssSessionEvents.Add('start-steamvr')
        $global:VrssSessionSteamVr = $true
    } elseif ($FilePath -eq 'steam://rungameid/438100') {
        $global:VrssSessionEvents.Add('steam-game')
        $global:VrssSessionGame = $true
    } else { throw "Unexpected launch: $FilePath" }
}
function Start-Sleep { param([int]$Milliseconds, [int]$Seconds) }

$global:VrssSessionFakeGame = $fakeGame
$previousLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = Join-Path $fixture 'local-app-data'
    foreach ($case in @(
        @{game=$true;steamvr=$false;candidate=$false;keyed=$false;hot=$true;expected='start-steamvr,preflight,provider'},
        @{game=$true;steamvr=$true;candidate=$false;keyed=$false;hot=$false;expected='preflight,provider,close-game,steam-game'},
        @{game=$false;steamvr=$true;candidate=$true;keyed=$true;expected='preflight,provider,keyed-game'},
        @{game=$true;steamvr=$true;candidate=$false;keyed=$true;expected='preflight,provider,close-game,keyed-game'})) {
        $global:VrssSessionEvents = [Collections.Generic.List[string]]::new()
        $global:VrssSessionGame = $case.game
        $global:VrssSessionSteamVr = $case.steamvr
        $global:VrssSessionProvider = $false
        $global:VrssSessionCandidate = $case.candidate
        $global:VrssSessionHmdTestBranch = $case.keyed
        $global:VrssSessionReady = $true
        $global:VrssSessionProviderFails = $false
        $global:VrssSessionHotConsumer = [bool]$case.hot
        $output = & (Join-Path $fixture 'Start-Common-Session.ps1') -InputRoute SRanibro -HotAttachWaitSeconds 0
        $actual = $global:VrssSessionEvents -join ','
        if ($actual -ne $case.expected -or -not $global:VrssSessionGame -or
            -not $global:VrssSessionProvider -or
            -not (@($output) -match '(NVIDIA found the already-running VRChat|Common gaze provider and VRChat are running)')) {
            throw "Common session order failed: $actual (expected $($case.expected))"
        }
    }
    foreach ($failure in @('preflight','provider')) {
        $global:VrssSessionEvents = [Collections.Generic.List[string]]::new()
        $global:VrssSessionGame = $true
        $global:VrssSessionSteamVr = $true
        $global:VrssSessionProvider = $false
        $global:VrssSessionCandidate = $false
        $global:VrssSessionHmdTestBranch = $false
        $global:VrssSessionReady = $failure -ne 'preflight'
        $global:VrssSessionProviderFails = $failure -eq 'provider'
        $global:VrssSessionHotConsumer = $false
        $failed = $false
        try { & (Join-Path $fixture 'Start-Common-Session.ps1') -InputRoute SRanibro -HotAttachWaitSeconds 0 | Out-Null }
        catch { $failed = $true }
        if (-not $failed -or -not $global:VrssSessionGame -or $global:VrssSessionProvider -or
            ($global:VrssSessionEvents -join ',') -ne
            $(if ($failure -eq 'preflight') { 'preflight' }
              else { 'preflight,provider' })) {
            throw "Common session failed to restore VRChat after $failure failure."
        }
    }
    foreach ($gameRunning in @($false,$true)) {
        $global:VrssSessionEvents = [Collections.Generic.List[string]]::new()
        $global:VrssSessionGame = $gameRunning
        $global:VrssSessionSteamVr = $true
        $global:VrssSessionProvider = $false
        $global:VrssSessionCandidate = $false
        $global:VrssSessionHmdTestBranch = $true
        $global:VrssSessionReady = $true
        $global:VrssSessionProviderFails = $false
        $global:VrssSessionHotConsumer = $false
        $output = & (Join-Path $fixture 'Start-Common-Session.ps1') -InputRoute SRanibro -ProviderOnly
        if (($global:VrssSessionEvents -join ',') -ne 'preflight,provider' -or
            $global:VrssSessionGame -ne $gameRunning -or -not $global:VrssSessionProvider -or
            -not (@($output) -match 'Gaze provider ready')) {
            throw "Provider-only mode unexpectedly touched VRChat: $($global:VrssSessionEvents -join ',')"
        }
    }
    Write-Output 'COMMON_SESSION_PASS hot_attach fallback_restart steamvr preflight provider candidate_launch provider_only_preserves_game failure_preserves_game no_UAC'
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    foreach ($name in @('VrssSessionFakeGame','VrssSessionEvents','VrssSessionGame',
                       'VrssSessionSteamVr','VrssSessionProvider','VrssSessionCandidate',
                       'VrssSessionReady','VrssSessionProviderFails','VrssSessionHotConsumer',
                       'VrssSessionHmdTestBranch')) {
        Remove-Variable -Name $name -Scope Global -ErrorAction SilentlyContinue
    }
}
