##################
# CMake Preamble
##################

cmake_minimum_required(VERSION 3.21)

# Git

find_package(Git)
if(GIT_FOUND)
    message("Git found: ${GIT_EXECUTABLE}")
else()
    message(FATAL_ERROR "Git not found!")
endif()

# Dirs

set(BUILD_DEPS_DIR "${BUILD_DIR}/deps")
file(MAKE_DIRECTORY "${BUILD_DEPS_DIR}")

# Build Deps

# cmake-git-versioning

set(CGV_PATH "${BUILD_DEPS_DIR}/cmake-git-versioning")

if(NOT EXISTS "${CGV_PATH}/cmake-git-versioning.cmake")
    message(STATUS "cmake-git-versioning not found at ${CGV_PATH} -- cloning it there...")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1
                https://github.com/emanuelemessina/cmake-git-versioning.git
                "${CGV_PATH}"
        RESULT_VARIABLE RS_CGV_CLONE_RESULT
    )
    if(NOT RS_CGV_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone cmake-git-versioning into ${CGV_PATH} "
                             "(pass -DRS_CGV_PATH=<path> to point at an existing checkout instead)")
    endif()
endif()

include("${CGV_PATH}/cmake-git-versioning.cmake")

##################
# Project
##################

set(PROJECT_NAME ReaShader)

project(${PROJECT_NAME}
    VERSION ${PROJECT_VERSION_CMAKE}
    DESCRIPTION "${PROJECT_NAME} CLAP Plug-in"
    LANGUAGES CXX
)

##################
# C++
##################

# C++ STANDARD

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ADDITIONAL INCLUDE DIRECTORIES (absolute or relative to this root)
#
# Phase A (CLAP build-system migration) note: reashader_clap.cpp only needs
# external/clap + the REAPER SDK headers. Everything below this point (restinio,
# cwalk, json, boxer, the Vulkan-ecosystem paths) is dead code until Phase B ports
# ReaShaderProcessor/ReaShaderRenderer/rsparams/rsui onto this shell -- left wired
# up here, unchanged, so Phase B doesn't have to re-discover this setup.

if(DEFINED $ENV{VK_SDK_PATH}) # env var
    message(FATAL_ERROR "Vulkan SDK is not installed!")
endif()

set(RS_GLM_PATH "$ENV{VK_SDK_PATH}/../glm" CACHE PATH "GLM path")
set(RS_VMA_PATH "$ENV{VK_SDK_PATH}/../VulkanMemoryAllocator" CACHE PATH "Vulkan Memory Allocator path (contains include and build)")
set(RS_TOL_PATH "$ENV{VK_SDK_PATH}/../tinyobjloader" CACHE PATH "Tiny Obj Loader path")
set(RS_STB_PATH "$ENV{VK_SDK_PATH}/../stb" CACHE PATH "STB path")
set(RS_GLSLANG_PATH "$ENV{VK_SDK_PATH}/../glslang" CACHE PATH "glslang path")
set(RS_SPVC_PATH "$ENV{VK_SDK_PATH}/../SPIRV-Cross" CACHE PATH "spirv cross path")

include_directories (
    "external/clap/include"

    "external/reaper-sdk/sdk"
    "external/reaper-sdk/WDL"

    "external/restinio/dev/asio/include"
    "external/restinio/dev/nodejs/http_parser"
    "external/restinio/dev"
    "external/restinio/dev/fmt/include"

    "external/cwalk"
    "external/json/include"
    "external/boxer/include"

    "src/reashader"

    ${RS_SPVC_PATH}

    # we use glslang headers in the vulkan sdk, since they work

    ${RS_GLM_PATH}
    "${RS_VMA_PATH}/include"
    ${RS_TOL_PATH}
    ${RS_STB_PATH}

    # at the end because of the override with the manual downloaded libraries (glslang, spirv...)
    "$ENV{VK_SDK_PATH}/include"
)

##############################################################################
#                                                                            #
#                              CLAP PLUGIN                                  #
#                                                                            #
##############################################################################

# Phase A: a minimal CLAP shell only (src/clap/reashader_clap.cpp). It proves the
# REAPER video tap works from CLAP (see spikes/clap-video-tap, now retired) and stands
# up the build pipeline. It is NOT wired to ReaShaderProcessor/ReaShaderRenderer/rsparams/
# rsui yet -- those use Steinberg VST3 types pervasively and need a real port (Phase B),
# at which point their sources get added to this target.

add_library(${PROJECT_NAME} MODULE
    src/clap/reashader_clap.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

##############################################################################
#                                                                            #
#                        SHADER COMPILATION (glslc)                        #
#                                                                            #
##############################################################################
#
# Generator-agnostic replacement for sln-make.inix's MSBuild pre-link shader step.
# Each shader is a proper custom command with tracked input/output, so rebuilds are
# incremental instead of "always recompile everything." Not linked into the Phase A
# target yet (nothing consumes .spv output until the renderer is ported in Phase B) --
# this just proves the pipeline and keeps the checked-in .spv files up to date if you
# have the Vulkan SDK installed.

find_program(RS_GLSLC_EXECUTABLE glslc HINTS "$ENV{VK_SDK_PATH}/Bin" "$ENV{VULKAN_SDK}/Bin")

if(RS_GLSLC_EXECUTABLE)
    function(rs_compile_shader stage filename)
        get_filename_component(name_we ${filename} NAME_WE)
        set(src "${CMAKE_SOURCE_DIR}/src/shaders/${filename}")
        set(out "${CMAKE_SOURCE_DIR}/src/shaders/${name_we}.spv")
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${RS_GLSLC_EXECUTABLE} -fshader-stage=${stage} ${src} -o ${out}
            DEPENDS ${src}
            COMMENT "Compiling shader ${filename}"
            VERBATIM
        )
        set(RS_COMPILED_SHADERS ${RS_COMPILED_SHADERS} ${out} PARENT_SCOPE)
    endfunction()

    rs_compile_shader(vertex vert.glsl)
    rs_compile_shader(fragment frag.glsl)
    rs_compile_shader(vertex pp_vert.glsl)
    rs_compile_shader(fragment pp_frag.glsl)

    add_custom_target(ReaShaderShaders DEPENDS ${RS_COMPILED_SHADERS})
    add_dependencies(${PROJECT_NAME} ReaShaderShaders)
else()
    message(STATUS "glslc not found (no Vulkan SDK detected) -- shader recompilation skipped; "
                    "the precompiled .spv files already committed under src/shaders are used as-is")
endif()

##############################################################################
#                                                                            #
#                            RESOURCE STAGING                              #
#                                                                            #
##############################################################################
#
# Generator-agnostic replacement for sln-make.inix's MSBuild pre-link resource-copy step.
# CLAP plugins are a single flat file on Windows/Linux (no bundle folder like VST3), so
# assets are staged next to the built .clap. Phase B decides how the ported renderer
# locates them at runtime (e.g. resolving a path relative to the plugin binary).

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/images" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/meshes" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/shaders" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/reashader/rsui/frontend" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rsui"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rsui/styles"
    COMMENT "Staging ReaShader resources next to the built plugin"
)

##############################################################################
#                                                                            #
#                    LOCAL DEV DEPLOY (per-user CLAP folder)               #
#                                                                            #
##############################################################################
#
# Generator-agnostic replacement for sln-make.inix's MSBuild post-build deploy step.
# Deploys to the *per-user* CLAP plugin folder (confirmed working during the video-tap
# spike), not an admin-owned system folder -- no more "take ownership of Program Files"
# step like the old VST3 flow needed.

if(WIN32)
    set(RS_CLAP_USER_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
elseif(APPLE)
    set(RS_CLAP_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(RS_CLAP_USER_DIR "$ENV{HOME}/.clap")
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RS_CLAP_USER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${RS_CLAP_USER_DIR}/${PROJECT_NAME}.clap"
    COMMENT "Deploying ${PROJECT_NAME}.clap to the per-user CLAP plugin folder (${RS_CLAP_USER_DIR}) for local testing"
)
