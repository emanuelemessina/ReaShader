#################################
# Tooling
#################################

# ReaShader's test application (doc/testing.md):
# - its own project, never included by the main CMakeLists.txt
# - run through build.cmake: cmake [-DPROFILE=<debug|release>] -DTEST=ON -P build.cmake

cmake_minimum_required(VERSION 3.25)

#################################
# Project configuration
#################################

project(ReaShaderTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Directories
get_filename_component(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

#################################
# Libraries
#################################

# Vulkan: as in the main build (glslc for the internal shaders, shaderc for the compiler under test)

find_package(Vulkan REQUIRED COMPONENTS glslc shaderc_combined)

# Boxer (util/logging)

add_subdirectory("${REPO_DIR}/external/boxer" boxer SYSTEM)

#################################
# Target
#################################

add_executable(reashader_tests)

#################################
# Generated
#################################

# Internal shaders: SPIR-V as a C array, included by the render code under test

foreach(_shader fullscreen.vert scene.vert scene.frag)
    set(_src "${REPO_DIR}/src/shaders/internal/${_shader}")
    set(_out "${GENERATED_DIR}/${_shader}.inc")
    add_custom_command(
        OUTPUT ${_out}
        COMMAND Vulkan::glslc -mfmt=num ${_src} -o ${_out}
        DEPENDS ${_src}
        COMMENT "Compiling internal shader ${_shader}"
        VERBATIM
    )
    target_sources(reashader_tests PRIVATE ${_out})
endforeach()

#################################
# Sources
#################################

# Test application: runner, support, cases

target_sources(reashader_tests PRIVATE
    main.cpp
    support/support.cpp

    cases/render.cpp
    cases/shader_compiler.cpp
)

# Fake REAPER host (Windows: LoadLibrary, Win32 message boxes) and its cases

if(WIN32)
    target_sources(reashader_tests PRIVATE
        host/reaper.cpp
        host/video.cpp

        cases/host_lifecycle.cpp
    )
else()
    message(WARNING "TODO: the fake REAPER host is Windows-only; host tests are skipped")
endif()

# Plugin sources under unit test

target_sources(reashader_tests PRIVATE
    "${REPO_DIR}/src/render/gpu.cpp"
    "${REPO_DIR}/src/render/context.cpp"
    "${REPO_DIR}/src/render/frame_targets.cpp"
    "${REPO_DIR}/src/render/shader_pass.cpp"
    "${REPO_DIR}/src/render/shader_compiler.cpp"
    "${REPO_DIR}/src/render/scene.cpp"
    "${REPO_DIR}/src/util/logging.cpp"
    "${REPO_DIR}/src/util/paths.cpp"
)

# External sources (warnings disabled)

set(EXTERNAL_SOURCES
    "${REPO_DIR}/external/vk-bootstrap/src/VkBootstrap.cpp"
    "${REPO_DIR}/external/SPIRV-Reflect/spirv_reflect.cpp"
)

target_sources(reashader_tests PRIVATE ${EXTERNAL_SOURCES})
set_source_files_properties(${EXTERNAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

#################################
# Includes
#################################

# Test and plugin includes
target_include_directories(reashader_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO_DIR}/src"
    ${GENERATED_DIR}
)

# External includes
target_include_directories(reashader_tests SYSTEM PRIVATE
    "${REPO_DIR}/external/doctest"
    "${REPO_DIR}/external/clap/include"
    "${REPO_DIR}/external/reaper-sdk/sdk"
    "${REPO_DIR}/external/WDL/WDL"
    "${REPO_DIR}/external/json/include"
    "${REPO_DIR}/external/glm"
    "${REPO_DIR}/external/VulkanMemoryAllocator/include"
    "${REPO_DIR}/external/tinyobjloader"
    "${REPO_DIR}/external/stb"
    "${REPO_DIR}/external/vk-bootstrap/src"
    "${REPO_DIR}/external/SPIRV-Reflect"
)

#################################
# Compiler options
#################################

# The built plugin the host tests load: the main build's preset of the same profile
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(REASHADER_CLAP "${REPO_DIR}/build/windows-debug/ReaShader-Debug.clap")
else()
    set(REASHADER_CLAP "${REPO_DIR}/build/windows-release/ReaShader.clap")
endif()

target_compile_definitions(reashader_tests PRIVATE
    REPO_DIR="${REPO_DIR}"
    REASHADER_CLAP="${REASHADER_CLAP}"
)

# Flags: the plugin's
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(reashader_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Werror=return-type
        -Wno-missing-field-initializers
    )
endif()

#################################
# Link
#################################

target_link_libraries(reashader_tests PRIVATE
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    Boxer
)

if(WIN32)
    # The Vulkan SDK debug static libraries omit PDB symbols
    target_link_options(reashader_tests PRIVATE "LINKER:/ignore:4099")
endif()

#################################
# Staging
#################################

# The render code resolves resources/ next to its binary (util::paths): stage the scene's assets there

add_custom_command(TARGET reashader_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_DIR}/res/meshes" "$<TARGET_FILE_DIR:reashader_tests>/resources/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_DIR}/res/images" "$<TARGET_FILE_DIR:reashader_tests>/resources/images"
    VERBATIM
)
