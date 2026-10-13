; packaging/windows/installer.iss — Inno Setup script for Desktop Pet.
;
; Built by .github/workflows/release.yml on tag push:
;     iscc /DVERSION=<tag without leading v> /DBUILD_BIN=<repo>/build/bin \
;          packaging/windows/installer.iss
; VERSION defaults to 0.0.0 (manual/local compiles); the CI flow always
; passes the git tag.
;
; DUAL MODE (2026-10 slimming) — one script, two installers, no copy-drift.
; The UPDATE preprocessor switch (iscc /DUPDATE) selects the slim update
; flavor; without it this is the full main installer, byte-for-byte the
; same behavior as before the switch existed:
;   main (default)  — FULL build/bin tree: fonts/ + sample models included,
;                     first install gets everything. Output: DesktopPet-Setup.
;   /DUPDATE        — same tree MINUS fonts\* + Resources\Models\* (~61MB of
;                     first-install payloads that never change between
;                     releases). Output: DesktopPet-Update. AppId / AppName /
;                     DefaultDirName / PrivilegesRequired are IDENTICAL in
;                     both modes: an in-place update over the existing
;                     %LOCALAPPDATA%\DesktopPet install (Inno re-uses the
;                     previous install dir via UsePreviousAppDir) sharing the
;                     same uninstall entry.
;
; PER-USER INSTALL (storage-layout revision 2026-09): installs to
; {localappdata}\DesktopPet with PrivilegesRequired=lowest — NO UAC
; elevation prompt, uninstall key under HKCU, {autoprograms}/{autodesktop}
; resolve to the current user's Start Menu / Desktop. This matches the
; Chrome / VS Code per-user convention and keeps the install tree
; user-writable (voice-pack/model downloads never need elevation).
; Config lives in %APPDATA%\desktop-pet (Roaming), data in
; %LOCALAPPDATA%\desktop-pet (Local) — see controller_qt ConfigDir.
; NOTE: a pre-2.0.0 Program Files install is NOT auto-detected (AppId is
; deliberately unchanged for product identity; ~zero users — acceptable).
;
; Deliberately NO SetupIconFile and no custom shortcut icons — the repo has
; no icon assets yet (known gap, tracked in BUILD.md's 发布 section); the
; system default icon is used until real art lands.
;
; Contents = the whole self-contained build/bin tree (controller exe +
; GL/VK renderers + Live2DCubismCore.dll + Resources/ + fonts/ + shader
; dirs + Qt runtime DLLs deployed by windeployqt POST_BUILD). Test and
; debug artifacts never ship (Excludes below); UPDATE mode additionally
; excludes fonts\* and Resources\Models\* (see FILE_EXCLUDES).

#ifndef VERSION
#define VERSION "0.0.0"
#endif
#ifndef BUILD_BIN
#define BUILD_BIN "..\..\build\bin"
#endif

; [Files] exclude list, parameterized for the two modes so the Source rule
; itself is never duplicated (drift-proof). UPDATE appends the first-install
; payloads. The directory excludes are deliberately belt-and-braces:
;   - a bare name mask ('fonts') matches the directory ENTRY during wildcard
;     expansion — the whole subtree is dropped from the file list;
;   - the '\*' variants match path-relatively against every file below the
;     directory ('*' spans path separators in Inno's mask matcher).
; Together: nothing under fonts\ or Resources\Models\ can ship in UPDATE
; mode, whatever the matcher edge case. ISPP string literals keep '\' raw
; (ISPP escapes use '#', not backslash).
#ifdef UPDATE
#define FILE_EXCLUDES "renderer_tests.exe,desktop-pet-controller-qt-poc.exe,*.pdb,*.lib,fonts,fonts\*,Resources\Models,Resources\Models\*"
#else
#define FILE_EXCLUDES "renderer_tests.exe,desktop-pet-controller-qt-poc.exe,*.pdb,*.lib"
#endif

[Setup]
AppId={{7A3C9E52-4B18-4D6A-9F2E-8C0D5A61B4F3}
AppName=Desktop Pet
AppVersion={#VERSION}
Uninstallable=yes
UninstallDisplayName=Desktop Pet
UninstallDisplayIcon={app}\desktop-pet-controller-qt.exe
; Per-user install (see header): user-writable LocalAppData dir, no UAC.
DefaultDirName={localappdata}\DesktopPet
DefaultGroupName=Desktop Pet
; No elevation prompt — per-user only (Chrome / VS Code convention); the
; uninstall key lands under HKCU and {autoprograms}/{autodesktop} resolve
; to the current user's locations automatically.
PrivilegesRequired=lowest
; Qt 6.10 floor
MinVersion=10.0
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
; CloseApplications IS Inno's default (=yes) — written out explicitly for
; the UPDATE flow: the running pet/panel locks the exes being replaced, and
; Restart Manager (CloseApplicationsFilter default *.exe,*.dll,*.chm)
; prompts the user to close them before files are overwritten. Behavior of
; the main installer is unchanged (same default as before).
CloseApplications=yes
WizardStyle=modern
OutputDir=.
#ifdef UPDATE
; UPDATE flavor: distinct artifact name (DesktopPet-Update-<ver>.exe).
OutputBaseFilename=DesktopPet-Update-{#VERSION}
#else
OutputBaseFilename=DesktopPet-Setup-{#VERSION}
#endif

[Languages]
; Vendored: choco's Inno Setup package does not ship ChineseSimplified.isl
; (it lives in the official repo's Files/Languages/, promoted from the
; unofficial set). Relative path = resolved next to this .iss file.
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything build/bin produces. Excludes: renderer_tests.exe (GoogleTest
; binary), the retired PoC exe (stale local builds only — CI never produces
; it), plus PDB/lib files that can leak in from debug-adjacent tooling.
; UPDATE mode additionally drops fonts\* + Resources\Models\* via the
; FILE_EXCLUDES define above (main install keeps the tree full).
Source: "{#BUILD_BIN}\*"; DestDir: "{app}"; \
    Excludes: "{#FILE_EXCLUDES}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Desktop Pet"; Filename: "{app}\desktop-pet-controller-qt.exe"
Name: "{autodesktop}\Desktop Pet"; Filename: "{app}\desktop-pet-controller-qt.exe"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\desktop-pet-controller-qt.exe"; \
    Description: "{cm:LaunchProgram,Desktop Pet}"; \
    Flags: nowait postinstall skipifsilent
