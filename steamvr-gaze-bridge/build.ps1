$ErrorActionPreference='Stop'
$here=$PSScriptRoot
. (Join-Path $here '..\vrss-tuner\Find-Vcvars.ps1')
$vcvars=Find-Vcvars64
$openvr=Join-Path $here '..\third_party\openvr'
$out=Join-Path $here 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$command='call "'+$vcvars+'" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"'+(Join-Path $here '..')+'" /Fo"'+(Join-Path $out 'bridge.obj')+'" "'+(Join-Path $here 'bridge.cpp')+'" /link /out:"'+(Join-Path $out 'steamvr_gaze_bridge.exe')+'" "'+(Join-Path $openvr 'openvr_api.lib')+'"'
& cmd.exe /d /c $command
if($LASTEXITCODE -ne 0){throw 'SteamVR gaze bridge build failed'}
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination $out -Force
Copy-Item -LiteralPath (Join-Path $here '..\steamvr-gaze-probe\eye_actions.json') -Destination $out -Force
Write-Output (Join-Path $out 'steamvr_gaze_bridge.exe')
