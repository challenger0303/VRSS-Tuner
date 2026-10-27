param([ValidateSet('Baballonia','PSVR2Toolkit','SRanibro','ViveProEye','ViveStreaming','SteamVREye','Varjo','PICO')][string]$InputRoute='Baballonia',
      [switch]$Worker,[string]$RunDirectory,[switch]$AllowRunningVRChat,[switch]$InputOnly,[switch]$Recovering,
      [string]$HeadsetIp,
      [ValidateRange(1,65535)][int]$OscPort=9020,
      [ValidateRange(0,65535)][int]$OscForward=8888)
$ErrorActionPreference = 'Stop'
$bundle = $PSScriptRoot
$data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner'
$calibration = Join-Path $data 'common-calibration.txt'
$runs = Join-Path $data 'runs'
function Get-PortableSha256([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function Find-ViveSdk([string]$name) {
    if ($env:VRSS_VIVE_SDK_DLL) {
        $candidate = [IO.Path]::GetFullPath($env:VRSS_VIVE_SDK_DLL)
        if ((Split-Path -Leaf $candidate) -ne $name -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "VRSS_VIVE_SDK_DLL must point to an installed $name"
        }
        return $candidate
    }
    $roots = @((Join-Path $env:ProgramFiles 'VIVE\SRanipal'))
    if (${env:ProgramFiles(x86)}) { $roots += (Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\VIVEDriver\App\SRanipal') }
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $candidate = Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    throw "$name not found. Set VRSS_VIVE_SDK_DLL to the installed vendor DLL path. No vendor DLL is bundled."
}
if ($Worker) {
    $resolved = [IO.Path]::GetFullPath($RunDirectory)
    $allowed = [IO.Path]::GetFullPath($runs).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $resolved -PathType Container)) { throw 'Invalid run directory' }
    $provider = Join-Path $bundle 'common-vrss-provider\common_vrss_provider.exe'
    $geometry = Join-Path $resolved 'geometry.txt'
    $manifest = Get-Content -LiteralPath (Join-Path $bundle 'bundle-manifest.json') -Raw | ConvertFrom-Json
    $expected = @($manifest.files | Where-Object { $_.path -eq 'common-vrss-provider/common_vrss_provider.exe' })
    if ($expected.Count -ne 1 -or (Get-PortableSha256 $provider) -ne $expected[0].sha256)
        { throw 'Provider file changed after preflight' }
    $source = if ($InputRoute -eq 'SRanibro') { 'tcp' } elseif ($InputRoute -eq 'Baballonia') { 'osc' } else { 'udp' }
    $port = if ($InputRoute -eq 'SRanibro') { 5555 } elseif ($InputRoute -eq 'Baballonia') { $OscPort } else { 5556 }
    . (Join-Path $bundle 'Gaze-Route.ps1')
    $child = $null
    $previousContract = $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT
    try {
        $contractFile = Join-Path $resolved 'pico-driver-contract.txt'
        if ($InputRoute -eq 'PICO' -and (Test-Path -LiteralPath $contractFile -PathType Leaf)) {
            $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT = (Get-Content -LiteralPath $contractFile -Raw).Trim()
        }
        if (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt') -PathType Leaf) { throw 'Gaze startup was cancelled by Stop.' }
        Set-PimaxGazeSender close
        $providerArgs = '--geometry "' + $geometry + '" --config "' + $calibration +
            '" --output "' + $resolved + '" --source ' + $source + ' --port ' + $port +
            ' --osc-forward ' + $OscForward + ' --input-route ' + $InputRoute + ' --allow-reconnect 1 --seconds 86400'
        $child = Start-Process -FilePath $provider -ArgumentList $providerArgs -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $resolved 'provider.stdout.txt') `
            -RedirectStandardError (Join-Path $resolved 'provider.stderr.txt')
        $null = $child.Handle
        while (-not $child.WaitForExit(500)) {
            if (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt') -PathType Leaf) {
                'User requested stop' | Set-Content -LiteralPath (Join-Path $resolved 'stop.request') -Encoding ASCII
            }
            # Headset reconnection may restart the sender. Retain the selected
            # common route without stopping XR5 or face-tracking applications.
            if (Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue) { Set-PimaxGazeSender close }
        }
        $child.ExitCode | Set-Content -LiteralPath (Join-Path $resolved 'exit-code.txt') -Encoding ASCII
    } catch {
        $_.Exception.Message | Set-Content -LiteralPath (Join-Path $resolved 'worker-error.txt') -Encoding UTF8
        if ($child -and -not $child.HasExited) {
            'Worker error' | Set-Content -LiteralPath (Join-Path $resolved 'stop.request') -Encoding ASCII
            [void]$child.WaitForExit(5000)
            if (-not $child.HasExited) { Stop-Process -Id $child.Id -Force -ErrorAction Stop }
        }
    } finally { $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT = $previousContract }
    return
}
if ($InputRoute -eq 'PICO') {
    . (Join-Path $bundle 'Pico-Source.ps1')
    $HeadsetIp = Get-PicoHeadsetIp $data $HeadsetIp
    if (-not (Test-Path -LiteralPath (Join-Path $bundle 'pico-gaze-bridge\PicoGazeBridge.exe') -PathType Leaf)) {
        throw 'PICO direct gaze client is missing from this package.'
    }
}
if (-not $InputOnly) {
$status = & (Join-Path $bundle 'Test-Setup.ps1') -AllowRunningVRChat:$AllowRunningVRChat -AllowOfficialHandoff | ConvertFrom-Json
if (-not $status.readyForProviderLaunch) {
    $messages = if ([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ja')
        { $status.reasonsJapanese } else { $status.reasons }
    $messages | ForEach-Object { Write-Output "BLOCKED: $_" }
    exit 2
}
if (Get-Process common_vrss_provider -ErrorAction SilentlyContinue) {
    throw 'A VRSS gaze provider is already running. Stop the active route before starting another.'
}
}
if ($InputRoute -eq 'PSVR2Toolkit') {
    $bridge = Join-Path $bundle 'psvr2-gaze-bridge\psvr2_gaze_bridge.exe'
    $check = & $bridge --check-exports-only 1 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not $check.Contains('PSVR2TK_CAPI_EXPORTS_READY')) {
        throw "PSVR2 Toolkit CAPI is unavailable: $check"
    }
}
if ($InputRoute -eq 'SRanibro' -and -not (Get-Process SRanibro -ErrorAction SilentlyContinue)) {
    throw 'SRanibro is not running. Start its binocular gaze stream first.'
}
$viveSdk = $null
if ($InputRoute -in @('ViveProEye','ViveStreaming')) {
    $dll = if ($InputRoute -eq 'ViveProEye') { 'SRanipal.dll' } else { 'VS_PC_SDK.dll' }
    $viveSdk = Find-ViveSdk $dll
    $viveBridge = Join-Path $bundle 'vive-streaming-gaze-bridge\ViveStreamingGazeBridge.exe'
    if (-not (Test-Path -LiteralPath $viveBridge -PathType Leaf)) { throw 'VIVE gaze bridge is missing from this package.' }
}
if ($InputRoute -eq 'SteamVREye') {
    $steamBridge = Join-Path $bundle 'steamvr-gaze-bridge\steamvr_gaze_bridge.exe'
    $steamManifest = Join-Path $bundle 'steamvr-gaze-bridge\eye_actions.json'
    if (-not (Test-Path -LiteralPath $steamBridge -PathType Leaf) -or
        -not (Test-Path -LiteralPath $steamManifest -PathType Leaf)) {
        throw 'SteamVR eye-action bridge is missing from this package.'
    }
}
if ($InputRoute -eq 'Varjo') {
    $varjoBridge = Join-Path $bundle 'varjo-gaze-bridge\varjo_gaze_bridge.exe'
    if (-not (Test-Path -LiteralPath $varjoBridge -PathType Leaf)) {
        throw 'Varjo gaze bridge is missing from this package.'
    }
    if ($env:VRSS_VARJO_DLL) {
        $varjoDll = [IO.Path]::GetFullPath($env:VRSS_VARJO_DLL)
        if ((Split-Path -Leaf $varjoDll) -ne 'VarjoLib.dll' -or
            -not (Test-Path -LiteralPath $varjoDll -PathType Leaf)) {
            throw 'VRSS_VARJO_DLL must point to an installed VarjoLib.dll.'
        }
    } else {
        $varjoDll = Join-Path $env:ProgramFiles 'Varjo\varjo-compositor\VarjoLib.dll'
        if (-not (Test-Path -LiteralPath $varjoDll -PathType Leaf)) {
            throw 'VarjoLib.dll not found. Set VRSS_VARJO_DLL to the installed DLL path.'
        }
    }
}
$originalInput = $null
$run = $null
$workerProcess = $null
try {
if ($InputOnly) {
    . (Join-Path $bundle 'Gaze-Route.ps1')
    $run = Get-ActiveGazeRun
    if (-not $run -or [IO.Path]::GetFullPath($RunDirectory) -ne $run) { throw 'Invalid input-only provider run.' }
    $current = Get-Content -LiteralPath (Join-Path $run 'status.json') -Raw | ConvertFrom-Json
    if (-not $current.registered -or [int]$current.pid -notin @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })) {
        throw 'The existing provider is not ready for an input change.'
    }
    $rollbackInput = "route=$($current.inputRoute)`nport=$($current.inputPort)`nforward=$($current.oscForward)`n"
    $originalInput = $current
    Stop-GazeInputBridges $run
    Remove-Item -LiteralPath (Join-Path $run 'input-error.txt') -ErrorAction SilentlyContinue
    $port = if ($InputRoute -eq 'SRanibro') { 5555 } elseif ($InputRoute -eq 'Baballonia') { $OscPort } else { 5556 }
    $request = "route=$InputRoute`nport=$port`nforward=$OscForward`n"
    $temp = Join-Path $run 'input-source.tmp'
    [IO.File]::WriteAllText($temp,$request,[Text.Encoding]::ASCII)
    Move-Item -LiteralPath $temp -Destination (Join-Path $run 'input-source.txt') -Force
} else {
New-Item -ItemType Directory -Path $runs -Force | Out-Null
$run = Join-Path $runs ((Get-Date -Format 'yyyyMMddTHHmmssfff') + '-' + $PID)
New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
Copy-Item -LiteralPath (Join-Path $data 'preflight\geometry.txt') -Destination (Join-Path $run 'geometry.txt')
$InputRoute | Set-Content -LiteralPath (Join-Path $run 'input-route.txt') -Encoding ASCII
if ($InputRoute -eq 'PICO' -and $env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT) {
    [IO.File]::WriteAllText((Join-Path $run 'pico-driver-contract.txt'),
        [IO.Path]::GetFullPath($env:VRSS_EXPERIMENTAL_DRIVER_CONTRACT),[Text.Encoding]::UTF8)
}
'3' | Set-Content -LiteralPath (Join-Path $run 'route-runtime-version.txt') -Encoding ASCII
if ($InputRoute -eq 'Baballonia') {
    [IO.File]::WriteAllText((Join-Path $run 'osc-settings.txt'),"$OscPort`n$OscForward`n",[Text.Encoding]::ASCII)
}
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$argsText = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Worker -RunDirectory "' + $run + '" -InputRoute ' + $InputRoute + ' -OscPort ' + $OscPort + ' -OscForward ' + $OscForward
$workerProcess = Start-Process -FilePath $powershell -ArgumentList $argsText -Verb RunAs -WindowStyle Hidden -PassThru
if (-not $workerProcess) { throw 'Could not start the elevated provider worker.' }
$run | Set-Content -LiteralPath (Join-Path $data 'active-run.txt') -Encoding UTF8
}
$deadline = (Get-Date).AddSeconds(20)
do {
    if (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt') -PathType Leaf) { throw 'Gaze startup was cancelled by Stop.' }
    $activeProviders = @(Get-Process common_vrss_provider -ErrorAction SilentlyContinue)
    $ready = $false
    $statusFile = Join-Path $run 'status.json'
    if ($activeProviders.Count -and (Test-Path -LiteralPath $statusFile -PathType Leaf)) {
        try {
            $telemetry = Get-Content -LiteralPath $statusFile -Raw | ConvertFrom-Json
            $ready = $telemetry.registered -and [int]$telemetry.pid -in @($activeProviders | ForEach-Object { $_.Id })
            if ($InputOnly) { $ready = $ready -and $telemetry.inputRoute -eq $InputRoute -and [int]$telemetry.inputPort -eq $port -and [int]$telemetry.oscForward -eq $OscForward }
        } catch { }
    }
    if ($InputOnly -and (Test-Path -LiteralPath (Join-Path $run 'input-error.txt'))) {
        $failure = Get-Content -LiteralPath (Join-Path $run 'input-error.txt') -Raw
        [IO.File]::WriteAllText((Join-Path $run 'input-source.tmp'),$rollbackInput,[Text.Encoding]::ASCII)
        Move-Item -LiteralPath (Join-Path $run 'input-source.tmp') -Destination (Join-Path $run 'input-source.txt') -Force
        throw $failure
    }
    if ($ready) {
        $gaze = $null
        $bridgeError = Join-Path $run ($InputRoute + '-bridge.stderr.txt')
        if ($InputRoute -eq 'PSVR2Toolkit') {
            $bridge = Join-Path $bundle 'psvr2-gaze-bridge\psvr2_gaze_bridge.exe'
            $bridgeArgs = '--geometry "' + (Join-Path $run 'geometry.txt') + '" --config "' + $calibration +
                '" --stop-file "' + (Join-Path $run 'input-stop.request') + '" --port 5556 --seconds 86400'
            $gaze = Start-Process -FilePath $bridge -ArgumentList $bridgeArgs -WindowStyle Hidden -PassThru -RedirectStandardError $bridgeError
            $gaze.Id | Set-Content -LiteralPath (Join-Path $run 'psvr2-bridge.pid') -Encoding ASCII
        } elseif ($InputRoute -in @('ViveProEye','ViveStreaming')) {
            $source = if ($InputRoute -eq 'ViveProEye') { 'pro-eye' } else { 'vive-streaming' }
            $bridge = Join-Path $bundle 'vive-streaming-gaze-bridge\ViveStreamingGazeBridge.exe'
            $manifest = Get-Content -LiteralPath (Join-Path $bundle 'bundle-manifest.json') -Raw | ConvertFrom-Json
            $expected = @($manifest.files | Where-Object { $_.path -eq 'vive-streaming-gaze-bridge/ViveStreamingGazeBridge.exe' })
            if ($expected.Count -ne 1 -or (Get-PortableSha256 $bridge) -ne $expected[0].sha256) {
                throw 'VIVE gaze bridge file changed after packaging.'
            }
            $bridgeArgs = '--geometry "' + (Join-Path $run 'geometry.txt') + '" --config "' + $calibration +
                '" --stop-file "' + (Join-Path $run 'input-stop.request') + '" --source ' + $source +
                ' --sdk "' + $viveSdk + '" --port 5556 --seconds 86400'
            $gaze = Start-Process -FilePath $bridge -ArgumentList $bridgeArgs -WindowStyle Hidden -PassThru -RedirectStandardError $bridgeError
            $gaze.Id | Set-Content -LiteralPath (Join-Path $run 'vive-bridge.pid') -Encoding ASCII
        } elseif ($InputRoute -eq 'SteamVREye') {
            $bridge = Join-Path $bundle 'steamvr-gaze-bridge\steamvr_gaze_bridge.exe'
            $bridgeArgs = '--geometry "' + (Join-Path $run 'geometry.txt') + '" --config "' + $calibration +
                '" --manifest "' + (Join-Path $bundle 'steamvr-gaze-bridge\eye_actions.json') +
                '" --stop-file "' + (Join-Path $run 'input-stop.request') + '" --port 5556 --seconds 86400'
            $gaze = Start-Process -FilePath $bridge -ArgumentList $bridgeArgs -WindowStyle Hidden -PassThru -RedirectStandardError $bridgeError
            $gaze.Id | Set-Content -LiteralPath (Join-Path $run 'steamvr-bridge.pid') -Encoding ASCII
        } elseif ($InputRoute -eq 'PICO') {
            $gaze = Start-PicoGazeClient $bundle $run $calibration $HeadsetIp
        } elseif ($InputRoute -eq 'Varjo') {
            $bridge = Join-Path $bundle 'varjo-gaze-bridge\varjo_gaze_bridge.exe'
            $previousSearchPath = $env:PATH
            $env:PATH = (Split-Path -Parent $varjoDll) + ';' + $env:PATH
            $gainValues = @{}
            foreach ($line in Get-Content -LiteralPath $calibration) {
                $parts = $line -split '=',2
                if ($parts.Count -eq 2) { $gainValues[$parts[0]] = $parts[1] }
            }
            $culture = [Globalization.CultureInfo]::InvariantCulture
            $xGain = [double]::Parse($gainValues['x_gain'],$culture).ToString($culture)
            $yGain = [double]::Parse($gainValues['y_gain'],$culture).ToString($culture)
            $bridgeArgs = '--geometry "' + (Join-Path $run 'geometry.txt') +
                '" --config "' + $calibration + '" --stop-file "' + (Join-Path $run 'input-stop.request') +
                '" --x-gain ' + $xGain + ' --y-gain ' + $yGain + ' --port 5556 --seconds 86400'
            try { $gaze = Start-Process -FilePath $bridge -ArgumentList $bridgeArgs -WindowStyle Hidden -PassThru -RedirectStandardError $bridgeError }
            finally { $env:PATH = $previousSearchPath }
            $gaze.Id | Set-Content -LiteralPath (Join-Path $run 'varjo-bridge.pid') -Encoding ASCII
        }
        if ($gaze -and $gaze.WaitForExit(500)) {
            $detail = if (Test-Path -LiteralPath $bridgeError) { Get-Content -LiteralPath $bridgeError -Raw } else { '' }
            throw "The $InputRoute input bridge exited during startup: $detail"
        }
        if ($InputRoute -eq 'PICO') {
            Save-PicoHeadsetIp $data $HeadsetIp | Out-Null
            Save-PicoHeadsetIp $run $HeadsetIp | Out-Null
        }
        $InputRoute | Set-Content -LiteralPath (Join-Path $run 'input-route.txt') -Encoding ASCII
        if ($InputRoute -eq 'Baballonia') {
            [IO.File]::WriteAllText((Join-Path $run 'osc-settings.txt'),"$OscPort`n$OscForward`n",[Text.Encoding]::ASCII)
        }
        Write-Output "Provider started. Run directory: $run"
        if ([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ja')
            { Write-Output '選択した視線経路をTunerで確認してから、MSAAを有効にしたVRChatを起動してください。' }
        else { Write-Output 'Check the selected gaze route in Tuner, then launch VRChat with MSAA enabled.' }
        return
    }
    if (Test-Path -LiteralPath (Join-Path $run 'worker-error.txt')) { throw (Get-Content -LiteralPath (Join-Path $run 'worker-error.txt') -Raw) }
    if (Test-Path -LiteralPath (Join-Path $run 'exit-code.txt')) {
        $detail = if (Test-Path -LiteralPath (Join-Path $run 'provider.stderr.txt')) { Get-Content -LiteralPath (Join-Path $run 'provider.stderr.txt') -Raw } else { '' }
        throw "Provider exited: $(Get-Content -LiteralPath (Join-Path $run 'exit-code.txt') -Raw) $detail"
    }
    Start-Sleep -Milliseconds 200
} while ((Get-Date) -lt $deadline)
if (-not $InputOnly) { 'Startup was not confirmed' | Set-Content -LiteralPath (Join-Path $run 'stop.request') -Encoding ASCII }
else {
    [IO.File]::WriteAllText((Join-Path $run 'input-source.tmp'),$rollbackInput,[Text.Encoding]::ASCII)
    Move-Item -LiteralPath (Join-Path $run 'input-source.tmp') -Destination (Join-Path $run 'input-source.txt') -Force
}
throw "Provider startup not confirmed. Stop requested. Inspect logs in $run"
} catch {
    $startupError = $_.Exception.Message
    if ($InputOnly -and $originalInput -and -not $Recovering -and
        -not (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt') -PathType Leaf)) {
        try {
            # Rebuild the old auxiliary bridge too, not only its UDP selector.
            $recoverArgs = @{}
            if ($originalInput.inputRoute -eq 'PICO') {
                . (Join-Path $bundle 'Pico-Source.ps1')
                $recoverArgs.HeadsetIp = Get-PicoHeadsetIp $run ''
            }
            & $PSCommandPath -InputOnly -Recovering -RunDirectory $run -InputRoute $originalInput.inputRoute `
                -OscPort ([int]$originalInput.inputPort) -OscForward ([int]$originalInput.oscForward) @recoverArgs | Out-Null
        } catch { throw ($startupError + "`nPrevious input could not be restored: " + $_.Exception.Message) }
    } elseif ($run -and ((-not $InputOnly -and $workerProcess) -or
              (Test-Path -LiteralPath (Join-Path $data 'gaze-paused.txt') -PathType Leaf))) {
        'Input startup failed' | Set-Content -LiteralPath (Join-Path $run 'stop.request') -Encoding ASCII
        try {
            . (Join-Path $bundle 'Gaze-Route.ps1')
            Stop-GazeInputBridges $run
        } catch { $startupError += "`nInput cleanup: " + $_.Exception.Message }
    }
    throw $startupError
}
