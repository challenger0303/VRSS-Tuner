param([switch]$AllowRunningVRChat,[switch]$AllowOfficialHandoff)
$ErrorActionPreference = 'Stop'
$bundle = $PSScriptRoot
$provider = Join-Path $bundle 'common-vrss-provider\common_vrss_provider.exe'
$exporter = Join-Path $bundle 'common-vrss-provider\export_openvr_geometry.exe'
$tuner = Join-Path $bundle 'VRSS Tuner.exe'
$calibration = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\common-calibration.txt'
$data = Join-Path $env:LOCALAPPDATA 'VRSS Tuner\preflight'
function Get-PortableSha256([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}
New-Item -ItemType Directory -Path $data -Force | Out-Null
if (-not (Test-Path -LiteralPath $calibration -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $bundle 'common-vrss-provider\calibration.txt') -Destination $calibration
}
$reasons = [Collections.Generic.List[string]]::new()
$integrity = $true
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'bundle-manifest.json') -Raw | ConvertFrom-Json
$bundleFull = [IO.Path]::GetFullPath($bundle).TrimEnd('\') + '\'
# An installer upgrade can run while the provider is connected to VRChat.
# In that case its executable and OpenVR DLL remain in use. Apply only
# manifest-verified staged files after the old provider has exited.
$stagedProvider = Join-Path $bundle 'updates\common_vrss_provider.exe'
if (Test-Path -LiteralPath $stagedProvider -PathType Leaf) {
    foreach ($name in @('common_vrss_provider.exe','openvr_api.dll')) {
        $relative = 'common-vrss-provider/' + $name
        $entry = @($manifest.files | Where-Object { $_.path -eq $relative })
        $staged = Join-Path $bundle ('updates\' + $name)
        $target = Join-Path $bundle ('common-vrss-provider\' + $name)
        if ($entry.Count -ne 1 -or -not (Test-Path -LiteralPath $staged -PathType Leaf) -or
            (Get-PortableSha256 $staged) -ne $entry[0].sha256) {
            $integrity = $false
            $reasons.Add("Staged provider update is missing or changed: $name")
            continue
        }
        if (-not (Get-Process common_vrss_provider -ErrorAction SilentlyContinue) -and
            (-not (Test-Path -LiteralPath $target -PathType Leaf) -or
             (Get-PortableSha256 $target) -ne $entry[0].sha256)) {
            try { Copy-Item -LiteralPath $staged -Destination $target -Force -ErrorAction Stop }
            catch { $integrity = $false; $reasons.Add("Staged provider update could not be applied: $name") }
        }
    }
}
foreach ($entry in $manifest.files) {
    $path = [IO.Path]::GetFullPath((Join-Path $bundle $entry.path))
    if (-not $path.StartsWith($bundleFull, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $path -PathType Leaf) -or
        # Tuner edits calibration.txt; --readiness and --check-only validate it below.
        ($entry.path -ne 'common-vrss-provider/calibration.txt' -and
         (Get-PortableSha256 $path) -ne $entry.sha256)) {
        $integrity = $false
        $reasons.Add("Package file missing or changed: $($entry.path)")
    }
}
$driver = $false
$driverMessage = 'not checked'
if ($integrity) {
    $driverOutput = & $provider --driver-check-only 1 2>&1 | Out-String
    $driver = $LASTEXITCODE -eq 0 -and $driverOutput.Contains('DRIVER_CHECK_PASS')
    $driverMessage = $driverOutput.Trim()
    if (-not $driver) { $reasons.Add('NVIDIA driver image or gaze methods do not match an exact contract.') }
}
$details = @{}
if ($integrity) {
    $detailsFile = Join-Path $data 'tuner-readiness.txt'
    $tunerRun = Start-Process -FilePath $tuner -ArgumentList ('--readiness "' + $detailsFile + '"') -WindowStyle Hidden -Wait -PassThru
    if ($tunerRun.ExitCode -eq 0 -and (Test-Path -LiteralPath $detailsFile)) {
        foreach ($line in Get-Content -LiteralPath $detailsFile) {
            $parts = $line -split '=',2
            if ($parts.Count -eq 2) { $details[$parts[0]] = $parts[1] }
        }
    }
}
$profile = $details['nvapi_vrchat_profile'] -eq 'readable'
$allow = $details['vrss_allow'] -eq 'enabled'
$mode = $details['vrss_mode'] -eq 'on'
$config = $details['common_calibration'] -eq 'readable'
if (-not $profile) { $reasons.Add('NVIDIA VRChat profile is not readable.') }
if (-not $allow) { $reasons.Add('This VRChat profile does not have the VRSS allow gate enabled.') }
if (-not $mode) { $reasons.Add('VRSS is Off. Enable it in VRSS Tuner before launching VRChat.') }
if (-not $config) { $reasons.Add('Common-provider calibration is missing or invalid.') }
$steamVr = [bool](Get-Process vrserver -ErrorAction SilentlyContinue)
$vrchat = [bool](Get-Process VRChat -ErrorAction SilentlyContinue)
$official = [bool](Get-Process vrss_gaze_provider -ErrorAction SilentlyContinue)
$common = [bool](Get-Process common_vrss_provider -ErrorAction SilentlyContinue)
if (-not $steamVr) { $reasons.Add('Start SteamVR with a connected HMD.') }
if ($vrchat -and -not $AllowRunningVRChat) { $reasons.Add('Close VRChat normally before starting the gaze provider.') }
if ($official -and -not $AllowOfficialHandoff) { $reasons.Add('An official gaze provider is active; keep that route or stop it using the vendor controls.') }
if ($common) { $reasons.Add('A common provider is already running.') }
$geometry = $false
$geometryFile = Join-Path $data 'geometry.txt'
$identitySource = 'unavailable'
$identityVendor = 0
$identityProduct = 0
$identityFingerprint = ''
$candidate = $env:VRSS_HMD_ID_CANDIDATE
$candidateValid = $false
if ($candidate) {
    if ($candidate -match '^([1-9][0-9]{0,4}):([1-9][0-9]{0,4})$') {
        $candidateVendor = [int]$Matches[1]
        $candidateProduct = [int]$Matches[2]
        $candidateValid = $candidateVendor -le 65535 -and $candidateProduct -le 65535
    }
    if (-not $candidateValid) {
        $reasons.Add('Invalid VRSS_HMD_ID_CANDIDATE. Use decimal vendor:product, each 1..65535.')
    }
}
if ($integrity -and $steamVr) {
    $exportArgs = @($geometryFile)
    if ($candidateValid) {
        $exportArgs += @('--candidate-id',$candidateVendor,$candidateProduct)
    } elseif (-not $candidate -and $details['hmd_test_branch'] -eq 'enabled') {
        $exportArgs += '--common-session-key'
    }
    Remove-Item -LiteralPath $geometryFile -ErrorAction SilentlyContinue
    $exportResult = & $exporter @exportArgs 2>&1 | Out-String
    $geometry = $LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $geometryFile)
    if ($geometry) {
        $fields = @{}
        foreach ($line in Get-Content -LiteralPath $geometryFile) {
            $parts = $line -split '=',2
            if ($parts.Count -eq 2) { $fields[$parts[0]] = $parts[1] }
        }
        $identitySource = $fields['identitySource']
        $identityVendor = [int]$fields['vendor']
        $identityProduct = [int]$fields['product']
        $identityFingerprint = $fields['identityFingerprint']
    }
    if ($geometry -and $config) {
        $null = & $provider --geometry $geometryFile --config $calibration --output $data --check-only 1 2>&1 | Out-String
        $geometry = $LASTEXITCODE -eq 0
    }
    if (-not $geometry) {
        if ($exportResult.Contains('HMD_EDID_AND_STABLE_ID_UNAVAILABLE')) {
            $reasons.Add('The HMD exposes neither a usable EDID nor a stable OpenVR tracking-system and serial pair.')
        } else { $reasons.Add('Could not read and validate the connected HMD geometry.') }
    }
}
if ($identitySource -in @('candidate-auto','candidate-manual','tuner-session') -and $details['hmd_test_branch'] -ne 'enabled') {
    $reasons.Add('Candidate HMD identity needs the audited NVIDIA HMD test branch in the VRChat profile.')
}
$reasonsJapanese = @(foreach ($reason in $reasons) {
    switch ($reason) {
        'NVIDIA driver image or gaze methods do not match an exact contract.' { 'NVIDIAドライバーの実体または視線メソッドが登録済み契約と一致しません。'; break }
        'NVIDIA VRChat profile is not readable.' { 'NVIDIAのVRChatプロファイルを読み取れません。'; break }
        'This VRChat profile does not have the VRSS allow gate enabled.' { 'このVRChatプロファイルではVRSSが許可されていません。'; break }
        'VRSS is Off. Enable it in VRSS Tuner before launching VRChat.' { 'VRSSがオフです。VRChat起動前にTunerでオンにしてください。'; break }
        'Common-provider calibration is missing or invalid.' { '共通プロバイダーの補正ファイルがありません、または無効です。'; break }
        'Start SteamVR with a connected HMD.' { 'HMDを接続してSteamVRを起動してください。'; break }
        'Close VRChat normally before starting the gaze provider.' { '視線プロバイダーの起動前にVRChatを通常終了してください。'; break }
        'An official gaze provider is active; keep that route or stop it using the vendor controls.' { '公式視線プロバイダーが動作中です。公式経路を使うか、ベンダーの操作で停止してください。'; break }
        'A common provider is already running.' { '共通プロバイダーはすでに動作しています。'; break }
        'Could not read and validate the connected HMD geometry.' { '接続中のHMDの投影情報を読み取り・検証できません。'; break }
        'The HMD exposes neither a usable EDID nor a stable OpenVR tracking-system and serial pair.' { 'HMDからEDIDも安定したOpenVRの追跡システム名とシリアル番号も取得できません。'; break }
        'Invalid VRSS_HMD_ID_CANDIDATE. Use decimal vendor:product, each 1..65535.' { 'VRSS_HMD_ID_CANDIDATEが不正です。vendor:productを十進数1～65535で指定してください。'; break }
        'Candidate HMD identity needs the audited NVIDIA HMD test branch in the VRChat profile.' { '代替HMD IDを使うには、VRChatプロファイルに監査済みのNVIDIA HMDテスト経路が必要です。'; break }
        default { "パッケージのファイルが不足または変更されています: $reason" }
    }
})
[pscustomobject]@{
    format = 1
    readyForProviderLaunch = $reasons.Count -eq 0
    packageIntegrity = $integrity
    driverSupported = $driver
    driverMessage = $driverMessage
    vrchatProfileReadable = $profile
    vrssAllowed = $allow
    hmdTestBranch = $details['hmd_test_branch'] -eq 'enabled'
    vrssOn = $mode
    commonCalibrationReadable = $config
    steamVrRunning = $steamVr
    vrchatRunning = $vrchat
    officialProviderRunning = $official
    pimaxXr5RuntimeRunning = $details['pimax_xr5_runtime_running'] -eq 'yes'
    pimaxServiceRunning = $details['pimax_service_running'] -eq 'yes'
    pimaxServiceDisabled = $details['pimax_service_disabled'] -eq 'yes'
    sranibroRunning = $details['sranibro_running'] -eq 'yes'
    pimaxBridgePresent = $details['pimax_bridge_present'] -eq 'yes'
    commonProviderRunning = $common
    hmdGeometryValid = $geometry
    hmdIdentitySource = $identitySource
    hmdIdentityVendor = $identityVendor
    hmdIdentityProduct = $identityProduct
    hmdIdentityFingerprint = $identityFingerprint
    hmdIdentityCandidate = $identitySource -in @('candidate-auto','candidate-manual','tuner-session')
    vrchatLaunchMode = $(if ($details['hmd_test_branch'] -eq 'enabled') { 'protected-hmd-key' }
                         elseif ($identitySource -in @('candidate-auto','candidate-manual','tuner-session')) { 'unavailable' }
                         else { 'steam' })
    gazeInput = 'Select and start the matching gaze source in VRSS Tuner after provider launch; live tracking still requires a headset check.'
    reasons = @($reasons)
    reasonsJapanese = @($reasonsJapanese)
} | ConvertTo-Json -Depth 4
