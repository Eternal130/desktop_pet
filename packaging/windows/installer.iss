; packaging/windows/installer.iss — Inno Setup script for Desktop Pet.
;
; Built by .github/workflows/release.yml on tag push:
;     iscc /DVERSION=<tag without leading v> /DBUILD_BIN=<repo>/build/bin \
;          packaging/windows/installer.iss
; VERSION defaults to 0.0.0 (manual/local compiles); the CI flow always
; passes the git tag.
;
; Deliberately NO SetupIconFile and no custom shortcut icons — the repo has
; no icon assets yet (known gap, tracked in BUILD.md's 发布 section); the
; system default icon is used until real art lands.
;
; Contents = the whole self-contained build/bin tree (controller exe +
; GL/VK renderers + Live2DCubismCore.dll + Resources/ + fonts/ + shader
; dirs + Qt runtime DLLs deployed by windeployqt POST_BUILD). Test and
; debug artifacts never ship (Excludes below).

#ifndef VERSION
#define VERSION "0.0.0"
#endif
#ifndef BUILD_BIN
#define BUILD_BIN "..\..\build\bin"
#endif

[Setup]
AppId={{7A3C9E52-4B18-4D6A-9F2E-8C0D5A61B4F3}
AppName=Desktop Pet
AppVersion={#VERSION}
Uninstallable=yes
UninstallDisplayName=Desktop Pet
UninstallDisplayIcon={app}\desktop-pet-controller-qt.exe
DefaultDirName={autopf}\DesktopPet
DefaultGroupName=Desktop Pet
; Qt 6.10 floor
MinVersion=10.0
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
OutputDir=.
OutputBaseFilename=DesktopPet-Setup-{#VERSION}

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything build/bin produces. Excludes: renderer_tests.exe (GoogleTest
; binary), the retired PoC exe (stale local builds only — CI never produces
; it), plus PDB/lib files that can leak in from debug-adjacent tooling.
Source: "{#BUILD_BIN}\*"; DestDir: "{app}"; \
    Excludes: "renderer_tests.exe,desktop-pet-controller-qt-poc.exe,*.pdb,*.lib"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Desktop Pet"; Filename: "{app}\desktop-pet-controller-qt.exe"
Name: "{autodesktop}\Desktop Pet"; Filename: "{app}\desktop-pet-controller-qt.exe"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\desktop-pet-controller-qt.exe"; \
    Description: "{cm:LaunchProgram,Desktop Pet}"; \
    Flags: nowait postinstall skipifsilent
