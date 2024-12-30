# ReaShader

## Motivation

Reaper is a great and versatile DAW, capable of handling not just audio but also video.
\
While it has its own video processing capabilities, currently (2024) the features are limited and the effects must be written by hand as custom scripts accessing an internal API.
\
\
Thus, Reashader is my own experiment in trying to make a VST that acts as a video processor for Reaper.
\
You install it the same way you would install a VST, and it will process video frames instead of audio samples.
\
\
It's a work in progress, proofs of concept are available in [Releases](https://github.com/emanuelemessina/ReaShader/releases).
\
Currently i've created a web based UI (instead of the default VSTGUI, given the complexity of the interface), and i use Vulkan to process the frames. 

<br>

## Dependencies

<br>

### VST3 SDK

<br>

Obtain a copy of the VST3 SDK from Steinberg.

<br>

### VULKAN SDK

<br>

Donwload [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) and install (preferably in the default location).

<br>

### Graphics libraries

<br>

The following libraries can be placed them next to the `x.x.x.x` folder inside the Vulkan install location as CMake has the include paths defaulted to there.

- [GLM](https://github.com/g-truc/glm)
- [Tiny Obj Loader](https://github.com/tinyobjloader/tinyobjloader)
- [STB](https://github.com/nothings/stb)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) : to be built with cmake (both debug and release)
- [glslang](https://github.com/KhronosGroup/glslang) has to be built with cmake (set the build dir to `/build`) (both debug and release!), then the project will automatically find the static library file paths. _(Vulkan sdk has it but the version is obsolete and has conflicts)_

<br>

### Other libraries

<br>

The following libraries can be placed under `/external` , as they are logically source code

- [nlohmann-json](https://github.com/nlohmann/json)

<br>

### CMake modules

<br>

The following CMake modules are required

- [cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning)

<br>

## Build steps

<br>

### CMake

<br>

Run CMake with build generation directory `/build/<os>` folder.
<br>
- For Windows: `<os>` is `win` , Compiler is **Visual Studio**
- For Mac: `<os>` is `mac` , Compiler is **Xcode**

Then build from the generated solution with the related IDE.

<br>

### sln-make (_Visual Studio_)

<br>

Use [sln-make](http://github.com/emanuelemessina/sln-make) to apply Visual Studio settings.

Take ownership of `C:\Program Files\Common Files` as the plugin will be copied there after building.

<br>


## Run

<br>

### Windows

<br>

- Make sure to have all the **VC Redist** updated to the latest version. It can be downloaded from Microsoft website.

- Make sure to have the latest version of **Vulkan**. It's automatically shipped with the graphics driver, so update it if needed. Check the vulkan version with `vulkaninfo`.

- If the plugin is still not recognised by Reaper, try running `validator.exe` on the actual `.vst3` and search for messages like `"exception"` or `"reashader crashed"`.

<br>

## Development

<br>

See [Development](doc/Development.md).

<br>


## Credits

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

- [VST](https://www.steinberg.net/developers/) _by Steinberg Media Technologies GmbH_
- [Vulkan](https://vulkan.lunarg.com/) _by Khronos Group_
- [Reaper SDK](https://github.com/justinfrankel/reaper-sdk) _by Cockos_

<br>

#### Thanks to

<br>

- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
