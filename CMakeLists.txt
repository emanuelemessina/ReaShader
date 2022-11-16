cmake_minimum_required(VERSION 3.14.0)

##############################################################################
#                                                                            #
#                                 GLOBALS                                    #
#                                                                            # 
##############################################################################

# SET VST-3-SDK PATH

set(RS_VST3_SDK_DIR "" CACHE PATH "Path to vst3 sdk")

if(NOT EXISTS ${RS_VST3_SDK_DIR})
    message(FATAL_ERROR "Invalid path to vst3 sdk!")
endif()

# SET VERSION NUMBER

# set project name
set(PROJECT_NAME ReaShader) # do not change !

# include cmake-git-versioning

# check git
find_package(Git)
if(Git_FOUND)
    message("Git found: ${GIT_EXECUTABLE}")
else()
    message(FATAL_ERROR "Git not found!")
endif()

set(RS_CGV_PATH CACHE PATH "cmake-git-versioning module path")

if(NOT EXISTS ${RS_CGV_PATH})
    message(FATAL_ERROR "Invalid path to cmake-git-versioning!")
endif()

include("${RS_CGV_PATH}/cmake-git-versioning.cmake")

project(${PROJECT_NAME}
    # This is your plug-in version number. Change it here only.
    # Version number symbols usable in C++ can be found in
    # source/version.h and ${PROJECT_BINARY_DIR}/projectversion.h.
    VERSION ${PROJECT_VERSION_CMAKE} 
    DESCRIPTION "${PROJECT_NAME} VST 3 Plug-in"
)

# ADDITIIONAL INCLUDE DIRECTORIES (absolute or relative to this root)

if(DEFINED $ENV{VK_SDK_PATH}) # env var
    message(FATAL_ERROR "Vulkan SDK is not installed!")
endif()

set(RS_GLM_PATH "$ENV{VK_SDK_PATH}/../glm" CACHE PATH "GLM library include path")
set(RS_VMA_PATH "$ENV{VK_SDK_PATH}/../VulkanMemoryAllocator" CACHE PATH "Vulkan Memory Allocator library path (contains include and build)")
set(RS_TOL_PATH "$ENV{VK_SDK_PATH}/../tiny_obj_loader" CACHE PATH "Tiny Obj Loader library include path")
set(RS_STB_PATH "$ENV{VK_SDK_PATH}/../stb" CACHE PATH "STB library include path")

include_directories (
    "$ENV{VK_SDK_PATH}/include"
    
    "external/reaper-sdk/sdk"
    "external/reaper-sdk/WDL"
    
    "external/restinio/dev/asio/include"
    "external/restinio/dev/nodejs/http_parser"
    "external/restinio/dev"
    "external/restinio/dev/fmt/include"
    
    "external/cwalk"
    "external/json/include"
    "external/boxer/include"

    "source/vst3"
    "source/reashader"
    
    ${RS_GLM_PATH}
    "${RS_VMA_PATH}/include"
    ${RS_TOL_PATH}
    ${RS_STB_PATH}
)

# C++ STANDARD

set(CPP_ISO 20)

# PREPROCESSOR DEFINITIONS

add_compile_definitions(
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING # codecvt silence deprecation warning
)

# SOURCES

# quick recursive search
file(GLOB_RECURSE SOURCE_FILES
    "source/*.h"
    "source/*.cpp"

    "source/*.glsl"
    
    "source/*.html"
    "source/*.css"
    "source/*.js"

    "doc/*.md"

    "scripts/*.ps1"

    "external/reaper-sdk/WDL/lice/lice.cpp"
    "external/cwalk/cwalk.c"
    "external/restinio/dev/fmt/src/format.cc"
    "external/restinio/dev/nodejs/http_parser/http_parser.c"
    "external/boxer/src/*.cpp"
    "external/boxer/src/*.mm"

    "sln-make.inix"

    ".vscode/ReaShader.code-workspace"
)
# specific files
file (GLOB SOURCE_FILES_2 LIST_DIRECTORIES FALSE
    "README.md"
)
# merge lists
list (APPEND SOURCE_FILES ${SOURCE_FILES_2})

##############################################################################
#
#                               STEINBERG
#
##############################################################################

# BOOTSTRAPPING VST PLUGIN (sets internal targets and variables)

# set vst3 sdk directory

set(vst3sdk_SOURCE_DIR "${RS_VST3_SDK_DIR}")

if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Path to VST3 SDK is empty!")
endif()

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# bind source files (calls add_library and creates sources_list)

smtg_add_vst3plugin(${PROJECT_NAME}
    ${SOURCE_FILES}
)

#- VSTGUI Wanted ----
if(SMTG_ADD_VSTGUI)
    target_sources(${PROJECT_NAME}
        PRIVATE
            resource/myplugineditor.uidesc
    )
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            vstgui_support
    )
    smtg_target_add_plugin_resources(${PROJECT_NAME}
        RESOURCES
            "resource/myplugineditor.uidesc"
    )
endif(SMTG_ADD_VSTGUI)
# -------------------

smtg_target_add_plugin_snapshots (${PROJECT_NAME}
    RESOURCES
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot.png
        resource/E0F1D00DC00B511782AD083BE3690071_snapshot_2.0x.png
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        sdk
)

smtg_target_configure_version_file(${PROJECT_NAME})

##############################################################################
#
#                   CUSTOM DELAYED (waited for target creation)
#
##############################################################################

# set c++ version
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD ${CPP_ISO})

# ADDITIONAL LIBRARY INCLUDES (needs to be PRIVATE otherwise conflict with SMTG)

target_link_directories(${PROJECT_NAME} PRIVATE
    "$ENV{VK_SDK_PATH}/Lib"
    "${RS_VMA_PATH}/build/src/Debug"
    "${RS_VMA_PATH}/build/src/Release"
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    opengl32.lib 
    vulkan-1.lib
    debug VulkanMemoryAllocatord.lib
    optimized VulkanMemoryAllocator.lib
)

# file groups (IDE)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCE_FILES})

##############################################################################
#
#                           PLATFORM SPECIFIC
#
##############################################################################

# CONFIGURE MAC

if(SMTG_MAC)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12)
    smtg_target_set_bundle(${PROJECT_NAME}
        BUNDLE_IDENTIFIER gvng.reashader
        COMPANY_NAME "Emanuele Messina"
    )

# CONFIGURE WIN

elseif(SMTG_WIN)

    # windows only resource file
    #target_sources(${PROJECT_NAME} PRIVATE 
    #    resource/win32resource.rc
    #)

    # VISUAL STUDIO SPECIFIC

    if(MSVC)

        # startup project
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME}) 

    endif()

# CONFIGURE LINUX

elseif(SMTG_LINUX)

    

endif(SMTG_MAC)

