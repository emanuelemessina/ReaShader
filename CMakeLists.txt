#################################
# Tooling
#################################

# - enforce cmake version
# - require git
# - include cmake_git_versioning

cmake_minimum_required(VERSION 3.25)

find_package(Git REQUIRED)

set(CGV_PATH "${CMAKE_CURRENT_LIST_DIR}/external/cmake-git-versioning")
if(NOT EXISTS "${CGV_PATH}/cmake-git-versioning.cmake")
    message(FATAL_ERROR "cmake-git-versioning not found at ${CGV_PATH} (missing submodule)")
endif()
include("${CGV_PATH}/cmake-git-versioning.cmake")

#################################
# Project configuration
#################################

set(PROJECT_NAME ReaShader)

project(${PROJECT_NAME}
    VERSION ${PROJECT_VERSION_CMAKE}
    DESCRIPTION "${PROJECT_NAME} CLAP Plugin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Directories
set(PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

# Plugin identity

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PLUGIN_DISPLAY_NAME "ReaShader (Debug)")
    set(PLUGIN_ID "com.emanuelemessina.reashader.debug")
    set(PLUGIN_FILE_NAME "ReaShader-Debug" CACHE INTERNAL "")
else()
    set(PLUGIN_DISPLAY_NAME "ReaShader")
    set(PLUGIN_ID "com.emanuelemessina.reashader")
    set(PLUGIN_FILE_NAME "ReaShader" CACHE INTERNAL "")
endif()

#################################
# Libraries
#################################

# Vulkan:
# - require Vulkan SDK
# - glslc (build-time GLSL -> SPIR-V) and shaderc (compiles uploaded shaders) ship with it

find_package(Vulkan REQUIRED COMPONENTS glslc shaderc_combined)


# WebView

if(WIN32)
    set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/webview SYSTEM)
endif()

# Boxer

if(APPLE)
    message(WARNING "TODO: boxer on macOS is untested (needs Objective-C++ for boxer_mac.mm)")
elseif(NOT WIN32)
    message(WARNING "TODO: boxer on Linux is untested (needs GTK3)")
endif()

add_subdirectory(external/boxer SYSTEM)

#################################
# Target
#################################

add_library(${PROJECT_NAME} MODULE)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PLUGIN_FILE_NAME}"
    PREFIX ""
    SUFFIX ".clap"
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_STAGE_DIR}"
)

#################################
# Generated
#################################

# Internal shaders: SPIR-V as a C array, included by the renderer (src/shaders/internal/*)

set(INTERNAL_SHADERS
    fullscreen.vert
    scene.vert
    scene.frag
)

foreach(_shader ${INTERNAL_SHADERS})
    set(_src "${CMAKE_SOURCE_DIR}/src/shaders/internal/${_shader}")
    set(_out "${GENERATED_DIR}/${_shader}.inc")
    add_custom_command(
        OUTPUT ${_out}
        COMMAND Vulkan::glslc -mfmt=num ${_src} -o ${_out}
        DEPENDS ${_src}
        COMMENT "Compiling internal shader ${_shader}"
        VERBATIM
    )
    target_sources(${PROJECT_NAME} PRIVATE ${_out})
endforeach()

# Sass

find_program(SASS_EXECUTABLE sass)
if(NOT SASS_EXECUTABLE)
    message(FATAL_ERROR "sass not found: install Dart Sass and put it on PATH "
                        "(e.g. `choco install sass`, `npm install -g sass`, `brew install sass/sass/sass`)")
endif()

file(GLOB_RECURSE UI_STYLES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/styles/*.scss")

add_custom_command(
    OUTPUT "${GENERATED_DIR}/index.css"
    COMMAND ${SASS_EXECUTABLE} --style=compressed --no-source-map
            "${CMAKE_SOURCE_DIR}/src/ui/styles/ui.scss" "${GENERATED_DIR}/index.css"
    DEPENDS ${UI_STYLES}
    COMMENT "Compiling UI stylesheet"
    VERBATIM
)
target_sources(${PROJECT_NAME} PRIVATE "${GENERATED_DIR}/index.css")

#################################
# Sources
#################################

# Core sources
target_sources(${PROJECT_NAME} PRIVATE
    src/clap/plugin_entry.cpp

    src/plugin/plugin.cpp
    src/plugin/params.cpp

    src/render/renderer.cpp
    src/render/context.cpp
    src/render/frame_targets.cpp
    src/render/shader_pass.cpp
    src/render/shader_compiler.cpp
    src/render/scene.cpp
    src/render/gpu.cpp

    src/util/logging.cpp
    src/util/paths.cpp
    src/util/shell.cpp
)

# External sources (warnings disabled)

set(EXTERNAL_SOURCES
    external/vk-bootstrap/src/VkBootstrap.cpp
    external/SPIRV-Reflect/spirv_reflect.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${EXTERNAL_SOURCES})
set_source_files_properties(${EXTERNAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# Platform-specific sources

set(PLATFORM_SPECIFIC_SOURCES "")

if(WIN32)
    list(APPEND PLATFORM_SPECIFIC_SOURCES
        src/clap/gui_win32.cpp
        src/clap/webui_host.cpp
    )
elseif(APPLE)
    message(WARNING "TODO: clap.gui and embedded webview are not implemented on macOS yet")
else()
    message(WARNING "TODO: clap.gui and embedded webview are not implemented on Linux yet")
endif()

target_sources(${PROJECT_NAME} PRIVATE ${PLATFORM_SPECIFIC_SOURCES})

#################################
# Includes
#################################

# Core includes
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${GENERATED_DIR}
)

# External includes
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    external/clap/include
    external/reaper-sdk/sdk
    external/WDL/WDL
    external/json/include
    external/glm
    external/VulkanMemoryAllocator/include
    external/tinyobjloader
    external/stb
    external/vk-bootstrap/src
    external/SPIRV-Reflect
)

#################################
# Compiler options
#################################

# Version and identity defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    REASHADER_VERSION="${PROJECT_VERSION_STRING}"
    REASHADER_NAME="${PLUGIN_DISPLAY_NAME}"
    REASHADER_ID="${PLUGIN_ID}"
)

# Flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Werror=return-type
        # Vulkan structs are set up as `VkXxxInfo info{ VK_STRUCTURE_TYPE_XXX }`: the rest is zeroed on purpose
        -Wno-missing-field-initializers
    )
endif()

#################################
# Link
#################################

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    Boxer
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32
        shell32
        webview::core_static
    )
    # The Vulkan SDK debug static libraries omit PDB symbols
    target_link_options(${PROJECT_NAME} PRIVATE "LINKER:/ignore:4099")
endif()

#################################
# Staging
#################################

# Stage resources next to the plugin, on every build:
# - resources/images, resources/meshes
# - resources/shaders/examples: example shader sources for the user
# - ui

add_custom_target(stage ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLUGIN_STAGE_DIR}/resources" "${PLUGIN_STAGE_DIR}/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/images" "${PLUGIN_STAGE_DIR}/resources/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/meshes" "${PLUGIN_STAGE_DIR}/resources/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/shaders/examples" "${PLUGIN_STAGE_DIR}/resources/shaders/examples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/ui" "${PLUGIN_STAGE_DIR}/ui"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLUGIN_STAGE_DIR}/ui/styles"
    COMMAND ${CMAKE_COMMAND} -E copy "${GENERATED_DIR}/index.css" "${PLUGIN_STAGE_DIR}/ui/index.css"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Staging ReaShader resources next to the built plugin"
    VERBATIM
)

#################################
# Install
#################################

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION .)
install(DIRECTORY "${PLUGIN_STAGE_DIR}/resources" "${PLUGIN_STAGE_DIR}/ui" DESTINATION .)

#################################
# Package
#################################

# One native installer per OS 

set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PACKAGE_NAME "${PLUGIN_FILE_NAME}")
set(CPACK_PACKAGE_VENDOR "Emanuele Messina")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PLUGIN_FILE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    # Inno Setup:
    # - per user, into the user CLAP folder (writable, for uploaded shaders), no admin
    # - installer/windows/: upgrade cleanup, VC++ runtime check and install

    set(CPACK_GENERATOR INNOSETUP)
    set(CPACK_PACKAGE_FILE_NAME "${PLUGIN_FILE_NAME}-${PROJECT_VERSION_STRING}-win64-setup")

    set(_pf86_var "ProgramFiles(x86)")
    file(TO_CMAKE_PATH "$ENV{ProgramFiles}" _pf)
    file(TO_CMAKE_PATH "$ENV{${_pf86_var}}" _pf86)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _localappdata)

    find_program(ISCC_EXECUTABLE ISCC PATHS
        "${_pf86}/Inno Setup 6"
        "${_pf}/Inno Setup 6"
        "${_localappdata}/Programs/Inno Setup 6"
    )
    if(ISCC_EXECUTABLE)
        set(CPACK_INNOSETUP_EXECUTABLE "${ISCC_EXECUTABLE}")
    endif()

    set(CPACK_INNOSETUP_ARCHITECTURE x64)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
    set(CPACK_INNOSETUP_ICON_FILE "${CMAKE_SOURCE_DIR}/src/ui/images/favicon.ico")
    set(CPACK_INNOSETUP_INSTALL_ROOT "{localappdata}\\Programs\\Common\\CLAP")
    set(CPACK_INNOSETUP_SETUP_AppId "${PLUGIN_ID}")
    set(CPACK_INNOSETUP_SETUP_AppName "${PLUGIN_DISPLAY_NAME}")
    set(CPACK_INNOSETUP_SETUP_UninstallDisplayName "${PLUGIN_DISPLAY_NAME}")
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequired lowest)
    set(CPACK_INNOSETUP_SETUP_DisableProgramGroupPage ON)
    set(CPACK_INNOSETUP_SETUP_DirExistsWarning OFF)
    set(CPACK_INNOSETUP_EXTRA_SCRIPTS "${CMAKE_SOURCE_DIR}/installer/windows/reashader.iss")
    set(CPACK_INNOSETUP_CODE_FILES "${CMAKE_SOURCE_DIR}/installer/windows/installer.pas")

    # VC++ runtime: the newest vc_redist.x64.exe of the installed MSVC toolsets (VS or Build Tools),
    # its version is the toolset folder name (the minimum runtime the plugin needs)

    file(GLOB _vcredists
        "${_pf}/Microsoft Visual Studio/*/*/VC/Redist/MSVC/14.*/vc_redist.x64.exe"
        "${_pf86}/Microsoft Visual Studio/*/*/VC/Redist/MSVC/14.*/vc_redist.x64.exe"
    )
    set(_vcredist_version 0)
    foreach(_candidate ${_vcredists})
        string(REGEX MATCH "/MSVC/([0-9.]+)/" _ "${_candidate}")
        if(CMAKE_MATCH_1 VERSION_GREATER _vcredist_version)
            set(_vcredist_version "${CMAKE_MATCH_1}")
            set(_vcredist "${_candidate}")
        endif()
    endforeach()

    if(NOT _vcredist)
        message(WARNING "vc_redist.x64.exe not found (VC/Redist/MSVC of a Visual Studio or Build Tools install): packaging will fail")
    else()
        string(REPLACE "." ";" _vcredist_version "${_vcredist_version}")
        list(GET _vcredist_version 0 CPACK_INNOSETUP_DEFINE_VcRedistMajor)
        list(GET _vcredist_version 1 CPACK_INNOSETUP_DEFINE_VcRedistMinor)
        list(GET _vcredist_version 2 CPACK_INNOSETUP_DEFINE_VcRedistBuild)
        file(TO_NATIVE_PATH "${_vcredist}" CPACK_INNOSETUP_DEFINE_VcRedist)
    endif()
elseif(APPLE)
    message(WARNING "TODO: packaging on macOS (the .clap is a bundle: resources and ui go inside it; signed .pkg)")
else()
    set(CPACK_GENERATOR TGZ)
    message(WARNING "TODO: packaging on Linux is untested (TGZ of the plugin folder, to extract into ~/.clap)")
endif()

include(CPack)
