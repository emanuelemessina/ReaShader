# ReaShader

_THE Video Processor VST for Reaper_

<br>

## Dependencies

<br>

### VST3 SDK

<br>

Clone the [vst-sdk-bundle](https://github.com/emanuelemessina/vst-sdk-bundle), it contains a copy of the vst3 sdk.

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
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) : has to be built with cmake (set the build dir to `/build`), then the project will automatically find the static library output

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

## Build

<br>

### CMake

<br>

First, build VMA for both debug and release (to have the `.lib` files).

Then, run CMake with build generation directory `/build/<os>` folder.
<br>
- For Windows: `<os>` is `win` , Compiler is **Visual Studio**
- For Mac: `<os>` is `mac` , Compiler is **Xcode**

<br>

### sln-make (_Visual Studio_)

<br>

Use [sln-make](http://github.com/emanuelemessina/sln-make) to apply Visual Studio settings.

Take ownership of `C:\Program Files\Common Files` as the plugin will be copied there after building.

<br>

---

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

---

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
