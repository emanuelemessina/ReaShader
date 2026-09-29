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

# Stage directories
set(PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}")

#################################
# Libraries
#################################

# Vulkan:
# - require Vulkan SDK
# - shaderc (runtime GLSL -> SPIR-V) ships with it

find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

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
    PREFIX ""
    SUFFIX ".clap"
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_STAGE_DIR}"
)

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
    src/render/gpu.cpp

    src/util/logging.cpp
    src/util/paths.cpp
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
# POST_BUILD
#################################

# Stage resources next to the plugin

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/images" "${PLUGIN_STAGE_DIR}/assets/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res/meshes" "${PLUGIN_STAGE_DIR}/assets/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/shaders" "${PLUGIN_STAGE_DIR}/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/ui" "${PLUGIN_STAGE_DIR}/rsui"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLUGIN_STAGE_DIR}/rsui/styles"
    COMMENT "Staging ReaShader resources next to the built plugin"
)
