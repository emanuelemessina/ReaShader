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

## Plugin format: migrating from VST3 to CLAP

ReaShader is in the middle of moving from VST3 to [CLAP](https://cleveraudio.org/) — a lighter, C-ABI, header-only plugin format. REAPER's video-processing tap works just as well from CLAP as it did from VST3, and dropping the VST3 SDK removes most of the build-system pain (bundle folder structure, validator, processor/controller split, IDE-specific build hacks) while keeping the door open for future changes (e.g. embedding the UI in REAPER's FX window, a possible Rust rewrite).

The build system has already been migrated (CMake + Ninja + VS Code, no Visual Studio/Xcode project generation needed); the actual rendering/parameter/UI code hasn't been ported off VST3 types yet, so the plugin currently only proves the pipeline works end-to-end rather than doing real video processing. See [CLAUDE.md](CLAUDE.md) for the up-to-date architecture and migration status.

## Dependencies

### CMake and Ninja

Install [CMake](https://cmake.org/) (3.21+) and [Ninja](https://ninja-build.org/). On Windows, build from a Developer Command Prompt (or let VS Code's CMake Tools extension pick an MSVC kit for you) so `cl.exe` is available to Ninja.

### CMake modules

- [cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning): vendored as a git submodule — initialize submodules (see below), CMake hard-fails at configure time if it's missing rather than fetching it automatically.

### Submodules

This repo uses git submodules for most vendored dependencies. After cloning:

```
git submodule update --init --recursive
```

(or clone with `git clone --recurse-submodules` in the first place)

### CLAP

Already vendored under `external/clap` (plain headers, MIT-licensed) — nothing to install.

### Vulkan / graphics libraries (not needed yet)

The Vulkan SDK isn't required to build the plugin today — it's only wired into `CMakeLists.txt` in preparation for porting the real renderer onto the new CLAP shell. Skip this section unless you're working on that port.

GLM, Tiny Obj Loader, STB, and Vulkan Memory Allocator are all header-only and vendored as git submodules (see above) — nothing to download or build by hand.

glslang and SPIRV-Cross are **not** vendored: install the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) (preferably in the default location) and CMake finds their headers/prebuilt static libraries directly inside the SDK install — no separate download, placement, or build step needed for either. If a given SDK's bundled version ever proves incompatible (this has happened in the past with older SDK releases), `RS_SPVC_PATH`/the glslang `find_library` hints in `CMakeLists.txt` can be overridden to point at a manually built checkout of [glslang](https://github.com/KhronosGroup/glslang) or [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) instead.

## Tasks

- **build**: builds the plugin and automatically deploys it to the user's plugin folder
  - Win: `%LOCALAPPDATA%\Programs\Common\CLAP`
  - Mac: `~/Library/Audio/Plug-Ins/CLAP`
  - Linux: `~/.clap`

- **clean**: clean build products for a fresh reconfigure

## Run

### Windows

- Make sure to have all the **VC Redist** updated to the latest version. It can be downloaded from Microsoft website.

- If REAPER doesn't list the plugin, make sure it has rescanned for CLAP plugins (Preferences → Plug-ins → Clear cache/re-scan).

## Development

See [Development](doc/Development.md).

## Credits

[License File](LICENSE)

### Author

    Emanuele Messina

#### Open source libraries

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

#### Third Party

- [CLAP](https://cleveraudio.org/) _by the CLever Audio Plug-in project_
- [Vulkan](https://vulkan.lunarg.com/) _by Khronos Group_
- [Reaper SDK](https://github.com/justinfrankel/reaper-sdk) _by Cockos_

#### Thanks to

- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
