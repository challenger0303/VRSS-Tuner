#ifndef BundleDir
  #error BundleDir must name a verified portable bundle
#endif
#ifndef OutputDir
  #error OutputDir must name the installer output directory
#endif

[Setup]
AppId={{A4C668E3-91A6-4898-AC0B-AEA1A0A6F749}
AppName=VRSS Tuner
AppVersion=0.1.0-beta.5
AppPublisher=challenger0303
AppPublisherURL=https://github.com/challenger0303/VRSS-Tuner
DefaultDirName={localappdata}\Programs\VRSS Tuner
UsePreviousAppDir=no
DisableDirPage=yes
DefaultGroupName=VRSS Tuner
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=VRSS-Tuner-Setup-0.1.0-beta.5
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\VRSS Tuner.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Excludes: "\common-vrss-provider\common_vrss_provider.exe,\common-vrss-provider\openvr_api.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
; Keep the running provider until it exits. Test-Setup verifies the staged
; files against bundle-manifest.json and applies them before its next start.
Source: "{#BundleDir}\common-vrss-provider\common_vrss_provider.exe"; DestDir: "{app}\common-vrss-provider"; Flags: onlyifdoesntexist
Source: "{#BundleDir}\common-vrss-provider\openvr_api.dll"; DestDir: "{app}\common-vrss-provider"; Flags: onlyifdoesntexist
Source: "{#BundleDir}\common-vrss-provider\common_vrss_provider.exe"; DestDir: "{app}\updates"; Flags: ignoreversion
Source: "{#BundleDir}\common-vrss-provider\openvr_api.dll"; DestDir: "{app}\updates"; Flags: ignoreversion
; Existing Steam launch options on upgraded systems point here. Keep this
; compatibility bridge as a harmless passthrough after uninstall.
; VRChat keeps this executable open for its whole session. Preserve an
; existing copy during upgrades so the installer can update Tuner live.
; New installations still receive both files. SteamVR autostart copies the
; newest bundle bridge after VRChat exits.
Source: "{#BundleDir}\steam_vrchat_launch.exe"; DestDir: "{localappdata}\VRSS Tuner\SteamLaunch"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#BundleDir}\openvr_api.dll"; DestDir: "{localappdata}\VRSS Tuner\SteamLaunch"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\VRSS Tuner"; Filename: "{app}\VRSS Tuner.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\VRSS Tuner"; Filename: "{app}\VRSS Tuner.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ExitCode: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  if not Exec(ExpandConstant('{app}\steamvr_autostart.exe'), '--install',
              ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
  begin
    Log('SteamVR registration command could not run.');
    Exit;
  end;
  Log('SteamVR registration exit code: ' + IntToStr(ExitCode));
  if (ExitCode <> 0) and not WizardSilent then
  begin
    if ActiveLanguage = 'ja' then
      MsgBox('アプリはインストールされました。SteamVRの自動起動登録は保留です。SteamからSteamVRを通常起動し、インストール先の Install-SteamVR-Autostart.cmd を一度実行してください。インストーラーはSteamVRを起動しません。', mbInformation, MB_OK)
    else
      MsgBox('The app is installed. SteamVR autostart registration is pending. Start SteamVR normally from Steam, then run Install-SteamVR-Autostart.cmd once from the installation folder. Setup does not launch SteamVR.', mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ExitCode: Integer;
begin
  if CurUninstallStep <> usUninstall then Exit;
  if Exec(ExpandConstant('{app}\steamvr_autostart.exe'), '--remove',
          ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Log('SteamVR unregister exit code: ' + IntToStr(ExitCode));
end;
