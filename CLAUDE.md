# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository. It describes the code **as it is now**. How it got here (the VST3 → CLAP migration, rejected alternatives, past crash investigations) is in [doc/history.md](doc/history.md).

## What this is

ReaShader is a **CLAP** video-effect plugin for REAPER. It taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back. Its HTML/JS/SCSS UI is embedded in REAPER's FX window through a native webview ([webview/webview](https://github.com/webview/webview)).

## Cleanup in progress

A multi-phase cleanup is underway. The plan lives at `~/.claude/plans/picking-up-on-this-snug-aurora.md`.
- **Phases:** 1 deletions/hygiene → 2 build → 3 utilities → 4 params/state/protocol → 5 layering → 6 renderer rewrite (vkt → vk-bootstrap + plain structs) → 7 docs.
- **How it runs:** before each batch of changes, give the user a brief rationale and wait for approval. The user commits between phases.
- **Status:** phase 2 done (built, awaiting REAPER check + user commit). Next: phase 3 (utilities).

## Hard rules

- **Nothing may throw out of a REAPER or CLAP callback.** REAPER treats an escaped exception as fatal (`abort()`, exception `0x40000015` "inside reaper.exe"). `ReaShaderRenderer::renderFrame()` never throws. A Vulkan error sets `frameFailed`, and video passes through until the next FX activation.
- **Never block REAPER's video thread.** `renderFrame` `try_lock`s `frameMutex`. `init`, `shutdown` and `changeRenderingDevice` hold that mutex.
- `deactivate()` deletes the video processor *before* the renderer's `shutdown()`.
- REAPER's `'RGBA'` frames are laid out in memory as **B,G,R,A** (byte 0 = B).
- **Encoding:** frontend files must be UTF-8 (`file rsui.html` must not say "UTF-16"). A UTF-16 `rsui.html` loaded via `file://` renders as garbage text.
- **Comments:** they describe what the code does and why, for a reader with no session context. Investigation narratives go in `doc/history.md`, not in code.
- **Commits:** the user commits. Don't run `git commit` unless asked.

## Build

There is no test suite or lint step. Verification is manual, in REAPER.

**Prerequisites**
- **CMake ≥ 3.25**: `CMakePresets.json` uses schema v6.
- **Ninja**, and **clang** (`clang++`, GNU driver; not `clang-cl` or MSVC `cl`). The Windows presets pin `clang`/`clang++`, which must be on `PATH`.
- **The Vulkan SDK:** found with `find_package(Vulkan)` through `VULKAN_SDK`, which its installer sets. `glslc`, glslang and SPIRV-Cross all come from the SDK. `CMakeLists.txt` picks the `d`-suffixed debug variants of the libraries for Debug builds.
- **Submodules:** run `git submodule update --init --recursive`. `clap`, `cwalk` and `boxer` are plain vendored copies. Everything else in `external/` is a submodule, including `cmake-git-versioning`, and configure fails without it.
- **First configure needs network once:** `webview` fetches the WebView2 headers from NuGet if no system copy is found.

**Building**
- The VS Code **`build+deploy`** task is the default build task (Ctrl+Shift+B). It runs `cmake -DPROFILE=<debug|release> -P build.cmake`, which:
  1. configures (first time only);
  2. builds;
  3. deploys the `.clap` plus `assets/` and `rsui/` to the per-user CLAP folder (`%LOCALAPPDATA%\Programs\Common\CLAP`, `~/Library/Audio/Plug-Ins/CLAP`, `~/.clap`).

  If REAPER has the `.clap` open, the deploy waits for you to close REAPER and press Enter, then retries.
- The **`clean`** task wipes the preset build directory.
- **CLI alternative** (builds without deploying): `cmake --preset windows-debug`, then `cmake --build --preset windows-debug`.
- **The build itself (`CMakeLists.txt`):**
  - compiles `src/shaders/*.glsl` to SPIR-V with `glslc`, into `build/<preset>/assets/shaders` (no `.spv` in git);
  - stages `res/images`, `res/meshes`, the shader sources and the `rsui` frontend (minus `styles/`) next to the `.clap`.

  The runtime resolves `assets/` and `rsui/` relative to the plugin binary, so they must travel with it.
- **IntelliSense:** it reads `build/windows-debug/compile_commands.json`, which is written at configure time. On a fresh clone, run the build task once.
- **Warnings:** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` apply to our code, and a missing `return` is an error. Third-party headers are `SYSTEM` includes and third-party sources build with `-w`. The goal is zero warnings from our code.
- Only Windows has been exercised. The macOS/Linux branches print `TODO` warnings for what's missing (the GUI, boxer's linking).

**Manual testing:**
1. Load "ReaShader" (CLAP) on a track that has a video item.
2. Check the FX window shows the embedded web UI and resizes with the window.
3. Check REAPER's generic parameter list shows "Audio Gain", "Video Param" and "Rendering Device".
4. Check REAPER's Video window shows the logo mesh composited over the video.

## Architecture

```
src/clap/reashader_clap.cpp          CLAP entry, descriptor, extension trampolines, REAPER video callbacks
src/clap/plugin_state.h              ClapPluginState: owns ReaShaderPlugin + the GUI's HWND/WebUIHost
src/clap/reashader_clap_gui_win32.cpp  clap.gui (Win32, embedded child window)
src/clap/webui_host_win32.*          WebUIHost: webview on its own thread, JSON bridge to the plugin
src/reashader/reashaderplugin.*      ReaShaderPlugin: params, state, web-UI messages, video-tap wiring
src/reashader/rsrenderer.*           ReaShaderRenderer: the Vulkan pipeline (uses vkt/)
src/reashader/vkt/                   hand-rolled Vulkan wrapper toolkit
src/reashader/rsparams/              polymorphic parameter types + binary (de)serialization
src/reashader/rsui/api.h             JSON message protocol (C++ side); frontend/ is the JS side
src/reashader/tools/                 logging, paths, exceptions, base64
```

### ReaShaderPlugin (one object, no processor/controller split)

- **`clap.params`:**
  - The `NumericParameter`/`Int8u` entries of `rsParams` are exposed as CLAP params.
  - Host automation arrives as `CLAP_EVENT_PARAM_VALUE` events in `process()`/`flush()`, through `handleParamEvents()`, and is applied by `applyHostParamValue()`.
  - Web-UI edits are queued (`_queueHostNotification`) and drained into `out_events` by the same helper.
- **`clap.state`:** `saveState()`/`loadState()` serialize `rsParams` through `ParamWriter`/`ParamReader` (`rsparams/paramstream.h`).
- **REAPER video tap:** `activate(host)` does:
  1. `host->get_extension(host, "cockos.reaper_extension")`, cast to `reaper_plugin_info_t*`;
  2. `GetFunc("clap_get_reaper_context")`: with `sel=4` it gives the FxDsp context, with `1` the parent track;
  3. `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`;
  4. `reaShaderRenderer->init()`.

  `deactivate()` undoes it. The same renderer instance survives repeated activate/deactivate cycles.
- **Renderer access:** `ReaShaderRenderer` reaches plugin data only through narrow, mutex-guarded accessors (`getRenderingDeviceIndex`, `setRenderingDeviceIndex`, `setRenderingDevicesList`, `rsParamsCount`, `addRendererParam`).

### Per-frame video path

REAPER calls `processVideoFrame` → `processFrame` (`reashader_clap.cpp`):
1. `vproc->renderInputVideoFrame(0, 'RGBA')` gets the upstream frame. It is immutable, and is `Release()`d before returning.
2. `ReaShaderRenderer::renderFrame()`, under `try_lock(frameMutex)`, runs:
   - `checkFrameSize`
   - `loadBitsToImage` (upload)
   - `drawFrame` (3D scene + post-process pass)
   - `transferFrame` (download into a new `vproc->newVideoFrame`)
3. If `renderFrame` returns `false` (uninitialized, busy or failed), the input frame is passed through unchanged.

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
  - The plugin holds a `WebUISender` (`std::function`) and no-ops when none is registered.
- **Shader upload:** a custom shader arrives as one base64 JSON message (`FileUpload`).
- **Frontend:**
  - Plain sequential `<script>` tags, no ES modules: `file://` blocks module imports.
  - SCSS is compiled ahead of time (Live Sass Compile, see `.vscode/settings.json`), and the compiled `rsui.css` is committed.
- **Ownership:** `ClapPluginState::webUIHost` is a raw pointer, `new`/`delete`d only in `reashader_clap_gui_win32.cpp`. `WebUIHost` is incomplete in `reashader_clap.cpp`.

### Web UI protocol

- `rsui/api.h` defines the message types, `MessageHandler` for parsing, and `MessageBuilder` for building.
- The C++ and JS sides match purely on the `"type"` string.
- To add a message: extend the enum and `typeStrings`, then add a `reactTo*` and a `build*` method.

### Parameters (`rsparams/`)

- **Types:** `Parameters::IParameter` is polymorphic and serializable to both JSON and binary.
  - `NumericParameter`: host-automatable.
  - `Int8u`: stepped/enum.
  - `String`: UI and state only.
- **Adding a type:** register it in `_registerParameterInstantiator` and implement the `...Derived` virtuals.

### Vulkan layer (`vkt/`)

- Every `vkt` object pushes its cleanup into a `vkt::deletion_queue`. Never delete `vkt` objects manually.
- `ReaShaderRenderer` keeps 4 queues: `vktMain`, `vktFrameResized`, `vktPhysicalDeviceChanged` and `vktCustomShaderChanged`.
- Raw `Vk*` handles are initialized to `VK_NULL_HANDLE`.
- `VK_CHECK_RESULT` throws `std::runtime_error`. Catch it before any callback boundary.

### Logging

- Use `LOG(...)` from `tools/logging.h`. Destinations are OR'd flags (`toConsole | toFile | toBox`).
- The log file is `<plugin dir>/rs.log`, truncated on the first load per process.

## Gotchas

- **REAPER SDK:** `video_frame.h` needs `wdltypes.h` included first. `IVideoFrame::get_bits()` returns `char*`.
- **C sources:** `.c` files (`cwalk.c`) need `LANGUAGES CXX C` in `project()`. Otherwise CMake silently skips them, and you get a link error later.
- **Windows headers:**
  - `<shellapi.h>` goes after `<windows.h>`.
  - No `NOMINMAX` is defined, so `std::max`/`std::min` break in files that include `<windows.h>`.
  - clang's GNU driver doesn't define `UNICODE`, so call the explicit `...W` functions.
- **`CreateWindowExW`** with `WS_CHILD` and a null parent fails with error 1406 (`ERROR_TLW_WITH_WSCHILD`). Create the window in `set_parent()`, not in `gui::create()`.
- **Vulkan SDK libs:** `OGLCompiler.lib` no longer exists. `GetDefaultResources()` lives in `glslang-default-resource-limits.lib`. `SpvTools.h` needs `SPIRV-Tools(-opt).lib` linked.
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
  - `vkt/` has no debug messenger, so launch REAPER with these to get the Khronos layer's output (on in debug builds) as a log file:
    - `VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG`
    - `VK_KHRONOS_VALIDATION_LOG_FILENAME=<path>`
    - `VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn`
  - The log is empty in normal use, so anything in it is a bug.
