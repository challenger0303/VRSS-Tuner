$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$research=Split-Path -Parent $here
. (Join-Path $here 'Find-Vcvars.ps1')
$vcvars=Find-Vcvars64
$fixture=Join-Path $here ('build\steam-launch-test-'+(Get-Date -Format 'yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$wrapper=Join-Path $fixture 'steam_vrchat_launch.exe'
$target=Join-Path $fixture 'start_protected_game.exe'
$steamTarget=Join-Path $fixture 'launch.exe'
$openvr=Join-Path $research 'third_party\openvr'
$line='call "'+$vcvars+'" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /I"'+$research+'" /Fo"'+(Join-Path $fixture 'wrapper.obj')+'" "'+(Join-Path $here 'steam_vrchat_launch.cpp')+'" /link "'+(Join-Path $openvr 'openvr_api.lib')+'" /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /OUT:"'+$wrapper+'"'
& cmd.exe /d /c $line
if($LASTEXITCODE -ne 0){throw 'Steam launch wrapper compilation failed.'}
$line='call "'+$vcvars+'" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /Fo"'+(Join-Path $fixture 'target.obj')+'" "'+(Join-Path $here 'steam_launch_fixture.cpp')+'" /Fe"'+$target+'"'
& cmd.exe /d /c $line
if($LASTEXITCODE -ne 0){throw 'Steam launch target fixture compilation failed.'}
Copy-Item -LiteralPath $target -Destination $steamTarget
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination (Join-Path $fixture 'openvr_api.dll')
$oldLocal=$env:LOCALAPPDATA
$oldOutput=$env:VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT
$oldVendor=$env:TEST_VENDOR_ID
$oldProduct=$env:TEST_PRODUCT_ID
try {
    $env:LOCALAPPDATA=Join-Path $fixture 'local'
    $env:VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT=Join-Path $fixture 'result.txt'
    Remove-Item Env:TEST_VENDOR_ID,Env:TEST_PRODUCT_ID -ErrorAction SilentlyContinue
    $process=Start-Process -FilePath $wrapper -ArgumentList ('"'+$target+'" --profile=0 "quoted value"') -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw "Steam wrapper exited $($process.ExitCode)"}
    $actual=Get-Content -LiteralPath $env:VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT
    $expected=@('vendor=','product=','arg=--profile=0','arg=quoted value')
    if(($actual -join "`n") -ne ($expected -join "`n")){throw "Steam wrapper passthrough mismatch: $($actual -join '; ')"}
    $process=Start-Process -FilePath $wrapper -ArgumentList ('"'+$steamTarget+'" --profile=0 "quoted value"') -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw "Steam launch.exe wrapper exited $($process.ExitCode)"}
    $actual=Get-Content -LiteralPath $env:VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT
    if(($actual -join "`n") -ne ($expected -join "`n")){throw "Steam launch.exe passthrough mismatch: $($actual -join '; ')"}
} finally {
    $env:LOCALAPPDATA=$oldLocal
    $env:VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT=$oldOutput
    $env:TEST_VENDOR_ID=$oldVendor
    $env:TEST_PRODUCT_ID=$oldProduct
}
Write-Output 'STEAM_LAUNCH_BRIDGE_PASS protected_and_steam_launcher quoted_arguments'
