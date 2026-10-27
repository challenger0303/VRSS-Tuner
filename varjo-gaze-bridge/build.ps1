param([Parameter(Mandatory=$true)][string]$VarjoSdk)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
. (Join-Path $here '..\vrss-tuner\Find-Vcvars.ps1')
$vcvars=Find-Vcvars64
$sdk=[IO.Path]::GetFullPath($VarjoSdk)
$include=Join-Path $sdk 'include\Varjo.h'
$library=Join-Path $sdk 'lib\VarjoLib.lib'
if (-not (Test-Path -LiteralPath $include -PathType Leaf) -or -not (Test-Path -LiteralPath $library -PathType Leaf)) {throw 'Varjo Native SDK include/lib missing'}
$output=Join-Path $here 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$command='call "'+$vcvars+'" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"'+(Join-Path $sdk 'include')+'" /Fo"'+(Join-Path $output 'bridge.obj')+'" "'+(Join-Path $here 'bridge.cpp')+'" /link /out:"'+(Join-Path $output 'varjo_gaze_bridge.exe')+'" "'+$library+'"'
& cmd.exe /d /c $command
if($LASTEXITCODE -ne 0){throw 'Varjo bridge build failed'}
Write-Output (Join-Path $output 'varjo_gaze_bridge.exe')
