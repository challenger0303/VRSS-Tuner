param([string]$PimaxBridgeDll = '', [string]$VarjoSdk = '', [switch]$RequireAllBridges,
      [string]$PicoTargetFramework = 'net9.0')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$research = Split-Path -Parent $here
. (Join-Path $here 'Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$nvapi = Join-Path $research 'third_party\nvapi'
$pimaxBuilt = if ($PimaxBridgeDll) { [IO.Path]::GetFullPath($PimaxBridgeDll) } else {
    Join-Path $research 'pimax-eye-bridge\bin\driver_sranibro_eye.dll'
}
$varjoBuilt = Join-Path $research 'varjo-gaze-bridge\build\varjo_gaze_bridge.exe'
if ($RequireAllBridges) {
    if (-not (Test-Path -LiteralPath $pimaxBuilt -PathType Leaf)) {
        throw "Required Pimax eye bridge is missing: $pimaxBuilt"
    }
    if (-not $VarjoSdk -and -not (Test-Path -LiteralPath $varjoBuilt -PathType Leaf)) {
        throw "Required Varjo gaze bridge is missing: $varjoBuilt (or supply -VarjoSdk)"
    }
}
$stamp = Get-Date -Format 'yyyyMMddTHHmmssfff'
$release = Join-Path $here 'release'
$bundle = Join-Path $release ('VRSS-Tuner-Portable-' + $stamp)
$common = Join-Path $bundle 'common-vrss-provider'
New-Item -ItemType Directory -Path $common -ErrorAction Stop | Out-Null
& (Join-Path $here 'build.ps1') -OutputPath (Join-Path $bundle 'VRSS Tuner.exe') -StaticRuntime | Out-Null
$openvr = Join-Path $research 'third_party\openvr'
$providerSource = Join-Path $research 'common-vrss-provider\provider.cpp'
$providerExe = Join-Path $common 'common_vrss_provider.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"' + $nvapi + '" /Fo"' + (Join-Path $here 'build\provider.portable.obj') + '" "' + $providerSource + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" /LIBPATH:"' + (Join-Path $nvapi 'amd64') + '" /OUT:"' + $providerExe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Portable provider build failed' }
$exportSource = Join-Path $research 'common-vrss-provider\export_openvr_geometry.cpp'
$exportExe = Join-Path $common 'export_openvr_geometry.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"' + $research + '" /Fo"' + (Join-Path $here 'build\export_openvr_geometry.portable.obj') + '" "' + $exportSource + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" /OUT:"' + $exportExe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Portable geometry exporter build failed' }
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination (Join-Path $common 'openvr_api.dll')
$steamLauncher = Join-Path $bundle 'steam_vrchat_launch.exe'
$steamLauncherSource = Join-Path $here 'steam_vrchat_launch.cpp'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"' + $research + '" /Fo"' + (Join-Path $here 'build\steam_vrchat_launch.portable.obj') + '" "' + $steamLauncherSource + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /OUT:"' + $steamLauncher + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'Steam VRChat launch bridge build failed' }
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination (Join-Path $bundle 'openvr_api.dll')
$steamAutostart = Join-Path $bundle 'steamvr_autostart.exe'
$autostartSource = Join-Path $here 'steamvr_autostart.cpp'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"' + $research + '" /Fo"' + (Join-Path $here 'build\steamvr_autostart.portable.obj') + '" "' + $autostartSource + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" Shell32.lib /OUT:"' + $steamAutostart + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'SteamVR autostart helper build failed' }
Copy-Item -LiteralPath (Join-Path $here 'portable\vrss-tuner.vrmanifest') -Destination (Join-Path $bundle 'vrss-tuner.vrmanifest')
Copy-Item -LiteralPath (Join-Path $here 'portable\calibration.txt') -Destination (Join-Path $common 'calibration.txt')
$driverContracts = Join-Path $common 'driver-contracts'
New-Item -ItemType Directory -Path $driverContracts -ErrorAction Stop | Out-Null
Copy-Item -LiteralPath (Join-Path $research 'common-vrss-provider\driver-contracts\616.92.txt') -Destination $driverContracts
Copy-Item -LiteralPath (Join-Path $here 'portable\Test-Driver-Contract.ps1') -Destination $bundle
Copy-Item -LiteralPath (Join-Path $here 'portable\Test-Driver-Contract.cmd') -Destination $bundle
$steamBridge = Join-Path $bundle 'steamvr-gaze-bridge'
New-Item -ItemType Directory -Path $steamBridge -ErrorAction Stop | Out-Null
$steamSource = Join-Path $research 'steamvr-gaze-bridge\bridge.cpp'
$steamExe = Join-Path $steamBridge 'steamvr_gaze_bridge.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"' + $research + '" /Fo"' + (Join-Path $here 'build\steamvr_gaze_bridge.portable.obj') + '" "' + $steamSource + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" /OUT:"' + $steamExe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'SteamVR eye bridge build failed' }
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination (Join-Path $steamBridge 'openvr_api.dll')
Copy-Item -LiteralPath (Join-Path $research 'steamvr-gaze-probe\eye_actions.json') -Destination (Join-Path $steamBridge 'eye_actions.json')
$psvr2Bridge = Join-Path $bundle 'psvr2-gaze-bridge'
New-Item -ItemType Directory -Path $psvr2Bridge -ErrorAction Stop | Out-Null
$psvr2Source = Join-Path $research 'psvr2-gaze-bridge\bridge.cpp'
$psvr2Exe = Join-Path $psvr2Bridge 'psvr2_gaze_bridge.exe'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /DUNICODE /D_UNICODE /Fo"' + (Join-Path $here 'build\psvr2_gaze_bridge.portable.obj') + '" "' + $psvr2Source + '" /link /OUT:"' + $psvr2Exe + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw 'PSVR2 bridge build failed' }
Copy-Item -LiteralPath (Join-Path $research 'psvr2-gaze-bridge\PSVR2Toolkit-LICENSE.txt') -Destination (Join-Path $psvr2Bridge 'PSVR2Toolkit-LICENSE.txt')
$viveBridge = Join-Path $bundle 'vive-streaming-gaze-bridge'
& dotnet publish (Join-Path $research 'vive-streaming-gaze-bridge\ViveStreamingGazeBridge.csproj') `
    -c Release -r win-x64 -p:PublishAot=true -p:StripSymbols=true -o $viveBridge | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $viveBridge 'ViveStreamingGazeBridge.exe'))) {
    throw 'VIVE gaze bridge native build failed'
}
Remove-Item -LiteralPath (Join-Path $viveBridge 'ViveStreamingGazeBridge.pdb') -ErrorAction SilentlyContinue
$picoBridge = Join-Path $bundle 'pico-gaze-bridge'
# NativeAOT is self-contained, like the VIVE client: no runtime installer or
# machine-wide .NET changes. PicoTargetFramework controls both restore and build.
& dotnet publish (Join-Path $research 'pico-gaze-bridge\PicoGazeBridge.csproj') `
    -c Release -r win-x64 -p:PicoTargetFramework=$PicoTargetFramework `
    -p:PublishAot=true -p:StripSymbols=true -o $picoBridge | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $picoBridge 'PicoGazeBridge.exe'))) {
    throw 'PICO direct gaze client self-contained build failed'
}
Remove-Item -LiteralPath (Join-Path $picoBridge 'PicoGazeBridge.pdb') -ErrorAction SilentlyContinue
$varjoBridge = Join-Path $bundle 'varjo-gaze-bridge'
New-Item -ItemType Directory -Path $varjoBridge -ErrorAction Stop | Out-Null
if ($VarjoSdk) {
    & (Join-Path $research 'varjo-gaze-bridge\build.ps1') -VarjoSdk $VarjoSdk | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Varjo bridge build failed.' }
}
if (Test-Path -LiteralPath $varjoBuilt -PathType Leaf) {
    Copy-Item -LiteralPath $varjoBuilt -Destination (Join-Path $varjoBridge 'varjo_gaze_bridge.exe')
} else {
    if ($RequireAllBridges) { throw "Required Varjo gaze bridge is missing: $varjoBuilt" }
    Write-Warning 'Varjo SDK bridge is not bundled; supply -VarjoSdk to build it.'
}
$pimaxBridge = Join-Path $bundle 'pimax-eye-bridge'
New-Item -ItemType Directory -Path $pimaxBridge -ErrorAction Stop | Out-Null
if (Test-Path -LiteralPath $pimaxBuilt -PathType Leaf) {
    Copy-Item -LiteralPath $pimaxBuilt -Destination (Join-Path $pimaxBridge 'driver_sranibro_eye.dll')
} else {
    if ($RequireAllBridges) { throw "Required Pimax eye bridge is missing: $pimaxBuilt" }
    Write-Warning 'Pimax eye-device bridge is not bundled; supply -PimaxBridgeDll if needed.'
}
foreach ($name in @('Gaze-Route.ps1','Set-Gaze-Route.ps1','Audit-Driver.ps1','Test-Setup.ps1','Start-Provider.ps1','Start-Common-Session.ps1','Stop-Provider.ps1','Start-VRChat-HMD-Key.ps1','Start-VRChat-Candidate.ps1','Install-Steam-Launch-Bridge.ps1','Audit-Driver.cmd','Test-Setup.cmd','Start-Provider.cmd','Start-Provider-Only.cmd','Stop-Provider.cmd','Start-VRChat-HMD-Key.cmd','Start-VRChat-Candidate.cmd','Install-Steam-Launch-Bridge.cmd','Install-SteamVR-Autostart.cmd','Remove-SteamVR-Autostart.cmd','Start-PSVR2-Toolkit.cmd','Start-SRanibro.cmd','Start-Vive-Pro-Eye.cmd','Start-Vive-Streaming.cmd','Start-SteamVR-Eye.cmd','Start-Varjo.cmd')) {
    Copy-Item -LiteralPath (Join-Path $here "portable\$name") -Destination (Join-Path $bundle $name)
}
foreach ($name in @('Pico-Source.ps1','Start-PICO.cmd')) {
    Copy-Item -LiteralPath (Join-Path $here "portable\$name") -Destination (Join-Path $bundle $name)
}
# The .cmd launchers use Windows PowerShell 5.1, which reads UTF-8 scripts
# without a BOM as the legacy ANSI code page. Normalize every shipped script.
foreach ($name in @('Gaze-Route.ps1','Set-Gaze-Route.ps1','Audit-Driver.ps1','Test-Setup.ps1','Start-Provider.ps1','Start-Common-Session.ps1','Stop-Provider.ps1','Start-VRChat-HMD-Key.ps1','Start-VRChat-Candidate.ps1','Install-Steam-Launch-Bridge.ps1','Pico-Source.ps1','Test-Driver-Contract.ps1')) {
    $path = Join-Path $bundle $name
    $source = [IO.File]::ReadAllText($path, [Text.UTF8Encoding]::new($false, $true))
    [IO.File]::WriteAllText($path, $source, [Text.UTF8Encoding]::new($true))
}
Copy-Item -LiteralPath (Join-Path $here 'portable\README.md') -Destination (Join-Path $bundle 'README.md')
Copy-Item -LiteralPath (Join-Path $here 'portable\README.en.md') -Destination (Join-Path $bundle 'README.en.md')
foreach ($name in @('STEAM-FRAME.md','DRIVER-CONTRACTS.md','PICO.md')) {
    Copy-Item -LiteralPath (Join-Path $research "docs\$name") -Destination (Join-Path $bundle $name)
}
Copy-Item -LiteralPath (Join-Path $research 'docs\FACE-TRACKING-COEXISTENCE.md') -Destination (Join-Path $bundle 'FACE-TRACKING-COEXISTENCE.md')
Copy-Item -LiteralPath (Join-Path $research 'docs\HMD-GAZE-COMPATIBILITY.md') -Destination (Join-Path $bundle 'HMD-GAZE-COMPATIBILITY.md')
Copy-Item -LiteralPath (Join-Path $research 'LICENSE') -Destination (Join-Path $bundle 'LICENSE.txt') -ErrorAction Stop
Copy-Item -LiteralPath (Join-Path $openvr 'LICENSE') -Destination (Join-Path $bundle 'OpenVR-LICENSE.txt') -ErrorAction Stop
Copy-Item -LiteralPath (Join-Path $nvapi 'License.txt') -Destination (Join-Path $bundle 'NVAPI-LICENSE.txt')
$notices = [IO.File]::ReadAllText((Join-Path $research 'THIRD_PARTY_NOTICES.md'))
$notices = $notices.Replace('](LICENSE)', '](LICENSE.txt)').Replace('](third_party/nvapi/License.txt)', '](NVAPI-LICENSE.txt)').Replace('](third_party/openvr/LICENSE)', '](OpenVR-LICENSE.txt)')
[IO.File]::WriteAllText((Join-Path $bundle 'THIRD_PARTY_NOTICES.md'), $notices, [Text.UTF8Encoding]::new($false))
$files = @('VRSS Tuner.exe','openvr_api.dll','steam_vrchat_launch.exe','steamvr_autostart.exe','vrss-tuner.vrmanifest','common-vrss-provider/common_vrss_provider.exe',
    'common-vrss-provider/driver-contracts/616.92.txt','Test-Driver-Contract.ps1','Test-Driver-Contract.cmd',
    'common-vrss-provider/export_openvr_geometry.exe','common-vrss-provider/openvr_api.dll',
    'common-vrss-provider/calibration.txt',
    'steamvr-gaze-bridge/steamvr_gaze_bridge.exe','steamvr-gaze-bridge/openvr_api.dll','steamvr-gaze-bridge/eye_actions.json',
    'psvr2-gaze-bridge/psvr2_gaze_bridge.exe','psvr2-gaze-bridge/PSVR2Toolkit-LICENSE.txt',
    'vive-streaming-gaze-bridge/ViveStreamingGazeBridge.exe',
    'Pico-Source.ps1','Start-PICO.cmd',
    'Gaze-Route.ps1','Set-Gaze-Route.ps1','Audit-Driver.ps1','Test-Setup.ps1','Start-Provider.ps1','Start-Common-Session.ps1','Stop-Provider.ps1','Start-VRChat-HMD-Key.ps1','Start-VRChat-Candidate.ps1','Install-Steam-Launch-Bridge.ps1',
    'Audit-Driver.cmd','Test-Setup.cmd','Start-Provider.cmd','Start-Provider-Only.cmd','Stop-Provider.cmd','Start-VRChat-HMD-Key.cmd','Start-VRChat-Candidate.cmd','Install-Steam-Launch-Bridge.cmd','Install-SteamVR-Autostart.cmd','Remove-SteamVR-Autostart.cmd',
    'Start-PSVR2-Toolkit.cmd','Start-SRanibro.cmd',
    'Start-Vive-Pro-Eye.cmd','Start-Vive-Streaming.cmd','Start-SteamVR-Eye.cmd','Start-Varjo.cmd',
    'README.md','README.en.md','FACE-TRACKING-COEXISTENCE.md',
    'HMD-GAZE-COMPATIBILITY.md','LICENSE.txt','THIRD_PARTY_NOTICES.md',
    'OpenVR-LICENSE.txt','NVAPI-LICENSE.txt','STEAM-FRAME.md','DRIVER-CONTRACTS.md','PICO.md')
foreach ($optional in @('varjo-gaze-bridge/varjo_gaze_bridge.exe','pimax-eye-bridge/driver_sranibro_eye.dll')) {
    if (Test-Path -LiteralPath (Join-Path $bundle $optional) -PathType Leaf) { $files += $optional }
}
foreach ($runtimeFile in Get-ChildItem -LiteralPath $picoBridge -File -Recurse) {
    $files += $runtimeFile.FullName.Substring($bundle.Length + 1).Replace('\','/')
}
if ($RequireAllBridges) {
    foreach ($required in @('varjo-gaze-bridge/varjo_gaze_bridge.exe','pimax-eye-bridge/driver_sranibro_eye.dll')) {
        if ($required -notin $files) { throw "Full portable bundle is missing: $required" }
    }
}
$entries = foreach ($name in $files) {
    [ordered]@{path=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $bundle $name) -Algorithm SHA256).Hash}
}
[ordered]@{format=1;builtAt=(Get-Date).ToString('o');files=@($entries)} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bundle 'bundle-manifest.json') -Encoding UTF8
$zip = $bundle + '.zip'
# PowerShell 5.1 writes backslashes into ZIP entry names on Windows. Use
# portable '/' entry names so the release verifier and other ZIP readers agree.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    $rootName = [IO.Path]::GetFileName($bundle)
    foreach ($file in Get-ChildItem -LiteralPath $bundle -File -Recurse) {
        $relative = $file.FullName.Substring($bundle.Length + 1).Replace('\', '/')
        $entry = $archive.CreateEntry($rootName + '/' + $relative, [IO.Compression.CompressionLevel]::Optimal)
        $source = [IO.File]::OpenRead($file.FullName)
        $destination = $entry.Open()
        try { $source.CopyTo($destination) }
        finally { $destination.Dispose(); $source.Dispose() }
    }
} finally { $archive.Dispose() }
[pscustomobject]@{Bundle=$bundle;Zip=$zip;Sha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash}
