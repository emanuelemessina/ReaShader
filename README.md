# ReaShader

## Motivation

Reaper is a great and versatile DAW, capable of handling not just audio but also video.
\
While it has its own video processing capabilities, currently (2024) the features are limited and the effects must be written by hand as custom scripts accessing an internal API.
\
\
Thus, Reashader is my own experiment in trying to make a plugin that acts as a video processor for Reaper.
\
You install it the same way you would install any audio plugin, and it will process video frames instead of audio samples.
\
\
It's a work in progress, proofs of concept are available in [Releases](https://github.com/emanuelemessina/ReaShader/releases).
\
Currently i've created a web based UI (instead of the default VSTGUI, given the complexity of the interface), and i use Vulkan to process the frames. 
\
\
Please cite me if you benefit from this project, as it required a lot of blood, sweat and tears, thank you 🙏.

<br>

## Plugin format: migrating from VST3 to CLAP

<br>

ReaShader is in the middle of moving from VST3 to [CLAP](https://cleveraudio.org/) — a lighter, C-ABI, header-only plugin format. REAPER's video-processing tap works just as well from CLAP as it did from VST3, and dropping the VST3 SDK removes most of the build-system pain (bundle folder structure, validator, processor/controller split, IDE-specific build hacks) while keeping the door open for future changes (e.g. embedding the UI in REAPER's FX window, a possible Rust rewrite).

The build system has already been migrated (CMake + Ninja + VS Code, no Visual Studio/Xcode project generation needed); the actual rendering/parameter/UI code hasn't been ported off VST3 types yet, so the plugin currently only proves the pipeline works end-to-end rather than doing real video processing. See [CLAUDE.md](CLAUDE.md) for the up-to-date architecture and migration status.

<br>

## Dependencies

<br>

### CMake and Ninja

<br>

Install [CMake](https://cmake.org/) (3.21+) and [Ninja](https://ninja-build.org/). On Windows, build from a Developer Command Prompt (or let VS Code's CMake Tools extension pick an MSVC kit for you) so `cl.exe` is available to Ninja.

<br>

### CMake modules

<br>

- [cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning), cloned anywhere — pass its path as the `RS_CGV_PATH` CMake cache variable, or set the `REASHADER_CGV_PATH` environment variable (read automatically by `CMakePresets.json`).

<br>

### CLAP

<br>

Already vendored under `external/clap` (plain headers, MIT-licensed) — nothing to install.

<br>

### Vulkan / graphics libraries (not needed yet)

<br>

The Vulkan SDK and the graphics libraries below aren't required to build the plugin today — they're only wired into `CMakeLists.txt` in preparation for porting the real renderer onto the new CLAP shell. Skip this section unless you're working on that port.

Download [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) and install (preferably in the default location), then place the following next to the `x.x.x.x` folder inside the Vulkan install location, as CMake has the include paths defaulted to there:

- [GLM](https://github.com/g-truc/glm)
- [Tiny Obj Loader](https://github.com/tinyobjloader/tinyobjloader)
- [STB](https://github.com/nothings/stb)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) : to be built with cmake (both debug and release)
- [glslang](https://github.com/KhronosGroup/glslang) has to be built with cmake (set the build dir to `/build`) (both debug and release!), then the project will automatically find the static library file paths. _(Vulkan sdk has it but the version is obsolete and has conflicts)_
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) _(Same problem as glslang with the vulkan sdk, to be built)_

<br>

## Build steps

<br>

Either open the repo in VS Code with the CMake Tools extension installed (it reads `CMakePresets.json` and will prompt you to pick a kit/toolchain), or from the command line:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

(`macos-debug`/`linux-debug` presets also exist but are untested — Windows is the only platform exercised so far.)

This alone compiles the plugin, stages its resources next to it, and deploys the built `.clap` to your per-user CLAP plugin folder (`%LOCALAPPDATA%\Programs\Common\CLAP` on Windows, `~/Library/Audio/Plug-Ins/CLAP` on macOS, `~/.clap` on Linux) — no admin rights needed, no separate IDE build step.

<br>


## Run

<br>

### Windows

<br>

- Make sure to have all the **VC Redist** updated to the latest version. It can be downloaded from Microsoft website.

- If REAPER doesn't list the plugin, make sure it has rescanned for CLAP plugins (Preferences → Plug-ins → Clear cache/re-scan).

<br>

## Development

<br>

See [Development](doc/Development.md).

<br>


## Credits

[License File](LICENSE)

<br>

### Author

<br>

    Emanuele Messina

<br>

#### Open source libraries

<br>

- [WDL](https://github.com/justinfrankel/WDL)
- [GLM](https://github.com/g-truc/glm)
- [Tiny Obj Loader](https://github.com/tinyobjloader/tinyobjloader)
- [STB](https://github.com/nothings/stb)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [RESTinio](https://github.com/Stiffstream/restinio)
- [cwalk](https://github.com/likle/cwalk)
- [nlohmann-json](https://github.com/nlohmann/json)
- [boxer](https://github.com/aaronmjacobs/Boxer)
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- [glslang](https://github.com/KhronosGroup/glslang)
- [iMurmurHash](https://github.com/jensyt/imurmurhash-js)

<br>

#### Third Party

<br>

- [CLAP](https://cleveraudio.org/) _by the CLever Audio Plug-in project_
- [Vulkan](https://vulkan.lunarg.com/) _by Khronos Group_
- [Reaper SDK](https://github.com/justinfrankel/reaper-sdk) _by Cockos_

<br>

#### Thanks to

<br>

- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
