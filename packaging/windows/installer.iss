; Inno Setup script for Gen VST. Invoked from CI as:
;   iscc /DAppVersion=<version> packaging\windows\installer.iss
;
; Builds an admin-elevated installer that drops:
;   * the VST3 bundle into C:\Program Files\Common Files\VST3
;   * the Standalone .exe into Program Files\Gen VST
;   * factory patches (.tfi / .vgi / .y12 / .psg) into
;     %LOCALAPPDATA%\GenVst\patches  --  matches GENVST_STANDALONE_PATCH_DIR
;     baked at compile time in src/CMakeLists.txt.
;
; v0.1 ships unsigned. Codesigning (SignTool against the .exe AND the installer
; output) is a v0.2 follow-up and would land here as SignTool / SignedUninstaller
; directives plus a CI secret.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

[Setup]
AppId={{A4F3C7E2-9B5D-4E8A-B6C1-D2F5A8E1C9B7}
AppName=Gen VST
AppVersion={#AppVersion}
AppPublisher=Gen VST
AppPublisherURL=https://github.com/binbuf/gen-vst
AppSupportURL=https://github.com/binbuf/gen-vst/issues
DefaultDirName={autopf}\Gen VST
DefaultGroupName=Gen VST
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=..\..\build\windows-release\installer
OutputBaseFilename=Gen-VST-{#AppVersion}-windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Gen VST {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; --- VST3 bundle (a directory tree) -> system VST3 location ------------------
Source: "..\..\build\windows-release\src\GenVst_artefacts\Release\VST3\Gen VST.vst3\*"; \
    DestDir: "{commoncf64}\VST3\Gen VST.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; --- Standalone --------------------------------------------------------------
Source: "..\..\build\windows-release\src\GenVst_artefacts\Release\Standalone\Gen VST.exe"; \
    DestDir: "{app}"; Flags: ignoreversion

; --- Factory patches for the Standalone --------------------------------------
; Standalone reads from GENVST_STANDALONE_PATCH_DIR, hard-coded at compile time
; to %LOCALAPPDATA%\GenVst\patches. Plugin formats find patches inside their
; own .vst3 bundle (POST_BUILD copy in src/CMakeLists.txt), so no install
; rule is needed for VST3 patches here.
Source: "..\..\extern\patches\*.tfi"; DestDir: "{localappdata}\GenVst\patches"; \
    Flags: ignoreversion skipifsourcedoesntexist
Source: "..\..\extern\patches\*.vgi"; DestDir: "{localappdata}\GenVst\patches"; \
    Flags: ignoreversion skipifsourcedoesntexist
Source: "..\..\extern\patches\*.y12"; DestDir: "{localappdata}\GenVst\patches"; \
    Flags: ignoreversion skipifsourcedoesntexist
Source: "..\..\extern\patches\sq\*.psg"; DestDir: "{localappdata}\GenVst\patches\sq"; \
    Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{commonprograms}\Gen VST"; Filename: "{app}\Gen VST.exe"
Name: "{commonprograms}\Uninstall Gen VST"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Gen VST.exe"; Description: "Launch Gen VST"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The VST3 install creates a directory tree under {commoncf64}\VST3\Gen VST.vst3;
; recursesubdirs files are removed by [Files] uninstall, but the now-empty
; directory needs an explicit cleanup.
Type: dirifempty; Name: "{commoncf64}\VST3\Gen VST.vst3"
Type: filesandordirs; Name: "{localappdata}\GenVst\patches"
