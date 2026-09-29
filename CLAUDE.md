# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository. It describes the code **as it is now**. How it got here (the VST3 → CLAP migration, rejected alternatives, past crash investigations) is in [doc/history.md](doc/history.md).

## What this is

ReaShader is a **CLAP** video-effect plugin for REAPER. It taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back. Its HTML/JS/SCSS UI is embedded in REAPER's FX window through a native webview ([webview/webview](https://github.com/webview/webview)).

## Cleanup in progress

A multi-phase cleanup is underway. The plan lives at `~/.claude/plans/picking-up-on-this-snug-aurora.md`.
- **Phases:** 1 deletions/hygiene → 2 build → 3 utilities → 4 params/state/protocol → 5 layering → 6 renderer rewrite (vkt → vk-bootstrap + plain structs) → 7 docs.
- **How it runs:** before each batch of changes, give the user a brief rationale and wait for approval. The user commits between phases.
- **Status:** phase 6 in progress.
  - **Done** (built, standalone GPU test passes, awaiting the REAPER test): the new renderer; built-in effects; `//@param`; shader params as host params through restart + rescan.
  - **Still to do:** `scene3d` (the logo easter egg, an off-by-default switch), then delete `render/vkt/` (no longer built; kept as the reference for `scene3d`).

## Proposals for after the cleanup (user's, not started)

- **Test application.** A small host that loads the plugin the way REAPER does, to verify features and stability programmatically before manual REAPER tests.
  - When a REAPER test disagrees with it, update the test app to match what REAPER actually does.
  - It must be clearly separate from the main `CMakeLists.txt` and sources.
  - Tests are standardized, and kept apart from the test app's own code.
  - The standalone GPU test from phase 6 (see Debugging) and `tests/shaders/` are natural seeds.
- **Render doc for humans.** A Markdown doc explaining the renderer in plain terms: Vulkan concepts, what each `render/` file does, and why each decision maps to how Vulkan works.
  - It is kept updated with every render change.
  - It's separate from CLAUDE.md: that doc is for humans, this file is for Claude.

## Hard rules

- **Nothing may throw out of a REAPER or CLAP callback.** REAPER treats an escaped exception as fatal (`abort()`, exception `0x40000015` "inside reaper.exe"). `ReaShaderRenderer` never throws: a Vulkan error during a frame sets `failed`, and video passes through until the next activation.
- **Never block REAPER's video thread.** `renderFrame` `try_lock`s `frameMutex`. `init`, `shutdown` and `changeRenderingDevice` hold that mutex.
- `deactivate()` deletes the video processor, so REAPER stops calling into the plugin. The renderer (GPU) stays up until the plugin is destroyed: it is created by the first `activate()`, which keeps re-activation (and the restart for a param rescan) fast.
- REAPER's `'RGBA'` frames are laid out in memory as **B,G,R,A** (byte 0 = B).
- **Encoding:** frontend files must be UTF-8 (`file rsui.html` must not say "UTF-16"). A UTF-16 `rsui.html` loaded via `file://` renders as garbage text.
- **Comments:** they describe what the code does and why, for a reader with no session context. Investigation narratives go in `doc/history.md`, not in code.
- **Commits:** the user commits. Don't run `git commit` unless asked.

## Build

There is no test suite or lint step. Verification is manual, in REAPER.

**Prerequisites**
- **CMake ≥ 3.25**: `CMakePresets.json` uses schema v6.
- **Ninja**, and **clang** (`clang++`, GNU driver; not `clang-cl` or MSVC `cl`). The Windows presets pin `clang`/`clang++`, which must be on `PATH`.
- **The Vulkan SDK:** found with `find_package(Vulkan COMPONENTS shaderc_combined)` through `VULKAN_SDK`, which its installer sets. shaderc (runtime GLSL → SPIR-V) comes from the SDK; FindVulkan picks the debug variant itself.
- **Submodules:** run `git submodule update --init --recursive`. `clap` is a plain vendored copy. Everything else in `external/` is a submodule, including `cmake-git-versioning`, and configure fails without it.
- **First configure needs network once:** `webview` fetches the WebView2 headers from NuGet if no system copy is found.

**Building**
- The VS Code **`build+deploy`** task is the default build task (Ctrl+Shift+B). It runs `cmake -DPROFILE=<debug|release> -P build.cmake`, which:
  1. configures (first time only);
  2. builds;
  3. deploys the `.clap` plus `assets/` and `rsui/` to the per-user CLAP folder (`%LOCALAPPDATA%\Programs\Common\CLAP`, `~/Library/Audio/Plug-Ins/CLAP`, `~/.clap`).

  If REAPER has the `.clap` open, the deploy is skipped with a warning (close REAPER and build again).
- The **`clean`** task wipes the preset build directory.
- **CLI alternative** (builds without deploying): `cmake --preset windows-debug`, then `cmake --build --preset windows-debug`.
- **The build itself (`CMakeLists.txt`):**
  - stages `res/images`, `res/meshes`, `src/shaders` and the `rsui` frontend (minus `styles/`) next to the `.clap`. The staged `assets/` and `rsui/` are wiped first, so no stale files are left. Shaders are compiled at runtime, so no `.spv` files exist anywhere.

  The runtime resolves `assets/` and `rsui/` relative to the plugin binary, so they must travel with it.
- **IntelliSense:** it reads `build/windows-debug/compile_commands.json`, which is written at configure time. On a fresh clone, run the build task once.
- **Warnings:** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` apply to our code, and a missing `return` is an error. `-Wno-missing-field-initializers` is set because the `VkXxxInfo info{ VK_STRUCTURE_TYPE_XXX }` idiom zeroes the rest on purpose. Third-party headers are `SYSTEM` includes and third-party sources build with `-w`. The goal is zero warnings from our code.
- Only Windows has been exercised. The macOS/Linux branches print `TODO` warnings for what's missing (the GUI, untested boxer).

**Manual testing:**
1. Load "ReaShader" (CLAP) on a track that has a video item.
2. Check the FX window shows the embedded web UI and resizes with the window.
3. Check REAPER's generic parameter list shows "Audio Gain" and "Video Param", and that they sync both ways with the web UI sliders.
4. Check REAPER's Video window shows the logo mesh composited over the video.

## Architecture

```
src/clap/plugin_entry.cpp    CLAP entry, descriptor, extension callbacks (forward to ReaShaderPlugin)
src/clap/plugin_state.h      ClapPluginState: clap_plugin_t + ReaShaderPlugin + Gui, one per instance
src/clap/gui_win32.cpp       clap.gui (Win32): Gui = container window + WebUIHost
src/clap/webui_host.*        WebUIHost: webview on its own thread, JSON bridge to the plugin (Win32)
src/plugin/plugin.*          ReaShaderPlugin: params, state, web UI messages, REAPER video tap
src/plugin/params.*          Param struct + ParamList (lock-free values)
src/render/renderer.*        ReaShaderRenderer: owns the GPU objects below, renders frames, never throws
src/render/context.*         gpu::Context: vk-bootstrap instance/device, queue, command buffer + fence, VMA
src/render/frame_targets.*   gpu::FrameTargets: upload/readback buffers + input/output images (per frame size)
src/render/shader_pass.*     gpu::compileShader (shaderc + SPIRV-Reflect) and gpu::ShaderPass (fullscreen pipeline)
src/render/gpu.*             Vulkan helpers: VK_CHECK, Buffer, Image, transition(); VMA implementation
src/render/frame_view.h      FrameView: a CPU frame (BGRA, rowBytes)
src/render/vkt/              old Vulkan toolkit, not built; reference for scene3d, to be deleted
src/util/                    logging, paths, exceptions
src/ui/                      the web UI (HTML/JS/SCSS), staged as rsui/
src/shaders/                 built-in GLSL shaders, staged as assets/shaders/
```

### ReaShaderPlugin (one object, no processor/controller split)

- **Threads** (listed in `plugin/plugin.h`):

  | Thread | Does |
  |---|---|
  | main | lifecycle, state, `onMainThread()` |
  | audio | host param events, lock-free values only |
  | video | renderer, `try_lock` only |
  | webview | UI messages, device switch, shader upload |
- **`clap.params`:**
  - Params with `automatable = true` are exposed as CLAP params. Today that is Audio Gain and Video Param.
  - **Host automation:** arrives in `process()`/`flush()` (`handleParamEvents()`) and goes into `applyHostParamValue()`, which is lock-free. It then requests a main-thread callback, and `onMainThread()` echoes the values to the web UI.
  - **Web UI edits:** flagged with `ParamList::flagForHost()`, plus `host_params->request_flush()`. They are drained into `out_events` by `takeParamChangeForHost()`, which is lock-free.
- **`clap.state`:** one JSON document: `{ version: 1, params: { name: value }, device, shader: { name, source } }`.
  - The shader source is embedded, so projects are self-contained.
  - Unknown or old state loads defaults.
  - Values of shader params are restored by name, once the shader has been recompiled.
- **Rendering device:** not a host param. It lives in state and the web UI. Changing it (from the UI or on state load) calls `changeRenderingDevice()`.
- **REAPER video tap:** `activate()` does:
  1. `host->get_extension(host, "cockos.reaper_extension")`, cast to `reaper_plugin_info_t*`;
  2. `GetFunc("clap_get_reaper_context")`: with `sel=4` it gives the FxDsp context, with `1` the parent track;
  3. `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`;
  4. `reaShaderRenderer->init()`.

  `deactivate()` deletes the video processor. The renderer stays initialized across activate/deactivate cycles; a failed renderer starts over on the next `activate()`.
- **Shaders:**
  - The current shader is `shaderName` + `shaderSource`, saved in state. It starts as `effects/default.frag`.
  - Built-in effects are the `*.frag` files in `assets/shaders/effects/` (`src/shaders/effects/`). The UI picks one by name (`shaderSelect`) or uploads a file (`shaderUpload`).
  - `_loadShader()` compiles through the renderer, and only a shader that compiles becomes current.
  - `tests/shaders/` holds shaders for manual testing, such as `broken.frag`; they are not shipped.
- **Shader params are host params (restart + rescan):**
  - CLAP allows the param list to change only while deactivated.
  - `setShaderParams()`, called from any thread, stores them as pending and requests a main-thread callback.
  - `onMainThread()`: if the plugin is inactive, it applies them now; else it calls `host->request_restart()`.
  - The host's `deactivate()` then applies them.
  - Applying means `ParamList::replaceShaderParams()` + `host_params->rescan(CLAP_PARAM_RESCAN_ALL)` + a snapshot.
  - While pending, frames give the shader's sliders their defaults.
- **Renderer access:** `ReaShaderRenderer` reaches plugin data only through `getRenderingDeviceIndex`, `setRenderingDeviceIndex`, `setRenderingDevicesList` and `setShaderParams`.

### Per-frame video path

REAPER calls `ReaShaderPlugin::_processVideoFrame` (`plugin/plugin.cpp`), installed by `activate()`:
1. `vproc->renderInputVideoFrame(0, 'RGBA')` gets the upstream frame. It is immutable, and is `Release()`d before returning.
2. Param values at video time come from `parmlist` (`[0]` = wet/dry, param `i` at `[i + 1]`), falling back to `ParamList` for params REAPER doesn't know yet.
3. `ReaShaderRenderer::renderFrame()`, under `try_lock(frameMutex)`, does:
   1. (re)creates `FrameTargets` if the size or row stride changed;
   2. `memcpy` into the mapped upload buffer, and writes the shader's `Params` into its mapped UBO;
   3. records one command buffer: buffer → input image, fullscreen shader pass → output image, output image → readback buffer;
   4. one submit and one fence wait (2 s timeout = GPU hang = `failed`);
   5. `memcpy` out into a new `vproc->newVideoFrame`.
4. If `renderFrame` returns `false` (inactive, busy or failed), the input frame is passed through unchanged.

### Embedded web UI (WebUIHost)

- **Where it runs:** the webview and its message loop run on a background thread owned by `WebUIHost`. `webview::webview`'s constructor blocks for seconds while WebView2 initializes, so the `WebUIHost` constructor just spawns that thread.
- **Creation:** `set_parent()` creates the container `HWND` and `WebUIHost` synchronously. Don't defer it: the host calls `show()` right after.
- **Cross-thread calls:** every call into the webview from another thread goes through `webview::dispatch(fn)`, because WebView2 COM objects are single-thread-affine. `Impl::mutex` guards the `webview` pointer.
- **Teardown:**
  - `webview::terminate()` is **not** cross-thread-safe on Win32: it is a bare `PostQuitMessage`.
  - So `~WebUIHost()` dispatches `terminate()` onto the webview thread.
  - It then waits with `MsgWaitForMultipleObjects(QS_SENDMESSAGE)` + `PeekMessageW`, not a plain `join()`. Child-window teardown can `SendMessage` to the UI-thread container, and a plain `join()` from the UI thread deadlocks REAPER.
- **Sizing:** the embedded widget starts at size 0, so the webview thread sizes it with `MoveWindow` after `navigate()`.
- **Transport:**
  - JS → C++: `bind("postToNative")` → `ReaShaderPlugin::handleWebUIMessage`.
  - C++ → JS: `eval()` → `window.__reashaderOnMessage`.
  - The plugin holds a `WebUISender` (`std::function`), invoked under its mutex so that `clearWebUISender()` waits for any in-flight send. It no-ops when no sender is registered.
- **Frontend:**
  - Plain sequential `<script>` tags, no ES modules: `file://` blocks module imports.
  - SCSS is compiled ahead of time (Live Sass Compile, see `.vscode/settings.json`), and the compiled `rsui.css` is committed.
- **Ownership:** `ClapPluginState::gui` is a `unique_ptr<Gui, GuiDeleter>`. `Gui` and its deleter are defined in `gui_win32.cpp`, so the shell never needs the full type. `gui_destroy` is just `gui.reset()`: the webview is torn down first, then the window.

### Web UI protocol

Plain JSON objects with a `"type"` field. They're documented in `plugin/plugin.cpp` (web UI section) and handled by an `if`/`else` on the type there. On the JS side, `client.js` switches on the type and `api.js` sends.

| Direction | Message | Payload / effect |
|---|---|---|
| to UI | `snapshot` | `{ track, params, devices, shader }`. The UI rebuilds itself from it; sent on `ready`, activate, state load, device and shader changes |
| to UI | `paramValue` | `{ id, value }`: host automation |
| to UI | `shaderStatus` | `{ status, error }` |
| from UI | `ready` | — |
| from UI | `paramValue` | `{ id, value }` |
| from UI | `renderingDevice` | `{ index }` |
| from UI | `shaderUpload` | `{ name, source }`: GLSL sent as text |

### Parameters (`plugin/params.*`)

- **`Param`:** one plain struct: id, name (the state key), label (display), group (`Main` or `Shader`), units, default, min, max, automatable. Values are plain, within min..max: the defaults are 0..1, and shader params use their `//@param` range. CLAP param info uses the same range.
- **Ids:** a param's id is its index in the list, and also its CLAP param id. The defaults (`AudioGain`, `VideoParam`) come first.
- **`ParamList`:**
  - Metadata is behind a mutex.
  - Values are a fixed array of `std::atomic<double>` (`maxCount` = 256), so the audio and video threads never lock.
  - `replaceShaderParams()` swaps the `Shader` group whenever a shader is compiled.

### Renderer (`render/`)

- **Vulkan 1.3** with dynamic rendering and synchronization2, so there are no render pass or framebuffer objects.
- **GPU list:** usable GPUs are the ones vk-bootstrap selects; the UI's device index is an index into that list.
- **Lifetimes, three tiers, each a plain struct with `create()`/`destroy()` listing its handles:**
  - `Context` (instance, device);
  - `FrameTargets` (frame size);
  - `ShaderPass` (per shader).

  No deletion queues. A device switch destroys the targets, the pass and the device, then recreates the device and the pass. The targets come back with the next frame.
- **Errors:** `VK_CHECK` throws `std::runtime_error`, and `ReaShaderRenderer`'s public functions catch everything.
- **Shader changes:** `changeShader()` compiles outside the lock, then swaps the pass under `frameMutex`. Frames render one at a time and wait on the fence, so the old pass is idle. With no device (inactive), the compiled shader is kept and installed by the next `init()`. An empty source means `assets/shaders/effects/default.frag`, also the fallback when `init()` has no shader.

### Shader contract (`shader_pass.cpp`, `kShaderPreamble`)

- **User shaders write only `main()`, plus an optional `uniform Params { ... };` block.**
- **Prepended automatically:**
  - `#version 450`;
  - `in vec2 uv` (0..1, top left = 0,0);
  - `out vec4 fragColor`;
  - `sampler2D iChannel0` (the input frame);
  - push constants `iResolution`, `iTime`, `iFrameRate`, `iFrame`, `videoParam`.

  A user `#version` is dropped, and `#extension` lines are hoisted above the preamble. `#line 1` keeps error line numbers matching the user's file.
- **`Params`:** members must be `float`/`vec2`/`vec3`/`vec4`. Each component becomes one slider, named `member` or `member.x`, in reflection order. It is auto-bound to binding 1; any other resource is rejected with an error.
- **Annotations:** `//@param member 'Label' default min max` (anywhere in the source; label and numbers optional, in that order) sets a slider's label, default and range. Without one: label = member name, 0.5, 0..1. Inspired by REAPER's video processor `//@param`, but keyed by member name, not index.
- **Keep in sync:** `gpu::ShaderInputs` must match `ReaShaderInputs` in the preamble (std430 push-constant layout, 24 bytes).

### Logging

- Use `LOG(level, toConsole | toFile | toBox, sender, title, message)` from `util/logging.h`.
- The log file is `<plugin dir>/rs.log`. It is kept open, and truncated on the first write of each process.
- **`toBox`:** message boxes are modal, so they are never shown on the calling thread.
  - `LOG` queues the box and calls the host's `request_callback()`.
  - `on_main_thread` then shows it (`showQueuedBoxes()`).
  - Each plugin instance registers the requester in `plugin_init`.
- **Paths:** use `util::paths::pluginDir()`, `assetsDir()` and `rsuiDir()` (`std::filesystem::path`). Pass `.string()` to narrow file APIs (`fopen`, `ifstream`).

## Gotchas

- **REAPER SDK:** `video_frame.h` needs `wdltypes.h` included first. `IVideoFrame::get_bits()` returns `char*`.
- **C sources:** the project only enables `CXX`. Adding a `.c` file requires `C` in `project(LANGUAGES ...)`, otherwise CMake silently skips it and you get a link error later.
- **Vulkan flags:** combine stage and usage bits into the `...Flags` type (e.g. `VkShaderStageFlags`), never the `...FlagBits` enum.
- **Windows headers:**
  - `<shellapi.h>` goes after `<windows.h>`.
  - No `NOMINMAX` is defined, so `std::max`/`std::min` break in files that include `<windows.h>`.
  - clang's GNU driver doesn't define `UNICODE`, so call the explicit `...W` functions.
- **The GUI container needs `WS_EX_CONTROLPARENT`.** REAPER's FX window is a dialog. Once the webview has keyboard focus, the dialog's tab navigation (`GetNextDlgTabItem`) starts from that focused window and climbs its parents. It can only climb back out through parents marked `WS_EX_CONTROLPARENT`, so an unmarked container makes it loop forever on the main thread (REAPER "Not Responding" at 100% CPU of one core).
- **`CreateWindowExW`** with `WS_CHILD` and a null parent fails with error 1406 (`ERROR_TLW_WITH_WSCHILD`). Create the window in `set_parent()`, not in `gui::create()`.
- **vk-bootstrap and SPIRV-Reflect** are submodules pinned to the installed SDK version (`v1.4.357`, `vulkan-sdk-1.4.357.0`). Their sources are compiled into the plugin with `-w`. `spirv_reflect.cpp` exists upstream to build the C file as C++, so the project stays C++-only.
- **GLM and VMA** stay as submodules. The SDK's GLM is older and doesn't compile with this code.
- **Build warnings:** a bizarre native crash is usually a compiler warning that got ignored. Grep the build log for `-Wreturn-type`/`-Wuninitialized` before suspecting the toolchain.

## Debugging native crashes/hangs (no WinDbg needed)

- **Crash dumps:**
  - WER writes full dumps to `%LOCALAPPDATA%\CrashDumps\reaper.exe.<pid>.dmp`.
  - List crash records with `Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'}`.
  - Open a dump with `lldb -c <dmp>`, then run `thread list` / `bt all`.
  - If a stack won't unwind, run `memory read --format A --count 3000 $rsp` and look for `_CxxThrowException` and return addresses inside `ReaShader.clap`.
- **Symbolizing:** run `llvm-symbolizer --obj=build/windows-debug/ReaShader.clap` on `(addr - module base + 0x180000000)`. This is only valid if the binary hasn't been rebuilt since the crash.
- **Hangs:** run `"" | lldb -p <pid> -o "bt all" -o "process detach" -o quit` (not `--batch`). Get the module base from `(Get-Process -Id <pid>).Modules`.
- **GPU faults:** recurring `nvlddmkm` events in the System log mean the Vulkan code is doing something invalid.
- **Vulkan validation output:**
  - Debug builds request the Khronos validation layer (if installed). Its warnings and errors go to `rs.log` through vk-bootstrap's debug messenger.
  - The log has none in normal use, so any is a bug.
  - `VK_LOADER_DEBUG=layer` shows whether the layer was loaded.
- **Testing the GPU code without REAPER:** a small executable that compiles `render/{gpu,context,frame_targets,shader_pass}.cpp` plus `util/` and runs frames through `FrameTargets` + `ShaderPass` checks output pixels exactly. This is how the renderer rewrite was verified, on both GPUs of the dev machine.
