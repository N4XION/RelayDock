; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2026 RelayDock contributors
;
; Inno Setup script for the RelayDock installer. Inno Setup is free: https://jrsoftware.org
;
; What the installer does
;   - Copies the plugin to %ProgramData%\obs-studio\plugins\relaydock, where OBS Studio looks
;     for plugins. Nothing else on the PC changes.
;   - Runs without administrator rights. It asks for them only when Windows does not let the
;     current account write to that folder.
;   - Warns when it finds no OBS Studio, or one that is too old.
;   - Refuses to copy files while OBS Studio runs, because OBS holds the plugin file open.
;   - On uninstall, removes the plugin files. It removes settings and saved stream keys only
;     when the user says so.
;
; Command line switches of its own, next to the ones every Inno Setup installer has:
;   /OBSDIR="folder"   Setup: look for OBS Studio in this folder instead of asking the registry.
;                      It only decides which warning Setup shows. The plugin folder stays the same.
;   /REMOVEDATA=1      Uninstall: also remove settings and saved stream keys, without asking.
;
; tests\integration\Test-Installer.ps1 runs Setup and the uninstaller and checks what they did.
;
; scripts\package.ps1 passes the values below on the ISCC command line.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\release\stage\relaydock"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif
#ifndef OutputBaseName
  #define OutputBaseName "RelayDock-Setup"
#endif
#ifndef ObsMinimumVersion
  #define ObsMinimumVersion "32.0.0"
#endif

[Setup]
; Identifies RelayDock to Windows for upgrades and uninstall. Never change it.
AppId={{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}
AppName=RelayDock
AppVersion={#AppVersion}
AppVerName=RelayDock {#AppVersion}
AppPublisher=RelayDock contributors
AppComments=Multistream plugin for OBS Studio
DefaultDirName={code:PluginDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName=RelayDock (OBS Studio plugin)
VersionInfoVersion={#AppVersionNumeric}.0
VersionInfoProductVersion={#AppVersionNumeric}.0
VersionInfoDescription=RelayDock Setup
VersionInfoCopyright=Copyright (C) 2026 RelayDock contributors
; The date in the file comes from the build, not from the moment Setup was compiled.
TimeStampsInUTC=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[CustomMessages]
NotX64=RelayDock needs 64-bit Windows.
ObsMissing=Setup found no OBS Studio on this PC.%n%nRelayDock is a plugin for OBS Studio {#ObsMinimumVersion} or newer. Install OBS Studio first, from obsproject.com.%n%nInstall RelayDock anyway?
ObsTooOld=Setup found OBS Studio %1.%n%nRelayDock needs OBS Studio {#ObsMinimumVersion} or newer and will not load in this version. Update OBS Studio first.%n%nInstall RelayDock anyway?
ObsRunning=OBS Studio is running.%n%nClose OBS Studio, then choose Retry.
NeedAdmin=Windows does not let your account write to%n%1%n%nSetup needs administrator rights for this folder. Choose OK and Windows asks you for them.
ElevationFailed=Setup did not get administrator rights and cannot continue.
RemoveData=Also remove your RelayDock settings and your saved stream keys?%n%nChoose No to keep them for a later install.

[Messages]
FinishedLabelNoIcons=Setup installed RelayDock.%n%nStart OBS Studio and open Docks, RelayDock.

[Code]
const
  ObsRegKey = 'SOFTWARE\OBS Studio';

// Where OBS Studio looks for plugins. The "common" constants of Inno Setup point at the
// user's own folders when Setup runs without administrator rights, so read the variable.
function PluginDir(Param: String): String;
begin
  Result := GetEnv('ProgramData');
  if Result = '' then
    Result := ExpandConstant('{sd}\ProgramData');
  Result := Result + '\obs-studio\plugins\relaydock';
end;

function ObsInstallDir(): String;
begin
  Result := ExpandConstant('{param:OBSDIR|}');
  if Result <> '' then
    Exit;
  if not RegQueryStringValue(HKLM64, ObsRegKey, '', Result) then
    RegQueryStringValue(HKLM32, ObsRegKey, '', Result);
end;

// True when the version text "a.b.c.d" is at least major.minor of the minimum.
function VersionAtLeast(Version, Minimum: String): Boolean;
var
  V, M: array [0..1] of Integer;
  I, P: Integer;
  Part: String;
begin
  for I := 0 to 1 do
  begin
    P := Pos('.', Version);
    if P > 0 then begin Part := Copy(Version, 1, P - 1); Version := Copy(Version, P + 1, Length(Version)); end
    else begin Part := Version; Version := ''; end;
    V[I] := StrToIntDef(Part, 0);
    P := Pos('.', Minimum);
    if P > 0 then begin Part := Copy(Minimum, 1, P - 1); Minimum := Copy(Minimum, P + 1, Length(Minimum)); end
    else begin Part := Minimum; Minimum := ''; end;
    M[I] := StrToIntDef(Part, 0);
  end;
  Result := (V[0] > M[0]) or ((V[0] = M[0]) and (V[1] >= M[1]));
end;

function ObsIsRunning(): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = "obs64.exe"');
    Result := Processes.Count > 0;
  except
    // Without WMI, Setup cannot tell. Copying then fails with a clear message if the file is in use.
    Result := False;
  end;
end;

// Tries to create the plugin folder and a file in it.
function CanWriteTo(Dir: String): Boolean;
var
  Probe: String;
begin
  Result := False;
  if not ForceDirectories(Dir) then
    Exit;
  Probe := Dir + '\.relaydock-setup-probe';
  if SaveStringToFile(Probe, 'probe', False) then
  begin
    DeleteFile(Probe);
    Result := True;
  end;
end;

function InitializeSetup(): Boolean;
var
  ObsDir, ObsVersion, Target, Params: String;
  I, ErrorCode: Integer;
begin
  Result := True;

  if not IsX64Compatible then
  begin
    SuppressibleMsgBox(CustomMessage('NotX64'), mbCriticalError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;

  ObsDir := ObsInstallDir();
  if (ObsDir = '') or not FileExists(ObsDir + '\bin\64bit\obs64.exe') then
  begin
    Log('RelayDock Setup: no OBS Studio found.');
    if SuppressibleMsgBox(CustomMessage('ObsMissing'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDYES) <> IDYES then
    begin
      Result := False;
      Exit;
    end;
  end
  else
  begin
    if not GetVersionNumbersString(ObsDir + '\bin\64bit\obs64.exe', ObsVersion) then
      ObsVersion := '';
    Log('RelayDock Setup: found OBS Studio ' + ObsVersion + ' in ' + ObsDir);
    if (ObsVersion <> '') and not VersionAtLeast(ObsVersion, '{#ObsMinimumVersion}') then
    begin
      if SuppressibleMsgBox(FmtMessage(CustomMessage('ObsTooOld'), [ObsVersion]), mbConfirmation,
                            MB_YESNO or MB_DEFBUTTON2, IDYES) <> IDYES then
      begin
        Result := False;
        Exit;
      end;
    end;
  end;

  // Administrator rights only when the folder really needs them.
  Target := ExpandConstant('{param:DIR|' + PluginDir('') + '}');
  Log('RelayDock Setup: plugin folder ' + Target);
  if not CanWriteTo(Target) then
  begin
    Log('RelayDock Setup: this account cannot write to the plugin folder.');
    if IsAdmin then
      Exit; // Already elevated. Let the file copy report the real error.
    SuppressibleMsgBox(FmtMessage(CustomMessage('NeedAdmin'), [Target]), mbInformation, MB_OK, IDOK);
    Params := '/ALLUSERS';
    for I := 1 to ParamCount do
      Params := Params + ' "' + ParamStr(I) + '"';
    if ShellExec('runas', ExpandConstant('{srcexe}'), Params, '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
      Result := False // The elevated copy of Setup takes over.
    else
    begin
      SuppressibleMsgBox(CustomMessage('ElevationFailed'), mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  while ObsIsRunning() do
  begin
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := 'OBS Studio is running. Close it and run Setup again.';
      Exit;
    end;
  end;
end;

// Deletes the Windows Credential Manager entries RelayDock created. Their names start with
// "RelayDock:". cmdkey is part of Windows.
procedure RemoveSavedKeys();
var
  ListFile, Line, Target: String;
  Lines: TArrayOfString;
  I, P, ResultCode: Integer;
begin
  ListFile := ExpandConstant('{tmp}\relaydock-credentials.txt');
  if not Exec(ExpandConstant('{cmd}'), '/C cmdkey.exe /list > "' + ListFile + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;
  if not LoadStringsFromFile(ListFile, Lines) then
    Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Lines[I];
    P := Pos('target=RelayDock:', Line);
    if P > 0 then
    begin
      Target := Trim(Copy(Line, P + Length('target='), Length(Line)));
      Exec(ExpandConstant('{sys}\cmdkey.exe'), '/delete:"' + Target + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    end;
  end;
  DeleteFile(ListFile);
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while ObsIsRunning() do
  begin
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RemoveData: Boolean;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // Keeping the data is the default, also for a silent uninstall. /REMOVEDATA=1 removes it
  // without asking.
  if ExpandConstant('{param:REMOVEDATA|0}') = '1' then
    RemoveData := True
  else
    RemoveData := SuppressibleMsgBox(CustomMessage('RemoveData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
  if RemoveData then
  begin
    Log('RelayDock uninstall: removing settings and saved stream keys.');
    DelTree(ExpandConstant('{userappdata}\obs-studio\plugin_config\relaydock'), True, True, True);
    RemoveSavedKeys();
  end
  else
    Log('RelayDock uninstall: settings and saved stream keys stay.');
end;
