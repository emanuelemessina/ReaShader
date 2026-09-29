; Extra sections of the Windows installer, included by the CPack Inno Setup generator (see CMakeLists.txt, Package).
; Defines from CMake: VcRedist (path of vc_redist.x64.exe), VcRedistMajor/Minor/Build (its toolset version).

; Upgrades replace the shipped folders, like build.cmake's deploy, so no stale files are left.
; resources\shaders\compiled (uploaded shaders) is kept; uninstall asks about it (see installer.pas).

[InstallDelete]
Type: filesandordirs; Name: "{app}\ui"
Type: filesandordirs; Name: "{app}\resources\images"
Type: filesandordirs; Name: "{app}\resources\meshes"
Type: filesandordirs; Name: "{app}\resources\shaders\examples"

[UninstallDelete]
Type: files; Name: "{app}\rs.log"

; VC++ runtime: installed only when missing or too old (see installer.pas)

[Files]
Source: "{#VcRedist}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: shellexec waituntilterminated; Check: VCRedistNeeded
