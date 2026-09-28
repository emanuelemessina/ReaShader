# Single entry point to build the project.
# Orchestrates configure + build for a given PROFILE (debug/release), 
# handling any OS-specific setup. 
#
# Usage:
#   cmake [-DPROFILE=<debug|release>] -P build.cmake

if(NOT DEFINED PROFILE)
    set(PROFILE debug)
endif()

# Preset

if(CMAKE_HOST_WIN32)
    set(PRESET "windows-${PROFILE}")
elseif(CMAKE_HOST_APPLE)
    set(PRESET "macos-${PROFILE}")
else()
    set(PRESET "linux-${PROFILE}")
endif()

message(STATUS "Preset: ${PRESET}")

# Dirs

set(BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/build")
file(MAKE_DIRECTORY "${BUILD_DIR}")

set(BUILD_PRESET_DIR "${BUILD_DIR}/${PRESET}")
file(MAKE_DIRECTORY "${BUILD_PRESET_DIR}")

# Setup

if(CMAKE_HOST_WIN32)

elseif(CMAKE_HOST_APPLE)

else()

endif()

# Configure

execute_process(
    COMMAND ${CMAKE_COMMAND} --preset ${PRESET}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE CONFIGURE_RESULT
)
if(NOT CONFIGURE_RESULT EQUAL 0)
    message(FATAL_ERROR "Configure failed (preset ${PRESET})")
endif()

# Build

execute_process(
    COMMAND ${CMAKE_COMMAND} --build --preset ${PRESET}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE BUILD_RESULT
)
if(NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Build failed (preset ${PRESET})")
endif()
