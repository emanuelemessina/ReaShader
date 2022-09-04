######### POST BUILD #########

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

#    STEINBERG DEFAULT

# generate vst module info
& "$($env:msbuild_project_dir)\bin\Debug\moduleinfotool.exe" -create -version 1.0.0.0 -path "$($env:target_path)" -output "$($env:vst3_bundle_out_dir)\moduleinfo.json"
if(-not $?){ end }

# run validator
if(-not $skipValidator){
    Write-Host "[SMTG] Validator started..."
    & "$($env:msbuild_project_dir)\bin\Debug\validator.exe" "$($env:target_path)"
    Write-Host "[SMTG] Validator finished."
}

#   REASHADER  

