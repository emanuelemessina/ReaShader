######## PRE-LINK SCRIPT #########

function log {
    param($msg)

    Write-Host "[ReaShader] $($msg)"
}


function end {
    log "[ERROR] Exited with $($lastexitcode)"
    Exit
}

#   STEINBERG DEFAULT PART

# create resources directory
cmake -E make_directory "$($env:vst3_resources_out_dir)"
if(-not $?){ end }

# copy uidesc file
cmake -E copy_if_different "$($env:project_root_dir)\resource\myplugineditor.uidesc" "$($env:vst3_resources_out_dir)"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/myplugineditor.uidesc to $($env:vst3_resources_out_dir)"
if(-not $?){ end }

# copy VST logos
cmake -E make_directory "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E copy_if_different "$($env:project_root_dir)\resource\E0F1D00DC00B511782AD083BE3690071_snapshot.png" "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png to $($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }

cmake -E make_directory "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E copy_if_different  "$($env:project_root_dir)\resource\E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png" "$($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }
cmake -E echo "[SMTG] Copied resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png to $($env:vst3_resources_out_dir)\Snapshots"
if(-not $?){ end }

#   REASHADER

# compile shaders
cd "$($env:project_root_dir)\source\shaders"

glslc -fshader-stage=vert vert.glsl -o vert.spv
if(-not $?){ end }
glslc -fshader-stage=frag frag.glsl -o frag.spv
if(-not $?){ end }
glslc -fshader-stage=vert pp_vert.glsl -o pp_vert.spv
if(-not $?){ end }
glslc -fshader-stage=frag pp_frag.glsl -o pp_frag.spv
if(-not $?){ end }
log "Shaders compiled"

# copy resources
cmake -E copy_directory "$($env:project_root_dir)\source\shaders" "$($env:assets_out_dir)\shaders"
if(-not $?){ end }
log "Copied shaders"

cmake -E copy_directory "$($env:project_root_dir)\resource\images" "$($env:assets_out_dir)\images"
if(-not $?){ end }
log "Copied images"

cmake -E copy_directory "$($env:project_root_dir)\resource\meshes" "$($env:assets_out_dir)\meshes"
if(-not $?){ end }
log "Copied meshes"
