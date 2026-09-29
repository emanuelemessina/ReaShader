#################################
# GPU test (seed of the test application, see doc/proposals.md)
#################################

# A standalone project, separate from the plugin's build:
# - compiles the renderer's GPU code with a small test main
# - runs frames on every GPU of the machine and checks the output pixels
#
# Usage (from the repo root, with the same prerequisites as the plugin):
#   cmake -S test/seed -B build/tests-seed -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build/tests-seed
#   build/tests-seed/gpu_test

cmake_minimum_required(VERSION 3.25)
project(gpu_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

find_package(Vulkan REQUIRED COMPONENTS glslc shaderc_combined)
add_subdirectory("${REPO_DIR}/external/boxer" boxer SYSTEM)

# Internal shaders, as in the plugin's build
set(INTERNAL_SHADER_INCLUDES "")
foreach(_shader fullscreen.vert scene.vert scene.frag)
    set(_out "${GENERATED_DIR}/${_shader}.inc")
    add_custom_command(
        OUTPUT ${_out}
        COMMAND Vulkan::glslc -mfmt=num "${REPO_DIR}/src/shaders/internal/${_shader}" -o ${_out}
        DEPENDS "${REPO_DIR}/src/shaders/internal/${_shader}"
        VERBATIM
    )
    list(APPEND INTERNAL_SHADER_INCLUDES ${_out})
endforeach()

add_executable(gpu_test
    gpu_test.cpp
    ${INTERNAL_SHADER_INCLUDES}
    "${REPO_DIR}/src/render/gpu.cpp"
    "${REPO_DIR}/src/render/context.cpp"
    "${REPO_DIR}/src/render/frame_targets.cpp"
    "${REPO_DIR}/src/render/shader_pass.cpp"
    "${REPO_DIR}/src/render/shader_compiler.cpp"
    "${REPO_DIR}/src/render/scene.cpp"
    "${REPO_DIR}/src/util/logging.cpp"
    "${REPO_DIR}/src/util/paths.cpp"
    "${REPO_DIR}/external/vk-bootstrap/src/VkBootstrap.cpp"
    "${REPO_DIR}/external/SPIRV-Reflect/spirv_reflect.cpp"
)

target_include_directories(gpu_test PRIVATE "${REPO_DIR}/src" ${GENERATED_DIR})
target_include_directories(gpu_test SYSTEM PRIVATE
    "${REPO_DIR}/external/json/include"
    "${REPO_DIR}/external/glm"
    "${REPO_DIR}/external/VulkanMemoryAllocator/include"
    "${REPO_DIR}/external/tinyobjloader"
    "${REPO_DIR}/external/stb"
    "${REPO_DIR}/external/vk-bootstrap/src"
    "${REPO_DIR}/external/SPIRV-Reflect"
)
target_compile_definitions(gpu_test PRIVATE REPO_DIR="${REPO_DIR}")
target_link_libraries(gpu_test PRIVATE Vulkan::Vulkan Vulkan::shaderc_combined Boxer)

if(WIN32)
    target_link_options(gpu_test PRIVATE "LINKER:/ignore:4099")
endif()

# The scene loads resources/{meshes,images} next to the executable (util::paths)
add_custom_command(TARGET gpu_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_DIR}/res/meshes" "$<TARGET_FILE_DIR:gpu_test>/resources/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO_DIR}/res/images" "$<TARGET_FILE_DIR:gpu_test>/resources/images"
)
