; ============================================================================
;  KlipIt — Windows installer wizard (Inno Setup 6.3+)
;  Build: iscc installer\KlipIt.iss /DSourceDir=<staged dist folder> /DAppVersion=1.0.0
;  The staged folder comes from tools\package_windows.ps1:
;     bin\64bit\KlipIt.exe (+ Qt, engine and Visual C++ runtime DLLs)
;     obs-plugins\64bit\*.dll, data\libobs, data\obs-plugins\...
; ============================================================================

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\KlipIt"
#endif
#define AppName "KlipIt"
#define AppExe  "KlipIt.exe"

[Setup]
; Never change AppId: it is how upgrades find the existing installation.
AppId={{3C8E5B71-9A2D-4F64-B1C7-5E0A9D4F2B86}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
; Installs just for you by default (no administrator prompt); "for all users" is offered too.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
OutputDir=..\dist
OutputBaseFilename=KlipIt-{#AppVersion}-Setup
SetupIconFile=assets\setup.ico
UninstallDisplayIcon={app}\bin\64bit\{#AppExe}
UninstallDisplayName={#AppName}
; Forced dark mode (Inno Setup 6.6+): every control, including checkboxes, radio buttons and
; text boxes, is drawn with light text on a dark background.
WizardStyle=modern dark hidebevels
WizardBackColor=#0B090D
WizardSizePercent=115
WizardImageFile=assets\wizard_side_100.bmp,assets\wizard_side_125.bmp,assets\wizard_side_150.bmp,assets\wizard_side_200.bmp
WizardSmallImageFile=assets\wizard_small_100.bmp,assets\wizard_small_125.bmp,assets\wizard_small_150.bmp,assets\wizard_small_200.bmp
WizardImageStretch=no
LicenseFile=..\resources\LICENSE.txt
Compression=lzma2/ultra64
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
en.WelcomeLabel1=Welcome to KlipIt
en.WelcomeLabel2=Enter your own world of gaming memories.%n%nKlipIt quietly keeps your last moments in every game and saves them with one key. This wizard installs [name/ver] on your computer.
en.FinishedHeadingLabel=Your clipping studio awaits.
en.FinishedLabel=KlipIt is installed. Start a game, press F8 after a great play, and your clip is saved.
en.SelectDirLabel3=Choose where KlipIt is installed. Your clips are saved in your Videos folder and your settings in %APPDATA%\KlipIt, so they are kept when you update or reinstall.
en.ButtonInstall=&Install

[Tasks]
Name: "desktopicon";   Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "startmenuicon"; Description: "Create a &Start menu shortcut"; GroupDescription: "Shortcuts:"
Name: "startup";       Description: "Start KlipIt when Windows starts (it waits quietly in the tray so it's ready when you play)"; GroupDescription: "Background recording:"

[Files]
Source: "{#SourceDir}\bin\*";         DestDir: "{app}\bin";         Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\obs-plugins\*"; DestDir: "{app}\obs-plugins"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\data\*";        DestDir: "{app}\data";        Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE";                 DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\LICENSE.txt";   DestDir: "{app}"; DestName: "NOTICE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: startmenuicon
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "KlipIt"; \
  ValueData: """{app}\bin\64bit\{#AppExe}"" --tray"; Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "{app}\bin\64bit\{#AppExe}"; Description: "Launch KlipIt"; WorkingDir: "{app}\bin\64bit"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM {#AppExe} /F"; RunOnceId: "StopKlipIt"; Flags: runhidden skipifdoesntexist

[UninstallDelete]
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\obs-plugins"
Type: filesandordirs; Name: "{app}\data"

[Code]
var
  ThemePage: TInputOptionWizardPage;

{ All wizard text in white on the obsidian background }
procedure WhiteText;
begin
  WizardForm.WelcomeLabel1.Font.Color := clWhite;
  WizardForm.WelcomeLabel1.Font.Name := 'Georgia';
  WizardForm.WelcomeLabel2.Font.Color := clWhite;
  WizardForm.FinishedHeadingLabel.Font.Color := clWhite;
  WizardForm.FinishedHeadingLabel.Font.Name := 'Georgia';
  WizardForm.FinishedLabel.Font.Color := clWhite;
  WizardForm.PageNameLabel.Font.Color := clWhite;
  WizardForm.PageNameLabel.Font.Name := 'Georgia';
  WizardForm.PageDescriptionLabel.Font.Color := clWhite;
  WizardForm.SelectDirLabel.Font.Color := clWhite;
  WizardForm.SelectDirBrowseLabel.Font.Color := clWhite;
  WizardForm.DiskSpaceLabel.Font.Color := clWhite;
  WizardForm.DirEdit.Font.Color := clWhite;
  WizardForm.LicenseLabel1.Font.Color := clWhite;
  WizardForm.LicenseMemo.Font.Color := clWhite;
  WizardForm.LicenseAcceptedRadio.Font.Color := clWhite;
  WizardForm.LicenseNotAcceptedRadio.Font.Color := clWhite;
  WizardForm.SelectTasksLabel.Font.Color := clWhite;
  WizardForm.TasksList.Font.Color := clWhite;
  WizardForm.ReadyLabel.Font.Color := clWhite;
  WizardForm.ReadyMemo.Font.Color := clWhite;
  WizardForm.PreparingLabel.Font.Color := clWhite;
  WizardForm.StatusLabel.Font.Color := clWhite;
  WizardForm.FilenameLabel.Font.Color := clWhite;
  WizardForm.RunList.Font.Color := clWhite;
end;

procedure InitializeWizard;
begin
  ThemePage := CreateInputOptionPage(wpSelectDir,
    'Choose your theme', 'How KlipIt looks. You can change it any time in Settings.',
    'Pick a colour theme:', True, False);
  ThemePage.Add('Black  (default: midnight black with crimson and silver)');
  ThemePage.Add('Dark grey');
  ThemePage.Add('White');
  ThemePage.Add('Blue');
  ThemePage.Add('Purple');
  ThemePage.SelectedValueIndex := 0;
  ThemePage.SubCaptionLabel.Font.Color := clWhite;
  ThemePage.CheckListBox.Font.Color := clWhite;
  WhiteText;
end;

function ThemeId: String;
begin
  case ThemePage.SelectedValueIndex of
    1: Result := 'darkgrey';
    2: Result := 'white';
    3: Result := 'blue';
    4: Result := 'purple';
  else
    Result := 'black';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir, Autostart: String;
begin
  if CurStep = ssPostInstall then begin
    { KlipIt reads this once on its first start, then deletes it }
    Dir := ExpandConstant('{userappdata}\KlipIt');
    ForceDirectories(Dir);
    if WizardIsTaskSelected('startup') then Autostart := 'true' else Autostart := 'false';
    SaveStringToFile(Dir + '\installer.json', '{"theme": "' + ThemeId + '", "autostart": ' + Autostart + '}', False);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    if MsgBox('Also remove your KlipIt settings and logs?'#13#10#13#10 +
              '(Your clips, recordings and screenshots are never deleted.)', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{userappdata}\KlipIt'), True, True, True);
  end;
end;
