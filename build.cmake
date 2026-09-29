# Build script:
# - single entry point for the build loop: configure, build, deploy
# - default profile to debug
# - resolve host preset and build directory
#
# Usage:
#   cmake [-DPROFILE=<debug|release>] -P build.cmake

if(NOT DEFINED PROFILE)
    set(PROFILE debug)
endif()

#################################
# Preset
#################################

if(CMAKE_HOST_WIN32)
    set(PRESET "windows-${PROFILE}")
elseif(CMAKE_HOST_APPLE)
    set(PRESET "macos-${PROFILE}")
else()
    set(PRESET "linux-${PROFILE}")
endif()

message(STATUS "Preset: ${PRESET}")

set(BUILD_PRESET_DIR "${CMAKE_CURRENT_LIST_DIR}/build/${PRESET}")

#################################
# Configure
#################################

# Run configure only on an unconfigured build tree.
# CMake re-runs configuration automatically when project inputs change.

if(NOT EXISTS "${BUILD_PRESET_DIR}/CMakeCache.txt")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --preset ${PRESET}
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESULT_VARIABLE CONFIGURE_RESULT
    )
    if(NOT CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "Configure failed (preset ${PRESET})")
    endif()
endif()

#################################
# Build
#################################

execute_process(
    COMMAND ${CMAKE_COMMAND} --build --preset ${PRESET}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE BUILD_RESULT
)
if(NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Build failed (preset ${PRESET})")
endif()

#################################
# Deploy
#################################

# Resolve user CLAP directory
if(CMAKE_HOST_WIN32)
    set(CLAP_USER_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
elseif(CMAKE_HOST_APPLE)
    set(CLAP_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(CLAP_USER_DIR "$ENV{HOME}/.clap")
endif()

message(STATUS "Deploying to ${CLAP_USER_DIR}")
file(MAKE_DIRECTORY "${CLAP_USER_DIR}")

# Binary:
# REAPER locks the plugin file while loaded.
# Wait for the user to unlock the binary before retrying.

while(TRUE)
    file(COPY_FILE "${BUILD_PRESET_DIR}/ReaShader.clap" "${CLAP_USER_DIR}/ReaShader.clap"
         RESULT COPY_RESULT ONLY_IF_DIFFERENT)
    if(COPY_RESULT EQUAL 0)
        break()
    endif()

    message(NOTICE "\nCan't overwrite ReaShader.clap (${COPY_RESULT}).\n"
                   "Close REAPER (or remove ReaShader from all FX chains), then press Enter to retry. Ctrl+C cancels.")
    # Wait for Enter (fails at end of input, i.e. no interactive terminal)
    if(CMAKE_HOST_WIN32)
        execute_process(COMMAND powershell -NoProfile -Command "if ($null -eq [Console]::In.ReadLine()) { exit 1 }"
                        RESULT_VARIABLE WAIT_RESULT)
    else()
        execute_process(COMMAND sh -c "read _"
                        RESULT_VARIABLE WAIT_RESULT)
    endif()
    if(NOT WAIT_RESULT EQUAL 0)
        message(FATAL_ERROR "Deploy aborted: no input to wait on. Close REAPER and build again.")
    endif()
endwhile()

# Resources

foreach(RESOURCE_DIR assets rsui)
    file(REMOVE_RECURSE "${CLAP_USER_DIR}/${RESOURCE_DIR}")
    file(COPY "${BUILD_PRESET_DIR}/${RESOURCE_DIR}" DESTINATION "${CLAP_USER_DIR}")
endforeach()

message(STATUS "Deployed ReaShader.clap + resources")
