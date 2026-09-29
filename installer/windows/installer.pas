// -------- VC++ runtime --------

// VC++ 2015-2022 x64 runtime (MSVCP140.dll, VCRUNTIME140.dll):
// - the plugin needs at least the version of the MSVC toolset it was built with
// - missing or older: the bundled vc_redist.x64.exe is installed (it asks for elevation itself)

function VCRedistNeeded: Boolean;
var
  Key: String;
  Installed, Major, Minor, Build: Cardinal;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(HKLM64, Key, 'Major', Major) and
     RegQueryDWordValue(HKLM64, Key, 'Minor', Minor) and
     RegQueryDWordValue(HKLM64, Key, 'Bld', Build) then
    Result := (Major < {#VcRedistMajor}) or
              ((Major = {#VcRedistMajor}) and
               ((Minor < {#VcRedistMinor}) or ((Minor = {#VcRedistMinor}) and (Build < {#VcRedistBuild}))))
  else
    Result := True;
end;

// -------- wizard --------

// CPack always adds a "desktop icon" task; a plugin has nothing to link, so the tasks page is skipped
// (the task stays unchecked and has no [Icons] entry)

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := PageID = wpSelectTasks;
end;

// -------- uninstall --------

// Uploaded shaders (resources\shaders\compiled) are the user's: ask before deleting them.
// A silent uninstall keeps them. The folders left empty are removed.

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App, Compiled: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  App := ExpandConstant('{app}');
  Compiled := App + '\resources\shaders\compiled';

  if DirExists(Compiled) and
     (SuppressibleMsgBox('Also delete your uploaded shaders?' + #13#10#13#10 + Compiled,
                         mbConfirmation, MB_YESNO, IDNO) = IDYES) then
    DelTree(Compiled, True, True, True);

  RemoveDir(App + '\resources\shaders');
  RemoveDir(App + '\resources');
  RemoveDir(App);
end;
