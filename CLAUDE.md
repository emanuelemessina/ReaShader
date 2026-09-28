# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ReaShader is a video-effect plugin for REAPER that runs a custom Vulkan rendering pipeline (GLSL shaders) over REAPER's video track frames. It ships with an embedded local web server that serves a JS/HTML/SCSS UI for controlling shaders, parameters, and GPU selection at runtime.

## Migration status: VST3 → CLAP (read this before touching the build or `source/vst3`)

The project is mid-migration from VST3 to **CLAP** (a lighter, C-ABI, header-only plugin format). This was a deliberate move, not a whim: CLAP was confirmed (empirically, not just from docs) to reach REAPER's video-processing tap just as well as VST3 does, and dropping the VST3 SDK removes the entire source of build-system pain (bundle folder structure, `moduleinfotool`/validator, processor/controller split, the old Visual-Studio-only `sln-make.inix` hacks). It also sets up two things planned for later: a possible rewrite in Rust, and embedding the web UI inside REAPER's FX window via CLAP's `gui` extension instead of a separate browser window.

- **Phase A (done)**: build system migrated to CLAP — CMake + Ninja + VS Code, no Visual Studio, no `sln-make`. The active build target is only the thin shell at [source/clap/reashader_clap.cpp](source/clap/reashader_clap.cpp), which proves the REAPER video tap works from CLAP but does **not** yet call into the real renderer (it paints a diagnostic animated test pattern instead — see the file's header comment).
- **Phase B (not started)**: port `ReaShaderProcessor`/`ReaShaderRenderer`/`rsparams`/`rscontroller`/`rsui` (described under Architecture below) off Steinberg VST3 types (`Vst::ParamID`, `Vst::ParamValue`, `IBStream`, `FUnknown`, ...) onto the CLAP shell, and wire the real Vulkan renderer into the CMake target.
- `source/vst3/` and the VST3 SDK dependency are **no longer part of the build** but are kept in the tree, untouched, as reference for the Phase B port (e.g. the `IBStreamer`-based param serialization logic). They'll be deleted once Phase B is complete — don't be surprised that they exist but aren't referenced by `CMakeLists.txt`.
- The REAPER video-tap access path confirmed for CLAP (different from VST3's `IReaperHostApplication::getReaperParent`): `clap_host_t::get_extension(host, "cockos.reaper_extension")` → cast to `reaper_plugin_info_t*` → `GetFunc("clap_get_reaper_context")` → call with `sel=4` ("FxDsp") → that pointer is the `fxctx` for `GetFunc("video_CreateVideoProcessor")(fxctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION)`. REAPER's `'RGBA'` video pixel format is packed in memory as **B,G,R,A** (byte0 = B) — build pixel values via `(a<<24)|(r<<16)|(g<<8)|b`, not hex literals (confirmed by an initial off-by-color-channel bug during testing).

## Build

There is no test suite or lint step. Verification is manual, in REAPER, using the project in `test/`.

### Prerequisites

- **CMake ≥ 3.21** and **Ninja**.
- **[cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning)** module, cloned anywhere — its path must be supplied as `RS_CGV_PATH` (either as a `-D` CMake cache variable, or via the `REASHADER_CGV_PATH` environment variable, which [CMakePresets.json](CMakePresets.json) reads automatically).
- **CLAP headers** are vendored under `external/clap` (plain copied headers, MIT-licensed, no build step, not a submodule) — nothing to install.
- Everything else currently in `external/` (`reaper-sdk`, `restinio`, `boxer`, `cwalk`, `json`) is wiring left over for Phase B; the Phase A build doesn't touch it. The Vulkan SDK and the manually-placed GLM/VMA/tinyobjloader/STB/glslang/SPIRV-Cross libraries (see the CMake cache variables `RS_GLM_PATH` etc. near the top of `CMakeLists.txt`) are **not required** to build today — they'll matter again once Phase B wires in the real renderer.

### Configure & build

Either open the repo in VS Code with the CMake Tools extension (it will read `CMakePresets.json` and prompt for a kit/toolchain), or from the CLI:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

(`macos-debug`/`linux-debug` presets exist too, untested — Windows is the only platform actually exercised so far). On Windows, run this from a Developer Command Prompt (or after `vcvarsall.bat`) so `cl.exe` is on `PATH` for Ninja to find; VS Code's CMake Tools does this automatically via its kit selection.

This single `cmake --build` step does everything `sln-make.inix` + `scripts/win/*.ps1` used to do by hand, generator-agnostically via plain `add_custom_command()`s in `CMakeLists.txt`:
- compiles `source/shaders/*.glsl` to SPIR-V via `glslc` if it's found (skipped with a warning otherwise — the precompiled `.spv` files already committed under `source/shaders` are used as a fallback; nothing in the Phase A build actually consumes them yet)
- stages `resource/images`, `resource/meshes`, `source/shaders`, and the `rsui` frontend (minus `styles/`) next to the built plugin
- deploys the built `.clap` to the **per-user** CLAP plugin folder (`%LOCALAPPDATA%\Programs\Common\CLAP` on Windows, `~/Library/Audio/Plug-Ins/CLAP` on macOS, `~/.clap` on Linux) — no admin rights needed, unlike the old VST3 flow's system-folder deploy.

### Manual testing

Load "ReaShader" as a CLAP plugin on a REAPER track's FX chain and open REAPER's Video window. Since Phase B hasn't wired in the real renderer yet, expect the diagnostic pattern described in `source/clap/reashader_clap.cpp`'s header comment (a color that cycles R/G/B every 2s + a sweeping bar, both driven by REAPER's `project_time`) — not real video processing yet.

## Architecture

Everything below describes `source/reashader/` (and `source/vst3/`) as they exist today: **not currently part of the active build** (see Migration status above), but the real logic that Phase B will port onto the `source/clap/` shell — read it as a map of what needs porting and how the pieces fit together, not as a description of what `cmake --build` currently produces.

### Two VST3 objects, two owners of the real logic

The mandatory Steinberg classes in `source/vst3/` (`MyPluginProcessor`, `MyPluginController`) are thin shells. All real behavior lives in `source/reashader/`:

- **`ReaShaderProcessor`** ([rsprocessor.h](source/reashader/rsprocessor.h)) — owned by the audio-processor-side object. Registers REAPER's `IREAPERVideoProcessor::process_frame` callback, owns the `ReaShaderRenderer`, owns the "processor-side" parameter list, handles VST parameter automation and state save/load.
- **`ReaShaderController`** ([rscontroller.h](source/reashader/rscontroller.h)) — owned by the controller-side object. Creates the VST editor view (which embeds the web UI) and starts the embedded `RSUIServer`.

These run in **separate VST3 objects/processes-in-spirit** (Steinberg's processor/controller split), so the web UI (attached to the controller) and the actual Vulkan rendering (in the processor) talk to each other only through VST3's inter-plugin messaging (`IMessage`, handled in `notify`/`receiveText` in [mypluginprocessor.cpp](source/vst3/mypluginprocessor.cpp)) plus a JSON/binary protocol. Never assume the controller can call directly into renderer state — it can't; everything crosses that boundary as a message.

### Per-frame video path

REAPER calls `processVideoFrame` ([rsprocessor.cpp](source/reashader/rsprocessor.cpp)) once per video frame:
1. `ReaShaderRenderer::loadBitsToImage` uploads REAPER's raw pixel buffer into a Vulkan image.
2. `ReaShaderRenderer::updateVirtualScene` + `drawFrame` run the Vulkan render pass (custom fragment shader, optional 3D scene, post-process pass).
3. `ReaShaderRenderer::transferFrame` copies the result back into REAPER's buffer.

### Vulkan layer (`vkt/` namespace)

A hand-rolled Vulkan wrapper toolkit used exclusively by `ReaShaderRenderer` ([rsrenderer.h](source/reashader/rsrenderer.h)). Key convention (see [doc/Development.md](doc/Development.md)):

- Every `vkt` object pushes its own cleanup into a `vkt::deletion_queue` supplied at construction — **never delete `vkt` objects manually**. `ReaShaderRenderer` keeps four separate queues (`vktMain`, `vktFrameResized`, `vktPhysicalDeviceChanged`, `vktCustomShaderChanged`) flushed at the appropriate granularity so a GPU switch or shader hot-swap doesn't tear down the whole context.
- All raw `Vk*` handles are initialized to `VK_NULL_HANDLE`.
- `changeCustomShader` recompiles GLSL to SPIR-V on the fly (glslang) and rebuilds only the pipeline, not the whole renderer, using `vktCustomShaderChanged`.
- `changeRenderingDevice` lets the user switch physical GPU at runtime (device list is enumerated and sent to the web UI).

### Web UI protocol (`rsui/`)

- [api.h](source/reashader/rsui/api.h) defines the JSON message contract shared conceptually between C++ and JS: a `MessageType`/`RequestType` enum pair, a fluent `MessageHandler` (`.reactToX(...).reactToY(...).fallbackWarning(...)`) for parsing incoming messages, and `MessageBuilder` for constructing outgoing ones. When adding a new message type, update the enum, `typeStrings`, and add both a `reactTo*` and a `build*` method — the two sides are matched purely by the `"type"` string field.
- [backend/backend.h](source/reashader/rsui/backend/backend.h) (`RSUIServer`) is a RESTinio-based embedded HTTP/WebSocket server owned by `ReaShaderController`. Binary WebSocket frames prefixed with the `"RS"` magic sequence are file uploads, reassembled by `FileUploadProcess` (chunked, with a timeout watchdog thread) — used e.g. for uploading a custom shader.
- Frontend lives in `source/reashader/rsui/frontend/` (plain HTML/CSS/JS + SCSS partials, no build tooling/bundler — SCSS is compiled ahead of time, e.g. via the Live Sass Compile VS Code extension configured in [.vscode/settings.json](.vscode/settings.json)).
- **Restinio header note**: restinio's headers conflict with `Windows.h` if not carefully ordered. `RSUIServer`'s public header ([backend.h](source/reashader/rsui/backend/backend.h)) uses `std::any` to hide restinio types entirely; restinio types only appear inside [rsuiserver.cpp](source/reashader/rsui/backend/rsuiserver.cpp)/`wshandler.cpp`. Follow this pattern for any new server-side code — do not leak restinio types into headers included elsewhere.

### Custom parameter system (`rsparams/`)

On top of plain VST3 parameters, `Parameters::IParameter` ([rsparams.h](source/reashader/rsparams/rsparams.h)) is a polymorphic, JSON- and `IBStreamer`-serializable parameter type, so the web UI can define/add parameters dynamically (not just ones registered as VST params at startup). `TypeInstantiator` is a factory keyed by `Parameters::Type`; new parameter subclasses must be registered in `_registerParameterInstantiator` and must implement the `...Derived` virtuals (`toJsonDerived`, `fromJsonDerived`, `serializeDerived`, `deserializeDerived_v1`, `setValueFromJson`). `VSTParameter` is the subclass that's actually bound to a real Steinberg VST parameter and can be automated from the DAW; other types (e.g. `Int8u`, `String`) are UI/renderer-only.

`ReaShaderProcessor` and `ReaShaderController` each keep their **own** parameter vector (`processor_rsParams`, `controller_rsParams`) — they're kept in sync via the JSON messaging protocol, not shared memory.

### Logging / errors

Use `LOG(...)` from [tools/logging.h](source/reashader/tools/logging.h) (destinations are OR'd flags like `toConsole | toFile`) rather than ad-hoc `std::cout`/`OutputDebugString`. Vulkan calls that return a `VkResult` should be wrapped in `VK_CHECK_RESULT(...)` ([vktcommon.h](source/reashader/vkt/vktcommon.h)), which throws `std::runtime_error` on failure — low-level Vulkan init failures are caught and guarded in `ReaShaderRenderer::_initVulkanGuarded`/`_cleanupVulkanGuarded` to avoid SEH unwinding issues.
