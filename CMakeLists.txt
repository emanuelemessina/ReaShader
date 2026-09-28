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

# Build Deps

# cmake-git-versioning

set(CGV_PATH "${CMAKE_CURRENT_LIST_DIR}/external/cmake-git-versioning")

if(NOT EXISTS "${CGV_PATH}/cmake-git-versioning.cmake")
    message(FATAL_ERROR "cmake-git-versioning not found at ${CGV_PATH}")
endif()

include("${CGV_PATH}/cmake-git-versioning.cmake")

##################
# Project
##################

set(PROJECT_NAME ReaShader)

project(${PROJECT_NAME}
    VERSION ${PROJECT_VERSION_CMAKE}
    DESCRIPTION "${PROJECT_NAME} CLAP Plug-in"
    LANGUAGES CXX C
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
#
# GLM/VMA/tinyobjloader/STB are all header-only and vendored as git submodules
# (external/glm, external/VulkanMemoryAllocator, external/tinyobjloader, external/stb)
# -- no manual download/placement needed, unlike before. glslang and SPIRV-Cross are
# NOT vendored: the installed Vulkan SDK already ships usable static libs for both
# (see the find_library block below), so building/placing them by hand is no longer
# required either. The Vulkan SDK itself remains the one dependency that has to be
# installed manually -- it's a full installer, not something to vendor.

set(RS_GLM_PATH "${CMAKE_CURRENT_LIST_DIR}/external/glm" CACHE PATH "GLM path")
set(RS_VMA_PATH "${CMAKE_CURRENT_LIST_DIR}/external/VulkanMemoryAllocator" CACHE PATH "Vulkan Memory Allocator path")
set(RS_TOL_PATH "${CMAKE_CURRENT_LIST_DIR}/external/tinyobjloader" CACHE PATH "Tiny Obj Loader path")
set(RS_STB_PATH "${CMAKE_CURRENT_LIST_DIR}/external/stb" CACHE PATH "STB path")

# SPIRV-Cross headers: defaults to the Vulkan SDK's copy. If a given SDK version turns
# out to be incompatible (this has happened before -- see README), override this cache
# variable to point at a manually built SPIRV-Cross checkout instead.
set(RS_SPVC_PATH "$ENV{VK_SDK_PATH}/Include" CACHE PATH "SPIRV-Cross headers path")

include_directories (
    "external/clap/include"

    "external/reaper-sdk/sdk"
    "external/WDL/WDL"

    "external/restinio/dev/asio/include"
    "external/restinio/dev/nodejs/http_parser"
    "external/restinio/dev"
    "external/restinio/dev/fmt/include"

    "external/cwalk"
    "external/json/include"
    "external/boxer/include"

    "src/reashader"

    ${RS_SPVC_PATH}

    ${RS_GLM_PATH}
    "${RS_VMA_PATH}/include"
    ${RS_TOL_PATH}
    ${RS_STB_PATH}

    # glslang headers also come from here (glslang/... is a subdir of the SDK's Include) --
    # at the end because of the override with any manually placed libraries above.
    "$ENV{VK_SDK_PATH}/include"
)

# glslang / SPIRV-Cross static libraries: both ship prebuilt (release-only) inside the
# installed Vulkan SDK's Lib/ folder, so nothing needs to be built by hand. These are
# looked up optionally (like RS_GLSLC_EXECUTABLE below) -- a missing Vulkan SDK must not
# break a build that doesn't need the renderer yet. Not linked into any target yet --
# nothing consumes glslang/SPIRV-Cross symbols until the renderer is wired in (Phase B+1);
# RS_VULKAN_RENDERER_LIBS is left populated here so that pass can just target_link_libraries
# against it without re-discovering these paths.

set(RS_GLSLANG_LIB_NAMES glslang OSDependent MachineIndependent GenericCodeGen OGLCompiler)
set(RS_SPVC_LIB_NAMES spirv-cross-core spirv-cross-glsl)
set(RS_VULKAN_RENDERER_LIBS "")
set(RS_VULKAN_RENDERER_LIBS_MISSING "")

foreach(_rs_lib_name ${RS_GLSLANG_LIB_NAMES} ${RS_SPVC_LIB_NAMES})
    find_library(RS_LIB_${_rs_lib_name} NAMES ${_rs_lib_name} HINTS "$ENV{VK_SDK_PATH}/Lib")
    if(RS_LIB_${_rs_lib_name})
        list(APPEND RS_VULKAN_RENDERER_LIBS ${RS_LIB_${_rs_lib_name}})
    else()
        list(APPEND RS_VULKAN_RENDERER_LIBS_MISSING ${_rs_lib_name})
    endif()
endforeach()

if(RS_VULKAN_RENDERER_LIBS_MISSING)
    message(STATUS "glslang/SPIRV-Cross libraries not found in Vulkan SDK (missing: "
                    "${RS_VULKAN_RENDERER_LIBS_MISSING}) -- fine for now, only needed once "
                    "the renderer is wired into the build")
else()
    message(STATUS "glslang/SPIRV-Cross libraries found in Vulkan SDK")
endif()

##############################################################################
#                                                                            #
#                              CLAP PLUGIN                                  #
#                                                                            #
##############################################################################

# Phase B: rsparams/rsui/reashaderplugin are ported off VST3 and wired into this target.
# ReaShaderRenderer/vkt (the Vulkan pipeline) are NOT wired in yet -- that's a separate, later
# pass -- so process_frame still paints the Phase A diagnostic pattern instead of real video.

add_library(${PROJECT_NAME} MODULE
    src/clap/reashader_clap.cpp

    src/reashader/reashaderplugin.cpp

    src/reashader/rsparams/rsparams.cpp
    src/reashader/rsparams/serdes.cpp

    src/reashader/rsui/backend/rsuiserver.cpp
    src/reashader/rsui/backend/wshandler.cpp

    src/reashader/tools/logging.cpp
    src/reashader/tools/paths.cpp

    # third-party sources these depend on (header-only otherwise)
    external/cwalk/cwalk.c
    external/restinio/dev/fmt/src/format.cc
    external/restinio/dev/nodejs/http_parser/http_parser.c
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
        src/clap/reashader_clap_gui_win32.cpp
        external/boxer/src/boxer_win.cpp
    )
    # restinio/Asio networking + boxer (MessageBox) + ShellExecute (gui's "open web UI" button)
    # + Shlwapi (PathRemoveFileSpecA, used by tools::paths)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock user32 shell32 shlwapi)
elseif(APPLE)
    target_sources(${PROJECT_NAME} PRIVATE external/boxer/src/boxer_mac.mm)
else()
    target_sources(${PROJECT_NAME} PRIVATE external/boxer/src/boxer_linux.cpp)
endif()

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
    # the .clap binary alone isn't enough -- RSUIServer resolves ASSETS_DIR/RSUI_DIR relative to
    # its own directory at runtime (see tools/paths.h), so the staged resource folders have to
    # follow it to wherever it's actually deployed, not just live next to the build-tree copy
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets" "${RS_CLAP_USER_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rsui" "${RS_CLAP_USER_DIR}/rsui"
    COMMENT "Deploying ${PROJECT_NAME}.clap + resources to the per-user CLAP plugin folder (${RS_CLAP_USER_DIR}) for local testing"
)
