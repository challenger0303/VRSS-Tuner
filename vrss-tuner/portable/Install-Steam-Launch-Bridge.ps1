param([switch]$ShowOnly)
$ErrorActionPreference='Stop'
$bundle=$PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $bundle 'bundle-manifest.json') -Raw | ConvertFrom-Json
$names=@('steam_vrchat_launch.exe','openvr_api.dll')
foreach($name in $names) {
    $item=@($manifest.files | Where-Object {$_.path -eq $name})
    $path=Join-Path $bundle $name
    if($item.Count -ne 1 -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Bundle is missing $name"
    }
    $stream=[IO.File]::OpenRead($path)
    $sha=[Security.Cryptography.SHA256]::Create()
    try {$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}
    finally {$sha.Dispose();$stream.Dispose()}
    if($hash -ne $item[0].sha256) {throw "Bundle file changed: $name"}
}
$destination=Join-Path $env:LOCALAPPDATA 'VRSS Tuner\SteamLaunch'
$target=Join-Path $destination 'steam_vrchat_launch.exe'
if(-not $ShowOnly) {
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach($name in $names) {Copy-Item -LiteralPath (Join-Path $bundle $name) -Destination (Join-Path $destination $name) -Force}
}
$option='"'+$target+'" %command%'
$action=if([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ja') {
    'Steamの「VRChat → プロパティ → 一般 → 起動オプション」にlaunchOptionを一度設定してください。共通プロバイダー停止中は通常起動します。'
} else {
    'Set launchOption once in Steam > VRChat > Properties > General. The bridge passes through while the common provider is stopped.'
}
[pscustomobject]@{
    installed=(-not $ShowOnly)
    launchOption=$option
    steamGame='VRChat'
    action=$action
} | ConvertTo-Json
