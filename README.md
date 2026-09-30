<h1 align="center"><img src="res/images/reashader-logo-hr.png" alt="ReaShader" width="600"></h1>

## Motivation

REAPER is a great, versatile DAW, and it edits video as well as audio. For many musicians and creators, that makes it the one place where a whole project lives: the song, the mix and the music video on the same timeline.

REAPER's own video effects, though, are limited. A custom effect means writing an EEL2 script against REAPER's video API, a language no other graphics tool shares. Meanwhile, the rest of the graphics world writes effects as **shaders**: small programs that run on the GPU, are fast enough for real time, and are shared by the thousands online.

ReaShader brings the two together. It's a plugin you add to a track like any audio effect, but it processes video frames instead of audio samples:

- **Effects are shaders.** Write one, or adapt one you found, and upload it. The plugin compiles it and runs it on your GPU.
- **Shader params are REAPER params** Every parameter a shader declares becomes a REAPER parameter, so it can be automated, modulated or linked like any plugin knob.
- **Color grading with LUTs.** Load a `.cube` LUT, the format most grading tools export, and apply it before or after the shader, or from inside it.

It started as my own experiment in making a video processor for REAPER, and grew into a full plugin. It took a lot of blood, sweat and tears: if you benefit from it, please cite me. Thank you 🙏

## What it does

- A [CLAP](https://cleveraudio.org/) plugin for REAPER that runs the track's video through a chain of GLSL fragment shaders and LUTs on the GPU, with Vulkan.
- The plugin window is a web UI embedded in REAPER's FX window. From it you:
  - build the chain: add shaders and LUTs (up to 16), reorder, bypass, swap or remove them;
  - upload new shaders and `.cube` LUTs;
  - move each shader's sliders, and blend each LUT with its Mix;
  - choose the GPU.
- Every slider is also a host parameter, so it can be automated in REAPER. Each shader or LUT in the chain has a letter, shown on its card, and its parameters are named with it in REAPER (`[B] brightness: Brightness`), so the same shader twice can be told apart. The letter stays with it, and so does its automation, when you reorder the chain.
- With an empty chain, the video passes through unchanged.

It's a work in progress. Only Windows is supported for now; macOS/Linux builds are planned (the code has `TODO`s where platform work is missing).

## Installing

**With the installer (Windows):** run `ReaShader-<version>-win64-setup.exe` from a release. It installs for your user only, needs no admin rights, and installs the VC++ runtime if it's missing. Upgrading keeps the shaders and LUTs you uploaded, and uninstalling asks whether to delete them.

**By hand:** put the `ReaShader` plugin folder (from a release, or [built from source](doc/building.md)) inside your CLAP folder. Hosts search it recursively.

- Windows: `%LOCALAPPDATA%\Programs\Common\CLAP\ReaShader`
- macOS: `~/Library/Audio/Plug-Ins/CLAP/ReaShader`
- Linux: `~/.clap/ReaShader`

ReaShader needs Vulkan (from your GPU driver) and, on Windows, WebView2 (built into Windows 11).

## Using it

1. In REAPER, add "ReaShader" (CLAP) to a track with a video item. If it's not listed, rescan: Preferences → Plug-ins → CLAP → Re-scan.
2. Open REAPER's video window (View → Video).
3. Under **Chain**, upload a shader with the folder button next to "Add a shader": try the examples in `resources/shaders/examples` inside the plugin folder. An uploaded shader is compiled once, added to the shader list, and appended to the chain.
4. Optionally, upload a `.cube` LUT (3D or 1D) the same way, next to "Add a LUT". It's read once, added to the LUT list, and appended to the chain.
5. Build the chain. The video goes through it top to bottom, and each card is one step:
   - **↑ / ↓** move it: a LUT before a shader (e.g. a camera Log to Rec.709 conversion, so the shader works on normal video), or after it (e.g. a creative look over the result);
   - **Bypass** leaves it out, keeping its settings;
   - **×** removes it; the drop-down in its header swaps it for another stored shader or LUT;
   - a LUT's **Mix** blends between its input (0%) and the LUT's result (100%);
   - a shader's **LUT (iLut)** hands a LUT to the shader itself, which decides how to use it through `iLut()` (try `lut_split.frag`). It shows only for shaders that use a LUT.

   Pick a stored shader or LUT from "Add a shader" / "Add a LUT" to append it again: the same one can be in the chain more than once, each with its own settings.

   Removing a node frees its letter for the next node you add. REAPER keeps the removed node's envelopes and parameter modulation on that letter's parameters, and they will drive the next node that gets it: delete them in REAPER when you remove an automated node. In REAPER these are separate: deleting an envelope lane doesn't remove an LFO or other modulation, which you turn off in its own window (Param → Parameter modulation/MIDI link).

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
    luts/                    LUTs read on upload (the LUT list)
  rs.log                     the plugin's log
```

## Documentation

For users:

- [Writing a shader](src/shaders/examples/README.md): the built-in inputs, sliders and `//@param`, and using the LUT.

For developers:

- [Building](doc/building.md): prerequisites, building and deploying, packaging the installer.
- [Contributing](CONTRIBUTING.md): the workflow, runtime rules, code style and documentation rules.
- [Architecture](doc/architecture.md): how the plugin, the web UI and the renderer fit together.
- [Rendering](doc/rendering.md): the Vulkan renderer, explained for readers who don't know Vulkan.
- [Testing](doc/testing.md): the test application and the manual test in REAPER.
- [Gotchas](doc/gotchas.md): platform and toolchain traps, and debugging crashes and hangs.

## Credits

[License File](LICENSE)

### Author

[Emanuele Messina](https://www.linkedin.com/in/emanuelemessina-em)

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
- [doctest](https://github.com/doctest/doctest) (tests)

#### Thanks to

- Sascha Willems : [Github](https://github.com/SaschaWillems/Vulkan)
- Victor Blanco : [Vulkan Guide](https://vkguide.dev/)
