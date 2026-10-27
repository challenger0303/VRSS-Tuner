param([string]$OutputPath = '', [switch]$StaticRuntime, [switch]$OfflineUiTest)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$research = Split-Path -Parent $here
$nvapi = Join-Path $research 'third_party\nvapi'
$pimaxSdk = 'C:\Program Files\Pimax\Sdk\Include'
. (Join-Path $here 'Find-Vcvars.ps1')
$vcvars = Find-Vcvars64
$output = if ($OutputPath) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath) } else { Join-Path $here 'VRSS Tuner.exe' }
$build = Join-Path $here 'build'

if (-not (Test-Path -LiteralPath $vcvars)) { throw "Visual Studio C++ tools not found: $vcvars" }
if (-not (Test-Path -LiteralPath (Join-Path $nvapi 'amd64\nvapi64.lib'))) { throw 'NVAPI library not found' }
New-Item -ItemType Directory -Path $build -Force | Out-Null

$runtime = if ($StaticRuntime) { '/MT' } else { '/MD' }
$pimaxCompile = if (Test-Path -LiteralPath (Join-Path $pimaxSdk 'PVR_API.h')) { ' /DVRSS_TUNER_PVR_SDK /I"' + $pimaxSdk + '"' } else { '' }
$testDefine = if ($OfflineUiTest) { ' /DVRSS_OFFLINE_UI_TEST' } else { '' }
$providerObject = Join-Path $build 'provider.embedded.obj'
$providerLine = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 ' + $runtime +
    ' /DVRSS_PROVIDER_EMBEDDED /I"' + $nvapi + '" /c "' + (Join-Path $research 'common-vrss-provider\provider.cpp') + '" /Fo"' + $providerObject + '"'
& cmd.exe /d /c $providerLine
if ($LASTEXITCODE -ne 0) { throw "Embedded provider build failed with code $LASTEXITCODE" }
$openvr = Join-Path $research 'third_party\openvr'
$line = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 ' + $runtime + $testDefine + $pimaxCompile + ' /DUNICODE /D_UNICODE /I"' + $nvapi + '" /Fo"' + (Join-Path $build 'VrssTuner.obj') + '" /Fd"' + (Join-Path $build 'VrssTuner.pdb') + '" "' + (Join-Path $here 'VrssTuner.cpp') + '" "' + $providerObject + '" /link "' + (Join-Path $openvr 'openvr_api.lib') + '" /SUBSYSTEM:WINDOWS /LIBPATH:"' + (Join-Path $nvapi 'amd64') + '" /OUT:"' + $output + '" /PDB:"' + (Join-Path $build 'VRSS Tuner.pdb') + '"'
& cmd.exe /d /c $line
if ($LASTEXITCODE -ne 0) { throw "C++ build failed with code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $openvr 'openvr_api.dll') -Destination (Join-Path (Split-Path -Parent $output) 'openvr_api.dll')
Get-Item -LiteralPath $output | Select-Object FullName, Length, LastWriteTime
