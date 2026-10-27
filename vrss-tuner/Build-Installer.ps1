param([Parameter(Mandatory=$true)][string]$Bundle)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$resolved=[IO.Path]::GetFullPath($Bundle)
$manifestPath=Join-Path $resolved 'bundle-manifest.json'
if(-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){throw 'Portable bundle manifest is missing.'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach($entry in $manifest.files){
    $path=Join-Path $resolved ($entry.path.Replace('/','\'))
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Bundle file missing: $($entry.path)"}
    $stream=[IO.File]::OpenRead($path)
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}
    finally{$sha.Dispose();$stream.Dispose()}
    if($hash -ne $entry.sha256){throw "Bundle hash mismatch: $($entry.path)"}
}
$compiler=Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
if(-not (Test-Path -LiteralPath $compiler -PathType Leaf)){
    $compiler=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
}
if(-not (Test-Path -LiteralPath $compiler -PathType Leaf)){throw 'Inno Setup 6 compiler not found.'}
$output=Join-Path $here 'release'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $compiler "/DBundleDir=$resolved" "/DOutputDir=$output" (Join-Path $here 'installer\VRSS-Tuner.iss') | Out-Null
if($LASTEXITCODE -ne 0){throw "Installer build failed: $LASTEXITCODE"}
$setup=Join-Path $output 'VRSS-Tuner-Setup-0.1.0-beta.5.exe'
if(-not (Test-Path -LiteralPath $setup -PathType Leaf)){throw 'Installer output is missing.'}
[pscustomobject]@{Installer=$setup;Sha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash;VerifiedFiles=@($manifest.files).Count}
