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

# compile shaders
cd "$($env:project_root_dir)\source\shaders"

glslc -fshader-stage=vert vert.glsl -o vert.spv
if(-not $?){ end }
glslc -fshader-stage=frag frag.glsl -o frag.spv
if(-not $?){ end }
log "Shaders compiled"