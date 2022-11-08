######### POST BUILD #########

using namespace System.Collections.Generic

param (
    [switch]$skipValidator = $false
)

function log {
    param($msg)

    Write-Host "[ReaShader] $($msg)"
}

function end {
    log "[ERROR] Exited with $($lastexitcode)"
    Exit
}

# get project version from .version file (cmake-git-versioning)

$str = Get-Content -Path "$($env:project_root_dir)\.version";

$m = [regex]::matches($str , '\*(\d)');
$groups = [List[PSObject]]::new();
foreach ($match in $m){
    $groups.Add($match.Groups[1])
}

$major = $groups[0].value;
$minor = $groups[1].value;
$patch = $groups[2].value;
$build = $groups[4].value;

$version_string = "$($major).$($minor).$($patch).$($build)";

#    STEINBERG DEFAULT

# generate vst module info
& "$($env:msbuild_project_dir)\bin\Debug\moduleinfotool.exe" -create -version $version_string -path "$($env:target_path)" -output "$($env:vst3_bundle_out_dir)\moduleinfo.json"
if(-not $?){ end }

# run validator
if(-not $skipValidator){
    Write-Host "[SMTG] Validator started..."
    & "$($env:msbuild_project_dir)\bin\Debug\validator.exe" "$($env:target_path)"
    Write-Host "[SMTG] Validator finished."
}

#   REASHADER  

# copy out dir to system vst3 default dir (debug/release will overwrite each other)

[System.IO.Directory]::CreateDirectory("C:\Program Files\Common Files\VST3\$($env:project_name)") # create recursive folders if not present
Remove-Item -LiteralPath "C:\Program Files\Common Files\VST3\$($env:project_name)" -Force -Recurse # delete old build
Copy-Item "$($env:vst3_bundle_out_dir)" -Destination "C:\Program Files\Common Files\VST3" -Recurse # copy new build
if(-not $?){ end }
log "Copied build $($env:vst3_bundle_out_dir) to VST3 default directory ( C:\Program Files\Common Files\VST3 )"