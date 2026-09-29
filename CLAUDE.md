# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository. It describes the code **as it is now**. How it got here (the VST3 → CLAP migration, rejected alternatives, past crash investigations) is in [doc/history.md](doc/history.md).

## What this is

ReaShader is a **CLAP** video-effect plugin for REAPER. It taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back. Its HTML/JS/SCSS UI is embedded in REAPER's FX window through a native webview ([webview/webview](https://github.com/webview/webview)).

The renderer is explained for humans in [doc/rendering.md](doc/rendering.md): the Vulkan concepts mapped to `src/render/`, every barrier of a frame, lifetimes and decisions.

## Proposals (the user's, not started)

A **test application**: a fake REAPER host plus standardized tests, separate from the main build (the seed is `test/seed/`). The full handoff, with requirements, what REAPER does to the plugin, the suggested design and open questions, is in [doc/proposals.md](doc/proposals.md).

## Hard rules

- **Nothing may throw out of a REAPER or CLAP callback.** REAPER treats an escaped exception as fatal (`abort()`, exception `0x40000015` "inside reaper.exe"). `ReaShaderRenderer` never throws: a Vulkan error during a frame sets `failed`, and video passes through until the next activation.
- **Never block REAPER's video thread.** `renderFrame` `try_lock`s `frameMutex`. `init`, `shutdown` and `changeRenderingDevice` hold that mutex.
- `deactivate()` deletes the video processor, so REAPER stops calling into the plugin. The renderer (GPU) stays up until the plugin is destroyed: it is created by the first `activate()`, which keeps re-activation (and the restart for a param rescan) fast.
- REAPER's `'RGBA'` frames are laid out in memory as **B,G,R,A** (byte 0 = B).
- **Encoding:** frontend files must be UTF-8 (`file index.html` must not say "UTF-16"). A UTF-16 `index.html` loaded via `file://` renders as garbage text.
- **Render doc:** any change in `src/render/` or `src/shaders/internal/` updates [doc/rendering.md](doc/rendering.md) in the same batch (barrier table, lifetimes, decisions). It is written for humans who don't know Vulkan, and references code by file and function, never by line.
- **Comments:** they describe what the code does and why, for a reader with no session context. Investigation narratives go in `doc/history.md`, not in code.
- **Changes:** before a batch of changes, give the user a brief rationale and wait for approval.
- **Commits:** the user commits. Don't run `git commit` unless asked.

## Build

There is no test suite or lint step. Verification is manual, in REAPER.

**Prerequisites**
- **CMake ≥ 3.25**: `CMakePresets.json` uses schema v6.
- **Ninja**, and **clang** (`clang++`, GNU driver; not `clang-cl` or MSVC `cl`). The Windows presets pin `clang`/`clang++`, which must be on `PATH`.
- **The Vulkan SDK:** found with `find_package(Vulkan COMPONENTS shaderc_combined)` through `VULKAN_SDK`, which its installer sets. shaderc (runtime GLSL → SPIR-V) comes from the SDK; FindVulkan picks the debug variant itself.
- **Submodules:** run `git submodule update --init --recursive`. Everything in `external/` is a submodule (CLAP pinned to `1.2.10`), including `cmake-git-versioning`, and configure fails without it.
- **First configure needs network once:** `webview` fetches the WebView2 headers from NuGet if no system copy is found.
- **Dart Sass** (`sass`) on `PATH`: the build compiles the UI's SCSS, and configure fails with an install hint without it (`choco install sass` / `npm install -g sass` / `brew install sass/sass/sass`).
- **Packaging only: Inno Setup 6** (`choco install innosetup`, or `winget install --id JRSoftware.InnoSetup --scope user`). `ISCC` is looked up in Program Files, Program Files (x86) and `%LOCALAPPDATA%\Programs`. It also needs a Visual Studio or Build Tools install, for `vc_redist.x64.exe`.

**Building**
- The VS Code **`build+deploy`** task is the default build task (Ctrl+Shift+B). It runs `cmake -DPROFILE=<debug|release> -P build.cmake`, which:
  1. configures (first time only);
  2. builds;
  3. deploys the `.clap` plus `resources/` and `ui/` to its own folder in the per-user CLAP folder (`%LOCALAPPDATA%\Programs\Common\CLAP`, `~/Library/Audio/Plug-Ins/CLAP`, `~/.clap`). Hosts search CLAP folders recursively (`clap/entry.h`).

  **Debug and release are separate plugins**, so both can be installed side by side. `CMakeLists.txt` sets the identity from `CMAKE_BUILD_TYPE` (`REASHADER_NAME`/`REASHADER_ID` defines, `OUTPUT_NAME`), and `build.cmake` reads the file name from the build tree's cache (`PLUGIN_FILE_NAME`):

  | | Release | Debug |
  |---|---|---|
  | Name in REAPER | `ReaShader` | `ReaShader (Debug)` |
  | CLAP id | `com.emanuelemessina.reashader` | `com.emanuelemessina.reashader.debug` |
  | Binary / deploy folder | `ReaShader.clap` in `ReaShader/` | `ReaShader-Debug.clap` in `ReaShader-Debug/` |

  Projects saved with one don't load the other.

  If REAPER has the `.clap` open, the deploy is skipped with a warning (close REAPER and build again).
  Deploy replaces `resources/images`, `resources/meshes`, `resources/shaders/examples` and `ui` one by one, so `resources/shaders/compiled` (the user's uploaded shaders) survives.
- The **`package`** task builds the release preset, then the installer: `cmake -DPROFILE=release -DPACKAGE=ON -P build.cmake` runs CPack instead of deploying (release only, since the debug build needs the non-redistributable debug CRT). The output is `build/windows-release/package/ReaShader-<tag>-win64-setup.exe`. See Packaging.
- The **`clean`** task wipes the preset build directory.
- **CLI alternative** (builds without deploying): `cmake --preset windows-debug`, then `cmake --build --preset windows-debug`.
- **The build itself (`CMakeLists.txt`):**
  - **generates, into `build/<preset>/generated/`:**
    - `src/shaders/internal/*` → `<name>.inc`, SPIR-V as a C array (glslc `-mfmt=num`), `#include`d by the renderer. Internal shaders are never read from disk.
    - `src/ui/styles/ui.scss` → `index.css` (compressed, `sass`). The CSS is a build output, not committed.
  - **stages next to the `.clap`** (the `stage` target, run on every build, so UI or shader edits alone get staged too): `res/images`, `res/meshes`, `src/shaders/examples` → `resources/shaders/examples`, and `src/ui` (minus `styles/`, plus `index.css`) → `ui`. The staged `resources/` and `ui/` are wiped first, so no stale files are left.
  - **Examples are sources for the user to try;** the plugin never reads that folder. User shaders are compiled only on upload (see Shaders), so no `.spv` files exist in the repo.

  The runtime resolves `resources/` and `ui/` relative to the plugin binary, so they must travel with it.
- **IntelliSense:** it reads `build/windows-debug/compile_commands.json`, which is written at configure time. On a fresh clone, run the build task once.
- **Warnings:** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` apply to our code, and a missing `return` is an error. `-Wno-missing-field-initializers` is set because the `VkXxxInfo info{ VK_STRUCTURE_TYPE_XXX }` idiom zeroes the rest on purpose. Third-party headers are `SYSTEM` includes and third-party sources build with `-w`. The goal is zero warnings from our code.
- Only Windows has been exercised. The macOS/Linux branches print `TODO` warnings for what's missing (the GUI, untested boxer).

**Packaging** (`CMakeLists.txt` Install + Package, `installer/windows/`):
- **One native installer per OS, from one CMake description:** `install()` rules lay out the plugin folder (`.clap`, `resources/`, `ui/`), and a CPack generator per OS builds the installer. Only Windows exists. macOS (a `.clap` bundle with its resources inside, a signed `.pkg`) and Linux (`TGZ`, untested) are TODOs.
- **Windows: CPack's Inno Setup generator.**
  - **Where it installs:** per user, into `%LOCALAPPDATA%\Programs\Common\CLAP\<PLUGIN_FILE_NAME>`, with no admin. The folder must be writable for uploaded shaders. `AppId` is the CLAP id.
  - **Upgrades:** `reashader.iss` (`[InstallDelete]`) replaces the shipped folders like the deploy does, and keeps `resources/shaders/compiled`.
  - **Uninstall:** `installer.pas` asks whether to delete the uploaded shaders (a silent uninstall keeps them), then removes the folders left empty.
  - **VC++ runtime:** the release binary imports `MSVCP140`/`VCRUNTIME140`. A static CRT isn't possible because the SDK's `shaderc_combined` is `/MD`. So CMake finds the newest `VC/Redist/MSVC/<ver>/vc_redist.x64.exe` and passes its path and version as `CPACK_INNOSETUP_DEFINE_*`. `installer.pas` installs it when the registry's runtime (`VisualStudio\14.0\VC\Runtimes\x64`) is missing or older than that toolset.
  - **Tasks page:** CPack always emits a "desktop icon" task, so `installer.pas` skips the tasks page.
  - **What the target machine needs:** Vulkan (from the GPU driver) and WebView2 (built into Windows 11).
  - **Debugging the installer:** the generated script is `build/windows-release/package/_CPack_Packages/win64/INNOSETUP/ISScript.iss`.

**Manual testing:**
1. Load "ReaShader" (CLAP) on a track that has a video item.
2. Check the FX window shows the embedded web UI and resizes with the window.
3. With no shader (the initial state) video passes through unchanged.
4. Upload `resources/shaders/examples/*.frag` from the plugin folder: each appears in the shader list, its sliders appear, and REAPER's generic parameter list shows "Audio Gain" plus the shader's params, synced both ways with the web UI sliders.
5. Upload `test/shaders/broken.frag`: the compile error shows in the UI, and the current shader stays.
6. Click the UI's logo: the about box opens and the 3D logo spins in the video window; closing it removes the logo.

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
src/render/shader_compiler.* gpu::compileShader (contract preamble, shaderc, SPIRV-Reflect, //@param), stored JSON form
src/render/shader_pass.*     gpu::ShaderPass: fullscreen pipeline for a compiled shader
src/render/scene.*           gpu::Scene: textured meshes (tinyobjloader, stb) + depth, drawn over the frame; today the logo
src/render/gpu.*             Vulkan helpers: VK_CHECK, Buffer, Image, createPipeline(), transition(); VMA implementation
src/render/frame_view.h      FrameView: a CPU frame (BGRA, rowBytes)
src/util/                    logging, paths, shell (openUrl)
installer/windows/           Inno Setup extras for the CPack installer: reashader.iss (sections), installer.pas (code)
src/ui/                      the web UI: index.html, scripts/{api,ui,client}.js, styles/ui.scss; staged as ui/
src/shaders/examples/        example shader sources + README for users, staged as resources/shaders/examples/
src/shaders/internal/        shaders built into the plugin (fullscreen.vert, scene.vert/.frag), compiled by glslc at build time
test/shaders/               shaders for manual testing (broken.frag), not shipped
test/seed/                  standalone GPU test (own CMake project, not part of the plugin build)
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
  - Params with `automatable = true` are exposed as CLAP params: Audio Gain (host-only, not in the web UI) and every shader param.
  - **Host automation:** arrives in `process()`/`flush()` (`handleParamEvents()`) and goes into `applyHostParamValue()`, which is lock-free. It then requests a main-thread callback, and `onMainThread()` echoes the values to the web UI.
  - **Web UI edits:** flagged with `ParamList::flagForHost()`, plus `host_params->request_flush()`. They are drained into `out_events` by `takeParamChangeForHost()`, which is lock-free.
- **`clap.state`:** one JSON document: `{ version: 2, params: { name: value }, device, logo, shader: { name, compiled } }`.
  - The compiled shader (its stored JSON) is embedded, so projects are self-contained and never recompile.
  - Unknown or old state loads defaults.
  - Values of shader params are restored by name, once the shader is loaded.
- **Logo (easter egg):**
  - `showLogo`, off by default, saved in state as `logo`. Not a host param.
  - The UI's header logo opens the about box, which sends `logo { enabled: true }`. Closing it (×, a click outside, Escape) sends `false`. The box is open whenever the snapshot's `logo` is true.
  - It calls `ReaShaderRenderer::setLogoEnabled()`.
- **Version:** `REASHADER_VERSION`, a compile definition from the last git tag (`PROJECT_VERSION_STRING` of cmake-git-versioning). It is used by the CLAP descriptor and the snapshot (about box).
- **Rendering device:** not a host param. It lives in state and the web UI. Changing it (from the UI or on state load) calls `changeRenderingDevice()`.
- **REAPER video tap:** `activate()` does:
  1. `host->get_extension(host, "cockos.reaper_extension")`, cast to `reaper_plugin_info_t*`;
  2. `GetFunc("clap_get_reaper_context")`: with `sel=4` it gives the FxDsp context, with `1` the parent track;
  3. `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`;
  4. `reaShaderRenderer->init()`.

  `deactivate()` deletes the video processor. The renderer stays initialized across activate/deactivate cycles; a failed renderer starts over on the next `activate()`.
- **Shaders:**
  - **Compiled once, on upload (the only place):** `_uploadShader()` runs `gpu::compileShader`, writes `resources/shaders/compiled/<stem>.json`, then uses it.
  - **The shader list** is the `*.json` files in `util::paths::compiledShadersDir()`. `shaderSelect` loads one, and `""` = none.
  - **The current shader** is `shaderName` + `shaderData` (the stored JSON), saved in state. There is none at start: video passes through.
  - **Loading:** `_useShader()` parses the stored JSON (`gpu::fromJson`) and hands it to `ReaShaderRenderer::setShader()`. `_clearShader()` unloads.
  - **Status:** every outcome goes to the UI as `shaderStatus`: `Loaded <name>` or the error. The UI itself shows `busy` (spinner) while it waits.
  - **Caveat:** writing into the plugin folder needs it to be writable. The per-user CLAP folder is; a system-wide install might not be.
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
   3. records one command buffer: buffer → input image → shader pass (or a plain copy when there's no shader) → output image → logo scene on top (if enabled) → readback buffer;
   4. one submit and one fence wait (2 s timeout = GPU hang = `failed`);
   5. `memcpy` out into a new `vproc->newVideoFrame`.
4. If `renderFrame` returns `false` (inactive, busy, failed, or no shader and no logo), the input frame is passed through unchanged.

### Embedded web UI (WebUIHost)

- **Where it runs:** the webview and its message loop run on a background thread owned by `WebUIHost`. `webview::webview`'s constructor blocks for seconds while WebView2 initializes, so the `WebUIHost` constructor just spawns that thread.
- **Creation:** `set_parent()` creates the container `HWND` and `WebUIHost` synchronously. Don't defer it: the host calls `show()` right after.
- **Cross-thread calls:** every call into the webview from another thread goes through `webview::dispatch(fn)`, because WebView2 COM objects are single-thread-affine. `Impl::mutex` guards the `webview` pointer.
- **Teardown:**
  - `webview::terminate()` is **not** cross-thread-safe on Win32: it is a bare `PostQuitMessage`.
  - So `~WebUIHost()` dispatches `terminate()` onto the webview thread.
  - It then waits with `MsgWaitForMultipleObjects(QS_SENDMESSAGE)` + `PeekMessageW`, not a plain `join()`. Child-window teardown can `SendMessage` to the UI-thread container, and a plain `join()` from the UI thread deadlocks REAPER.
- **Sizing:**
  - The embedded widget starts at size 0, so the webview thread sizes it with `MoveWindow` after `navigate()`.
  - The container fills the host window (`fillParent()`) in `set_parent()` and `show()`: REAPER doesn't call `set_size()` when switching from its generic UI to ours.
- **DevTools:** debug builds create the webview with devtools on (right click → Inspect), for the console and the DOM.
- **Transport:**
  - JS → C++: `bind("postToNative")` → `ReaShaderPlugin::handleWebUIMessage`.
  - C++ → JS: `eval()` → `window.__reashaderOnMessage`.
  - The plugin holds a `WebUISender` (`std::function`), invoked under its mutex so that `clearWebUISender()` waits for any in-flight send. It no-ops when no sender is registered.
- **Frontend:**
  - Plain sequential `<script>` tags, no ES modules: `file://` blocks module imports.
  - `styles/ui.scss` (partials in `styles/components/_*.scss`) is compiled by the build to `index.css` (see Build).
- **Ownership:** `ClapPluginState::gui` is a `unique_ptr<Gui, GuiDeleter>`. `Gui` and its deleter are defined in `gui_win32.cpp`, so the shell never needs the full type. `gui_destroy` is just `gui.reset()`: the webview is torn down first, then the window.

### Web UI protocol

Plain JSON objects with a `"type"` field. They're documented in `plugin/plugin.cpp` (web UI section) and handled by an `if`/`else` on the type there. On the JS side, `client.js` switches on the type and `api.js` sends.

| Direction | Message | Payload / effect |
|---|---|---|
| to UI | `snapshot` | `{ version, track, params, devices, logo, shader, shaders }`. The UI rebuilds itself from it (except the shader status line); its shader list is rescanned each time, e.g. when the UI opens and after an upload; sent on `ready`, activate, state load, device and shader changes |
| to UI | `paramValue` | `{ id, value }`: host automation |
| to UI | `shaderStatus` | `{ status, state }`, state = `busy`/`ok`/`error` |
| from UI | `ready` | — |
| from UI | `shaderSelect` | `{ name }`: a compiled shader, `""` = none (passthrough) |
| from UI | `paramValue` | `{ id, value }` |
| from UI | `renderingDevice` | `{ index }` |
| from UI | `logo` | `{ enabled }`: the 3D logo, on while the about box is open |
| from UI | `openUrl` | `{ url }`: `https://` only, opened in the system browser (`util::shell::openUrl`); the webview itself must never navigate away |
| from UI | `shaderUpload` | `{ name, source }`: GLSL sent as text |

### Parameters (`plugin/params.*`)

- **`Param`:** one plain struct: id, name (the state key), label (display), group (`Main` or `Shader`), units, default, min, max, automatable. Values are plain, within min..max: the defaults are 0..1, and shader params use their `//@param` range. CLAP param info uses the same range.
- **Ids:** a param's id is its index in the list, and also its CLAP param id. The plugin's own params (`AudioGain`, group `Main`, not shown in the web UI) come first, then the shader's (group `Shader`).
- **`ParamList`:**
  - Metadata is behind a mutex.
  - Values are a fixed array of `std::atomic<double>` (`maxCount` = 256), so the audio and video threads never lock.
  - `replaceShaderParams()` swaps the `Shader` group whenever a shader is compiled.

### Renderer (`render/`)

- **Vulkan 1.3** with dynamic rendering and synchronization2, so there are no render pass or framebuffer objects.
- **GPU list:** usable GPUs are the ones vk-bootstrap selects; the UI's device index is an index into that list.
- **Lifetimes: plain structs with `create()`/`destroy()` listing their handles, no deletion queues:**
  - `Context` (instance, device);
  - `FrameTargets` (frame size);
  - `ShaderPass` (per shader);
  - `Scene` (from the first time the logo is on until the device goes; its depth buffer per frame size).

  A device switch destroys the targets, the pass, the scene and the device, then recreates the device, the pass and the scene. The targets come back with the next frame.
- **Errors:** `VK_CHECK` throws `std::runtime_error`, and `ReaShaderRenderer`'s public functions catch everything.
- **Shader changes:** the renderer never compiles. `setShader(CompiledShader)` swaps the pass under `frameMutex`. Frames render one at a time and wait on the fence, so the old pass is idle. With no device (inactive), the shader is kept and installed by the next `init()`. `clearShader()` removes it. With no shader, `renderFrame` returns `false` (passthrough).
- **Internal shaders** (`fullscreen.vert`, `scene.vert`, `scene.frag`) are SPIR-V arrays compiled at build time, never read from disk.
- **Scene (`scene.*`):**
  - `Mesh` (.obj via tinyobjloader, host-visible vertex/index buffers) and `Texture` (stb, staged to a device image) are reusable for more 3D content.
  - `Scene` holds objects (mesh + texture descriptor set + local transform), a depth buffer (`prepare()` per frame size) and one pipeline (push constant: the object's MVP matrix).
  - It is drawn with `loadOp = LOAD` over the output.
  - It is created on `setLogoEnabled(true)` or with the device if the logo is on, and destroyed with the device.
  - Camera: z = -5, 70° FOV, the frame's aspect, y flipped via `proj[1][1] *= -1`. The spin is one degree per video frame (`time * frameRate`), wobbling with time.
- **Frame layouts:** after the passes, `output` is always `COLOR_ATTACHMENT_OPTIMAL` (the shader pass, or `recordInputToOutput()`); `recordDownload()` expects that.

### Shader contract (`shader_compiler.cpp`, `kShaderPreamble`; user docs in `src/shaders/examples/README.md`)

- **User shaders write only `main()`, plus an optional `uniform Params { ... };` block.**
- **Prepended automatically:**
  - `#version 450`;
  - `in vec2 uv` (0..1, top left = 0,0);
  - `out vec4 fragColor`;
  - `sampler2D iChannel0` (the input frame);
  - push constants `iResolution`, `iTime`, `iFrameRate`, `iFrame`.

  A user `#version` is dropped, and `#extension` lines are hoisted above the preamble. `#line 1` keeps error line numbers matching the user's file.
- **`Params`:** members must be `float`/`vec2`/`vec3`/`vec4`. Each component becomes one slider, named `member` or `member.x`, in reflection order. It is auto-bound to binding 1 (shaderc shifts uniform-block bindings by 1, explicit ones too); any other resource, or anything outside descriptor set 0, is rejected with an error.
- **Annotations:** `//@param member 'Label' default min max` (anywhere in the source; label and numbers optional, in that order) sets a slider's label, default and range. Without one: label = member name, 0.5, 0..1. Inspired by REAPER's video processor `//@param`, but keyed by member name, not index.
- **Keep in sync:** `gpu::ShaderInputs` must match `ReaShaderInputs` in the preamble (std430 push-constant layout, 20 bytes).

### Logging

- Use `LOG(level, toConsole | toFile | toBox, sender, title, message)` from `util/logging.h`.
- The log file is `<plugin dir>/rs.log`. It is kept open, and truncated on the first write of each process.
- **`toBox`:** message boxes are modal, so they are never shown on the calling thread.
  - `LOG` queues the box and calls the host's `request_callback()`.
  - `on_main_thread` then shows it (`showQueuedBoxes()`).
  - Each plugin instance registers the requester in `plugin_init`.
- **Paths:** use `util::paths::pluginDir()`, `resourcesDir()`, `uiDir()` and `compiledShadersDir()` (`std::filesystem::path`). Pass `.string()` to narrow file APIs (`fopen`, `ifstream`).

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
  - If a stack won't unwind, run `memory read --format A --count 3000 $rsp` and look for `_CxxThrowException` and return addresses inside `ReaShader-Debug.clap` (or `ReaShader.clap` for release).
- **Symbolizing:** run `llvm-symbolizer --obj=build/windows-debug/ReaShader-Debug.clap` on `(addr - module base + 0x180000000)`. This is only valid if the binary hasn't been rebuilt since the crash.
- **Hangs:** run `"" | lldb -p <pid> -o "bt all" -o "process detach" -o quit` (not `--batch`). Get the module base from `(Get-Process -Id <pid>).Modules`.
- **GPU faults:** recurring `nvlddmkm` events in the System log mean the Vulkan code is doing something invalid.
- **Vulkan validation output:**
  - Debug builds request the Khronos validation layer (if installed), with synchronization validation on (hazards between barriers and accesses). Its warnings and errors go to `rs.log` through vk-bootstrap's debug messenger. For the GPU test, no `rs.log` next to `gpu_test.exe` means no messages.
  - The log has none in normal use, so any is a bug.
  - `VK_LOADER_DEBUG=layer` shows whether the layer was loaded.
- **Testing the GPU code without REAPER:** `test/seed/` (`cmake -S test/seed -B build/tests-seed -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++`, build, run `build/tests-seed/gpu_test`). It runs frames through `FrameTargets` + `ShaderPass` + `Scene` and the shader compiler on every GPU, and checks the output pixels exactly. It prints `ALL PASSED`.
