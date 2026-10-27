param([string]$PimaxBridgeDll = '', [string]$VarjoSdk = '', [string]$PicoTargetFramework = 'net9.0')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$research = Split-Path -Parent $here
$release = Join-Path $here 'release'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMddTHHmmssfff'
$testLog = Join-Path $release "release-tests-$stamp.log"

& (Join-Path $here 'test.ps1') *>&1 | Tee-Object -FilePath $testLog | Out-Null
$buildArgs = @{ RequireAllBridges = $true; PicoTargetFramework = $PicoTargetFramework }
if ($PimaxBridgeDll) { $buildArgs.PimaxBridgeDll = $PimaxBridgeDll }
if ($VarjoSdk) { $buildArgs.VarjoSdk = $VarjoSdk }
$artifact = & (Join-Path $here 'Build-Portable.ps1') @buildArgs | Select-Object -Last 1
if (-not $artifact -or -not (Test-Path -LiteralPath $artifact.Zip -PathType Leaf)) {
    throw 'Full portable build did not return a ZIP.'
}

Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::OpenRead($artifact.Zip)
try {
    $manifestEntry = @($archive.Entries | Where-Object { $_.FullName -match '^[^/]+/bundle-manifest\.json$' })
    if ($manifestEntry.Count -ne 1) { throw 'ZIP must contain exactly one bundle manifest.' }
    $root = $manifestEntry[0].FullName.Substring(0, $manifestEntry[0].FullName.IndexOf('/') + 1)
    $reader = [IO.StreamReader]::new($manifestEntry[0].Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json }
    finally { $reader.Dispose() }
    $listed = @($manifest.files)
    $required = @('VRSS Tuner.exe','steam_vrchat_launch.exe','steamvr_autostart.exe','vrss-tuner.vrmanifest','openvr_api.dll','Install-Steam-Launch-Bridge.ps1',
        'Install-Steam-Launch-Bridge.cmd','common-vrss-provider/common_vrss_provider.exe','Audit-Driver.cmd','Audit-Driver.ps1',
        'Start-Common-Session.ps1','Gaze-Route.ps1','Set-Gaze-Route.ps1','Start-Provider-Only.cmd','Install-SteamVR-Autostart.cmd','Remove-SteamVR-Autostart.cmd',
        'steamvr-gaze-bridge/steamvr_gaze_bridge.exe','psvr2-gaze-bridge/psvr2_gaze_bridge.exe',
        'vive-streaming-gaze-bridge/ViveStreamingGazeBridge.exe',
        'pico-gaze-bridge/PicoGazeBridge.exe','Pico-Source.ps1','Start-PICO.cmd','PICO.md',
        'varjo-gaze-bridge/varjo_gaze_bridge.exe','pimax-eye-bridge/driver_sranibro_eye.dll',
        'STEAM-FRAME.md','DRIVER-CONTRACTS.md','THIRD_PARTY_NOTICES.md')
    foreach ($name in $required) {
        if ($name -notin @($listed.path)) { throw "Release ZIP missing required component: $name" }
    }
    foreach ($name in @($listed.path | Where-Object { $_ -like '*.ps1' })) {
        $scriptEntry = $archive.GetEntry($root + $name)
        if (-not $scriptEntry) { throw "Release ZIP missing script: $name" }
        $scriptStream = $scriptEntry.Open()
        try {
            $prefix = [byte[]]::new(3)
            $count = $scriptStream.Read($prefix, 0, 3)
            if ($count -ne 3 -or $prefix[0] -ne 239 -or $prefix[1] -ne 187 -or $prefix[2] -ne 191) {
                throw "Windows PowerShell script is missing UTF-8 BOM: $name"
            }
            $scriptReader = [IO.StreamReader]::new($scriptStream,[Text.Encoding]::UTF8)
            try { $scriptText = $scriptReader.ReadToEnd() } finally { $scriptReader.Dispose() }
            $tokens = $null; $parseErrors = $null
            $null = [Management.Automation.Language.Parser]::ParseInput($scriptText,[ref]$tokens,[ref]$parseErrors)
            if ($parseErrors.Count) { throw "Windows PowerShell syntax error in $name : $($parseErrors[0].Message)" }
        } finally { $scriptStream.Dispose() }
    }
    $actual = @($archive.Entries | Where-Object { $_.Name })
    if ($actual.Count -ne $listed.Count + 1) { throw 'ZIP entry count does not match its manifest.' }
    if (@($listed.path | Select-Object -Unique).Count -ne $listed.Count) { throw 'Duplicate manifest paths.' }
    foreach ($item in $listed) {
        $entry = $archive.GetEntry($root + $item.path)
        if (-not $entry) { throw "Manifest path not found in ZIP: $($item.path)" }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -ne $item.sha256) { throw "ZIP content hash mismatch: $($item.path)" }
    }
} finally { $archive.Dispose() }

$setupResult = & (Join-Path $here 'Build-Installer.ps1') -Bundle $artifact.Bundle | Select-Object -Last 1
if (-not $setupResult -or -not (Test-Path -LiteralPath $setupResult.Installer -PathType Leaf)) {
    throw 'Installer build did not return an executable.'
}
$zipHash = (Get-FileHash -LiteralPath $artifact.Zip -Algorithm SHA256).Hash
$setupHash = (Get-FileHash -LiteralPath $setupResult.Installer -Algorithm SHA256).Hash
$stem = [IO.Path]::GetFileNameWithoutExtension($artifact.Zip)
$sums = Join-Path $release "$stem.SHA256SUMS.txt"
[IO.File]::WriteAllText($sums,
    "$setupHash  $([IO.Path]::GetFileName($setupResult.Installer))`n$zipHash  $([IO.Path]::GetFileName($artifact.Zip))`n",
    [Text.Encoding]::ASCII)
$revision = (& git -C $research rev-parse HEAD).Trim()
$report = Join-Path $release "$stem.release-candidate.json"
[ordered]@{
    format = 1
    sourceRevision = $revision
    sourceDirty = [bool]@(& git -C $research status --porcelain)
    builtAt = (Get-Date).ToString('o')
    installer = [IO.Path]::GetFileName($setupResult.Installer)
    installerSha256 = $setupHash
    zip = [IO.Path]::GetFileName($artifact.Zip)
    sha256 = $zipHash
    verifiedFiles = $listed.Count
    testsLog = [IO.Path]::GetFileName($testLog)
    pimaxBridgeInputSha256 = (Get-FileHash -LiteralPath (Join-Path $artifact.Bundle 'pimax-eye-bridge\driver_sranibro_eye.dll') -Algorithm SHA256).Hash
    varjoBridgeInputSha256 = (Get-FileHash -LiteralPath (Join-Path $artifact.Bundle 'varjo-gaze-bridge\varjo_gaze_bridge.exe') -Algorithm SHA256).Hash
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8
[pscustomobject]@{Installer=$setupResult.Installer;InstallerSha256=$setupHash;Zip=$artifact.Zip;ZipSha256=$zipHash;VerifiedFiles=$listed.Count;Report=$report;Checksums=$sums;Tests=$testLog}
