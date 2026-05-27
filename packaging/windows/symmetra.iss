[Setup]
AppName=Symmetra
AppVersion=1.0.0
AppPublisher=Symmetra Contributors
AppPublisherURL=https://symmetra.dev
AppSupportURL=https://github.com/JvanDyk/Symmetra/issues
AppUpdatesURL=https://github.com/JvanDyk/Symmetra/releases
DefaultDirName={localappdata}\.symmetra
DefaultGroupName=Symmetra
DisableProgramGroupPage=yes
OutputDir=..\..\release-packages
OutputBaseFilename=symmetra-1.0.0-windows-x64-setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64os
ArchitecturesAllowed=x64os
PrivilegesRequired=lowest
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\engine\build\Release\sym.exe"; DestDir: "{app}\bin"; Flags: ignoreversion

[Registry]
; Add to user PATH
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}\bin"; \
  Check: not IsInPath('{app}\bin')

[Code]
function IsInPath(const Dir: string): Boolean;
var
  Path: string;
begin
  RegQueryStringValue(HKCU, 'Environment', 'Path', Path);
  Result := Pos(LowerCase(ExpandConstant(Dir)), LowerCase(Path)) > 0;
end;

[Run]
Filename: "cmd.exe"; Parameters: "/c echo Symmetra installed. Open a new terminal and run: sym --version"; \
  Flags: runhidden

[UninstallDelete]
Type: dirifempty; Name: "{app}\bin"
Type: dirifempty; Name: "{app}"
