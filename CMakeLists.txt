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
# GLM/VMA/tinyobjloader/STB are all header-only and vendored as git submodules
# (external/glm, external/VulkanMemoryAllocator, external/tinyobjloader, external/stb)
# -- no manual download/placement needed, unlike before. glslang and SPIRV-Cross are
# NOT vendored: the installed Vulkan SDK already ships usable static libs for both
# (see the find_library block below), so building/placing them by hand is no longer
# required either. The Vulkan SDK itself remains the one dependency that has to be
# installed manually -- it's a full installer, not something to vendor.
#
# The SDK's own installer sets VULKAN_SDK, not VK_SDK_PATH (a pre-existing project
# convention) -- check both, preferring VK_SDK_PATH for anyone who set it explicitly.
if(DEFINED ENV{VK_SDK_PATH} AND NOT "$ENV{VK_SDK_PATH}" STREQUAL "")
    set(RS_VK_SDK_PATH "$ENV{VK_SDK_PATH}")
else()
    set(RS_VK_SDK_PATH "$ENV{VULKAN_SDK}")
endif()

set(RS_GLM_PATH "${CMAKE_CURRENT_LIST_DIR}/external/glm" CACHE PATH "GLM path")
set(RS_VMA_PATH "${CMAKE_CURRENT_LIST_DIR}/external/VulkanMemoryAllocator" CACHE PATH "Vulkan Memory Allocator path")
set(RS_TOL_PATH "${CMAKE_CURRENT_LIST_DIR}/external/tinyobjloader" CACHE PATH "Tiny Obj Loader path")
set(RS_STB_PATH "${CMAKE_CURRENT_LIST_DIR}/external/stb" CACHE PATH "STB path")

# SPIRV-Cross headers: defaults to the Vulkan SDK's copy, which nests them under
# Include/spirv_cross/ (so #include <spirv_cross.hpp> resolves) -- unlike a from-source
# SPIRV-Cross checkout, whose headers sit flat at the repo root. If a given SDK version
# turns out to be incompatible (this has happened before -- see README), override this
# cache variable to point at a manually built SPIRV-Cross checkout instead.
set(RS_SPVC_PATH "${RS_VK_SDK_PATH}/Include/spirv_cross" CACHE PATH "SPIRV-Cross headers path")

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
    "${RS_VK_SDK_PATH}/include"
)

# glslang / SPIRV-Cross static libraries: both ship prebuilt inside the installed Vulkan
# SDK's Lib/ folder (this SDK version ships debug ("d"-suffixed) and release variants of
# each), so nothing needs to be built by hand. These are looked up optionally (like
# RS_GLSLC_EXECUTABLE below) -- a missing Vulkan SDK must not break a build that doesn't
# need the renderer. RS_VULKAN_RENDERER_LIBS is linked into the target below.
# Note: OGLCompiler.lib doesn't exist in current glslang releases (folded away upstream) --
# don't add it back. glslang-default-resource-limits is needed for GetDefaultResources(),
# used by vktpipeline.h's compile_glsl_to_spirv(). SPIRV-Tools/-opt are needed because
# vktpipeline.h pulls in glslang/SPIRV/SpvTools.h, which links against SPIRV-Tools' validator
# and optimizer.

set(RS_GLSLANG_LIB_NAMES glslang OSDependent MachineIndependent GenericCodeGen
                         glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools)
set(RS_SPVC_LIB_NAMES spirv-cross-core spirv-cross-glsl)
set(RS_VULKAN_RENDERER_LIBS "")
set(RS_VULKAN_RENDERER_LIBS_MISSING "")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RS_VK_LIB_SUFFIX "d")
else()
    set(RS_VK_LIB_SUFFIX "")
endif()

foreach(_rs_lib_name ${RS_GLSLANG_LIB_NAMES} ${RS_SPVC_LIB_NAMES})
    find_library(RS_LIB_${_rs_lib_name} NAMES "${_rs_lib_name}${RS_VK_LIB_SUFFIX}" ${_rs_lib_name}
                 HINTS "${RS_VK_SDK_PATH}/Lib")
    if(RS_LIB_${_rs_lib_name})
        list(APPEND RS_VULKAN_RENDERER_LIBS ${RS_LIB_${_rs_lib_name}})
    else()
        list(APPEND RS_VULKAN_RENDERER_LIBS_MISSING ${_rs_lib_name})
    endif()
endforeach()

if(RS_VULKAN_RENDERER_LIBS_MISSING)
    message(FATAL_ERROR "glslang/SPIRV-Cross libraries not found in Vulkan SDK (missing: "
                         "${RS_VULKAN_RENDERER_LIBS_MISSING}) -- rsrenderer.cpp/vkt need them. "
                         "Install/update the Vulkan SDK (see README).")
else()
    message(STATUS "glslang/SPIRV-Cross libraries found in Vulkan SDK")
endif()

find_package(Vulkan REQUIRED)

##############################################################################
#                                                                            #
#                              CLAP PLUGIN                                  #
#                                                                            #
##############################################################################

# rsparams/rsui/reashaderplugin (ported off VST3) and ReaShaderRenderer/vkt (the Vulkan pipeline)
# are both wired into this target -- process_frame drives real rendering.

add_library(${PROJECT_NAME} MODULE
    src/clap/reashader_clap.cpp

    src/reashader/reashaderplugin.cpp
    src/reashader/rsrenderer.cpp

    src/reashader/rsparams/rsparams.cpp
    src/reashader/rsparams/serdes.cpp

    src/reashader/rsui/backend/rsuiserver.cpp
    src/reashader/rsui/backend/wshandler.cpp

    src/reashader/tools/logging.cpp
    src/reashader/tools/paths.cpp

    src/reashader/vkt/vktbuffers.cpp
    src/reashader/vkt/vktcommandpool.cpp
    src/reashader/vkt/vktcommon.cpp
    src/reashader/vkt/vktdescriptors.cpp
    src/reashader/vkt/vktdevices.cpp
    src/reashader/vkt/vktimages.cpp
    src/reashader/vkt/vktmesh.cpp
    src/reashader/vkt/vktqueue.cpp
    src/reashader/vkt/vktrenderpass.cpp

    # third-party sources these depend on (header-only otherwise)
    external/cwalk/cwalk.c
    external/restinio/dev/fmt/src/format.cc
    external/restinio/dev/nodejs/http_parser/http_parser.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan ${RS_VULKAN_RENDERER_LIBS})

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
# incremental instead of "always recompile everything." rsrenderer.cpp loads vert.spv/
# frag.spv/pp_vert.spv at runtime (see SHADERS_DIR usage), so ReaShaderShaders is a real
# build dependency of the plugin target now, not just a freestanding convenience target.

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
# assets are staged next to the built .clap -- ReaShaderRenderer locates them via
# tools/paths.h's ASSETS_DIR (resolved relative to the plugin binary at runtime).

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
