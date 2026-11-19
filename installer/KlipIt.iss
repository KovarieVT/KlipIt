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
WizardStyle=modern
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
const
  { Inno colours are BGR }
  ColMidnight = $0D090B;   { #0B090D }
  ColObsidian = $191115;   { #151119 }
  ColCharcoal = $251B20;   { #201B25 }
  ColBone     = $E5E1E9;   { #E9E1E5 }
  ColSilver   = $BFB2B8;   { #B8B2BF }
  ColCrimson  = $482AB5;   { #B52A48 }

var
  ThemePage: TInputOptionWizardPage;


procedure InitializeWizard;
begin
  { Gothic look: obsidian panels, bone-white text, crimson headings }
  WizardForm.Color := ColMidnight;
  WizardForm.MainPanel.Color := ColObsidian;
  WizardForm.InnerPage.Color := ColMidnight;
  WizardForm.WelcomePage.Color := ColMidnight;
  WizardForm.FinishedPage.Color := ColMidnight;
  WizardForm.Bevel.Visible := False;
  WizardForm.Bevel1.Visible := False;
  WizardForm.WelcomeLabel1.Font.Color := ColCrimson;
  WizardForm.WelcomeLabel1.Font.Name := 'Georgia';
  WizardForm.WelcomeLabel2.Font.Color := ColBone;
  WizardForm.FinishedHeadingLabel.Font.Color := ColCrimson;
  WizardForm.FinishedHeadingLabel.Font.Name := 'Georgia';
  WizardForm.FinishedLabel.Font.Color := ColBone;
  WizardForm.PageNameLabel.Font.Color := ColCrimson;
  WizardForm.PageNameLabel.Font.Name := 'Georgia';
  WizardForm.PageDescriptionLabel.Font.Color := ColSilver;
  WizardForm.SelectDirLabel.Font.Color := ColBone;
  WizardForm.SelectDirBrowseLabel.Font.Color := ColBone;
  WizardForm.DiskSpaceLabel.Font.Color := ColSilver;
  WizardForm.DirEdit.Color := ColCharcoal;
  WizardForm.DirEdit.Font.Color := ColBone;
  WizardForm.LicenseLabel1.Font.Color := ColBone;
  WizardForm.LicenseMemo.Color := ColCharcoal;
  WizardForm.LicenseMemo.Font.Color := ColBone;
  WizardForm.LicenseAcceptedRadio.Font.Color := ColBone;
  WizardForm.LicenseNotAcceptedRadio.Font.Color := ColBone;
  WizardForm.SelectTasksLabel.Font.Color := ColBone;
  WizardForm.TasksList.Color := ColMidnight;
  WizardForm.TasksList.Font.Color := ColBone;
  WizardForm.ReadyLabel.Font.Color := ColBone;
  WizardForm.ReadyMemo.Color := ColCharcoal;
  WizardForm.ReadyMemo.Font.Color := ColBone;
  WizardForm.StatusLabel.Font.Color := ColBone;
  WizardForm.FilenameLabel.Font.Color := ColSilver;
  WizardForm.RunList.Color := ColMidnight;
  WizardForm.RunList.Font.Color := ColBone;
  WizardForm.ProgressGauge.Height := ScaleY(16);

  ThemePage := CreateInputOptionPage(wpSelectDir,
    'Choose your theme', 'How KlipIt looks. You can change it any time in Settings.',
    'Pick a colour theme:', True, False);
  ThemePage.Add('Black  (default: midnight black with crimson and silver)');
  ThemePage.Add('Dark grey');
  ThemePage.Add('White');
  ThemePage.Add('Blue');
  ThemePage.Add('Purple');
  ThemePage.SelectedValueIndex := 0;
  ThemePage.Surface.Color := ColMidnight;
  ThemePage.SubCaptionLabel.Font.Color := ColBone;
  ThemePage.CheckListBox.Color := ColMidnight;
  ThemePage.CheckListBox.Font.Color := ColBone;
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
