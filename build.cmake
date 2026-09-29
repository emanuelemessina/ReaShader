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

# Resolve the plugin's folder in the user CLAP directory

if(CMAKE_HOST_WIN32)
    set(CLAP_USER_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
elseif(CMAKE_HOST_APPLE)
    set(CLAP_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(CLAP_USER_DIR "$ENV{HOME}/.clap")
endif()

# The plugin's file name (ReaShader or ReaShader-Debug) comes from the build tree's cache (see CMakeLists.txt)

load_cache("${BUILD_PRESET_DIR}" READ_WITH_PREFIX "" PLUGIN_FILE_NAME)

set(DEPLOY_DIR "${CLAP_USER_DIR}/${PLUGIN_FILE_NAME}")
set(PLUGIN_FILE "${PLUGIN_FILE_NAME}.clap")

message(STATUS "Deploying to ${DEPLOY_DIR}")
file(MAKE_DIRECTORY "${DEPLOY_DIR}")

# Binary:
# if REAPER is locking the plugin file, skip the deploy with a warning

file(COPY_FILE "${BUILD_PRESET_DIR}/${PLUGIN_FILE}" "${DEPLOY_DIR}/${PLUGIN_FILE}"
     RESULT COPY_RESULT ONLY_IF_DIFFERENT)
if(NOT COPY_RESULT EQUAL 0)
    message(WARNING "Not deployed: can't overwrite ${PLUGIN_FILE} (${COPY_RESULT}).\n"
                    "Close REAPER (or remove ${PLUGIN_FILE_NAME} from all FX chains) and build again to deploy.")
    return()
endif()

# Resources:
# - replaced folder by folder
# - resources/shaders/compiled (uploaded shaders) is kept

foreach(RESOURCE_DIR resources/images resources/meshes resources/shaders/examples ui)
    file(REMOVE_RECURSE "${DEPLOY_DIR}/${RESOURCE_DIR}")
    file(COPY "${BUILD_PRESET_DIR}/${RESOURCE_DIR}/" DESTINATION "${DEPLOY_DIR}/${RESOURCE_DIR}")
endforeach()

message(STATUS "Deployed ${PLUGIN_FILE} + resources")
