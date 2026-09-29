# ReaShader

## Motivation

Reaper is a great and versatile DAW, capable of handling not just audio but also video.
\
While it has its own video processing capabilities, currently (2024) the features are limited and the effects must be written by hand as custom scripts accessing an internal API.
\
\
Thus, ReaShader is my own experiment in trying to make a plugin that acts as a video processor for Reaper.
\
You install it the same way you would install any audio plugin, and it will process video frames instead of audio samples.
\
\
Please cite me if you benefit from this project, as it required a lot of blood, sweat and tears, thank you 🙏.

## What it does

- A [CLAP](https://cleveraudio.org/) plugin for REAPER that runs the track's video through a GLSL fragment shader on the GPU, with Vulkan.
- The plugin window is a web UI embedded in REAPER's FX window. From it you:
  - pick a shader;
  - upload new ones;
  - move the shader's sliders;
  - choose the GPU.
- Every shader slider is also a host parameter, so it can be automated in REAPER.
- With no shader selected, the video passes through unchanged.

It's a work in progress. Only Windows is supported for now; macOS/Linux builds are planned (the code has `TODO`s where platform work is missing).

## Using it

1. Build it (below) or grab a release, and put the `ReaShader` plugin folder inside your CLAP folder (hosts search it recursively):
   - Windows: `%LOCALAPPDATA%\Programs\Common\CLAP\ReaShader`
   - macOS: `~/Library/Audio/Plug-Ins/CLAP/ReaShader`
   - Linux: `~/.clap/ReaShader`
2. In REAPER, add "ReaShader" (CLAP) to a track with a video item. If it's not listed, rescan: Preferences → Plug-ins → CLAP → Re-scan.
3. Open REAPER's video window (View → Video).
4. In the plugin window, **Upload** a shader: try the examples in `resources/shaders/examples` inside the plugin folder. An uploaded shader is compiled once and added to the shader list.

Writing your own shader is simple: see [the examples' README](src/shaders/examples/README.md).

The plugin folder contains:

```
ReaShader/
  ReaShader.clap
  ui/                        the plugin window (HTML/JS/CSS)
  resources/
    images/, meshes/
    shaders/examples/        example shader sources, to upload and learn from
    shaders/compiled/        shaders compiled on upload (the shader list)
  rs.log                     the plugin's log
```

## Building

### Prerequisites

- [CMake](https://cmake.org/) 3.25+ and [Ninja](https://ninja-build.org/)
- [clang](https://releases.llvm.org/) (`clang++`) on `PATH`
- The [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/). Its installer sets `VULKAN_SDK`, which is how the build finds it; glslc and shaderc come with it.
- [Dart Sass](https://sass-lang.com/install/) (`sass`) on `PATH`, e.g. `choco install sass`, `npm install -g sass` or `brew install sass/sass/sass`
- The submodules, which hold every other dependency:

  ```
  git submodule update --init --recursive
  ```

  (or clone with `git clone --recurse-submodules`)

- The first configure downloads the WebView2 headers from NuGet once, if no system copy is found.

### Tasks (VS Code)

- **build+deploy** (default build task, Ctrl+Shift+B): configures (the first time), builds, and deploys the plugin folder to `<your CLAP folder>/ReaShader`. If REAPER has the plugin loaded, the deploy is skipped with a warning: close REAPER and build again.
- **clean**: wipes the build directory, for a fresh configure.

From a terminal:

```
cmake -DPROFILE=debug -P build.cmake
```

or build without deploying:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

## Project layout

```
src/clap/        CLAP entry point, plugin window (Win32), embedded webview host
src/plugin/      the plugin: parameters, state, UI messages, REAPER video tap
src/render/      the Vulkan renderer and the shader compiler
src/ui/          the web UI (index.html, scripts/, styles/)
src/shaders/     example shaders, internal shaders, the logo scene's shaders
src/util/        logging, paths, fault handling
tests/shaders/   shaders for manual testing
external/        dependencies (git submodules)
doc/history.md   how the project got here (VST3 → CLAP, design decisions)
```

## Credits

[License File](LICENSE)

### Author

    Emanuele Messina

#### Open source libraries

- [CLAP](https://github.com/free-audio/clap) _by the CLever Audio Plug-in project_
- [Vulkan SDK](https://vulkan.lunarg.com/) _by Khronos Group_
- [Reaper SDK](https://github.com/justinfrankel/reaper-sdk) _by Cockos_
- [webview](https://github.com/webview/webview)
- [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [shaderc](https://github.com/google/shaderc) (from the Vulkan SDK)
- [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
- [GLM](https://github.com/g-truc/glm)
- [Tiny Obj Loader](https://github.com/tinyobjloader/tinyobjloader)
- [STB](https://github.com/nothings/stb)
- [nlohmann-json](https://github.com/nlohmann/json)
- [Boxer](https://github.com/aaronmjacobs/Boxer)
- [WDL](https://github.com/justinfrankel/WDL)
- [cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning)
- [Dart Sass](https://sass-lang.com/dart-sass/) (build tool)

#### Thanks to

- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
