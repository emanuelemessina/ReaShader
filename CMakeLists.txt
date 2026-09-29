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
set(SHADERS_STAGE_DIR "${PLUGIN_STAGE_DIR}/assets/shaders")

#################################
# Libraries
#################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LIB_SUFFIX "d")
else()
    set(LIB_SUFFIX "")
endif()

# Vulkan:
# - require Vulkan SDK
# - find libraries (glslang, spirv-cross)

find_package(Vulkan REQUIRED COMPONENTS glslc)
cmake_path(GET Vulkan_LIBRARY PARENT_PATH VK_LIB_DIR)

set(GLSLANG_LIB_NAMES 
    glslang 
    OSDependent 
    MachineIndependent 
    GenericCodeGen
    glslang-default-resource-limits 
    SPIRV-Tools-opt 
    SPIRV-Tools
)
set(SPVC_LIB_NAMES 
    spirv-cross-core 
    spirv-cross-glsl
)

set(VULKAN_SDK_LIBS "")
set(VULKAN_SDK_LIBS_MISSING "")

foreach(_lib_name ${GLSLANG_LIB_NAMES} ${SPVC_LIB_NAMES})
    find_library(LIB_${_lib_name} 
        NAMES "${_lib_name}${LIB_SUFFIX}" ${_lib_name}
        HINTS "${VK_LIB_DIR}" 
        NO_DEFAULT_PATH
    )
    if(LIB_${_lib_name})
        list(APPEND VULKAN_SDK_LIBS ${LIB_${_lib_name}})
    else()
        list(APPEND VULKAN_SDK_LIBS_MISSING ${_lib_name})
    endif()
endforeach()

if(VULKAN_SDK_LIBS_MISSING)
    message(FATAL_ERROR "glslang/SPIRV-Cross libraries not found in ${VK_LIB_DIR} "
                        "(missing: ${VULKAN_SDK_LIBS_MISSING}). Install/update the Vulkan SDK.")
endif()

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
# Shaders
#################################

set(RS_SHADERS 
    vert.glsl:vertex 
    frag.glsl:fragment 
    pp_vert.glsl:vertex 
    pp_frag.glsl:fragment
)

set(RS_SHADER_SOURCES "")
set(RS_COMPILED_SHADERS "")

foreach(_shader ${RS_SHADERS})
    string(REPLACE ":" ";" _shader "${_shader}")
    list(GET _shader 0 _file)
    list(GET _shader 1 _stage)
    cmake_path(GET _file STEM _name)

    set(_src "${CMAKE_SOURCE_DIR}/src/shaders/${_file}")
    set(_out "${SHADERS_STAGE_DIR}/${_name}.spv")

    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADERS_STAGE_DIR}"
        COMMAND Vulkan::glslc -fshader-stage=${_stage} ${_src} -o ${_out}
        DEPENDS ${_src}
        COMMENT "Compiling shader ${_file}"
        VERBATIM
    )
    list(APPEND RS_SHADER_SOURCES ${_src})
    list(APPEND RS_COMPILED_SHADERS ${_out})
endforeach()

add_custom_target(rs_shaders DEPENDS ${RS_COMPILED_SHADERS})

add_dependencies(${PROJECT_NAME} rs_shaders)

#################################
# Sources
#################################

# Core sources
target_sources(${PROJECT_NAME} PRIVATE
    src/clap/reashader_clap.cpp

    src/reashader/reashaderplugin.cpp
    src/reashader/rsrenderer.cpp

    src/reashader/rsparams/rsparams.cpp
    src/reashader/rsparams/serdes.cpp

    src/reashader/tools/logging.cpp
    src/reashader/tools/paths.cpp
    src/reashader/tools/base64.cpp

    src/reashader/vkt/vktbuffers.cpp
    src/reashader/vkt/vktcommandpool.cpp
    src/reashader/vkt/vktcommon.cpp
    src/reashader/vkt/vktdescriptors.cpp
    src/reashader/vkt/vktdevices.cpp
    src/reashader/vkt/vktimages.cpp
    src/reashader/vkt/vktmesh.cpp
    src/reashader/vkt/vktqueue.cpp
    src/reashader/vkt/vktrenderpass.cpp
)

# Platform-specific sources

set(PLATFORM_SPECIFIC_SOURCES "")

if(WIN32)
    list(APPEND PLATFORM_SPECIFIC_SOURCES
        src/clap/reashader_clap_gui_win32.cpp
        src/clap/webui_host_win32.cpp
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
    src/reashader
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
    "${Vulkan_INCLUDE_DIR}/spirv_cross"
)

#################################
# Compiler options
#################################

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Werror=return-type
    )
endif()

#################################
# Link
#################################

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    ${VULKAN_SDK_LIBS}
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
    COMMAND ${CMAKE_COMMAND} -E copy ${RS_SHADER_SOURCES} "${SHADERS_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/reashader/rsui/frontend" "${PLUGIN_STAGE_DIR}/rsui"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLUGIN_STAGE_DIR}/rsui/styles"
    COMMENT "Staging ReaShader resources next to the built plugin"
)
