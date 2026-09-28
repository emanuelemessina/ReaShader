# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ReaShader is a video-effect plugin for REAPER that runs a custom Vulkan rendering pipeline (GLSL shaders) over REAPER's video track frames. It ships with an embedded local web server that serves a JS/HTML/SCSS UI for controlling shaders, parameters, and GPU selection at runtime.

## Migration status: VST3 → CLAP (read this before touching the build or `source/vst3`)

The project is mid-migration from VST3 to **CLAP** (a lighter, C-ABI, header-only plugin format). This was a deliberate move, not a whim: CLAP was confirmed (empirically, not just from docs) to reach REAPER's video-processing tap just as well as VST3 does, and dropping the VST3 SDK removes the entire source of build-system pain (bundle folder structure, `moduleinfotool`/validator, processor/controller split, the old Visual-Studio-only `sln-make.inix` hacks). It also sets up two things planned for later: a possible rewrite in Rust, and embedding the web UI inside REAPER's FX window via CLAP's `gui` extension instead of a separate browser window.

- **Phase A (done)**: build system migrated to CLAP — CMake + Ninja + VS Code, no Visual Studio, no `sln-make`.
- **Phase B (done, renderer still pending)**: `rsparams`/`rsui`/the plugin-logic object were ported off Steinberg VST3 types and wired into the CLAP shell — `clap.params`, `clap.state`, and a minimal native `clap.gui` (Win32) all work against real parameter data now. `ReaShaderProcessor` + `ReaShaderController` (VST3's forced processor/controller split, and the two parallel parameter vectors it required) are **gone**, replaced by one unified [ReaShaderPlugin](src/reashader/reashaderplugin.h) instance — see Architecture below. What's **not** done yet: `ReaShaderRenderer`/the Vulkan pipeline isn't owned/wired by `ReaShaderPlugin` at all; `process_frame` (in [src/clap/reashader_clap.cpp](src/clap/reashader_clap.cpp)) still paints Phase A's diagnostic test pattern instead of calling into a renderer. That's the next pass.
- `src/vst3/` and the VST3 SDK dependency are **no longer part of the build** but are kept in the tree, untouched, as a reference for anyone comparing against the old VST3 behavior. `src/reashader/rsrenderer.*`/`vkt/` are also not in the build yet (they still reference the deleted `ReaShaderProcessor` type) — both get cleaned up/rewired together whenever the renderer is ported.
- **Renderer-port scoping (audited, not yet implemented)**: `vkt/` (21 files) has zero references to `ReaShaderProcessor` or any VST3 type — it's pure, self-contained Vulkan plumbing and can be pulled into the build with no rewrite. `rsrenderer.h`/`.cpp` is the *only* bridge to the plugin layer, with exactly 6 touchpoints on the deleted `ReaShaderProcessor*` member: the constructor (stores the pointer), `_initVulkan()` (pushes the enumerated GPU list via `setRenderingDevicesList`, and reads/writes `processor_rsParams[uRenderingDevice]` directly via raw `friend` access), and `changeCustomShader()` (reads `processor_rsParams.size()` for the next dynamic param id, calls the private `_webuiSendParamAdd`, and appends the new param). `ReaShaderPlugin` already has an equivalent `rsParams` vector but it's private/mutex-guarded with no renderer-facing accessor yet, and doesn't own a `std::unique_ptr<ReaShaderRenderer>`. Recommended shape for that pass: add narrow accessor methods to `ReaShaderPlugin` (an `rsParams` reference getter, `setRenderingDevicesList`, a `_webuiSendParamAdd` equivalent) rather than reintroducing the old `friend`-based raw-access relationship — keeps `rsrenderer.cpp`'s ~1500 lines almost untouched (mechanical `reaShaderProcessor`→`reaShaderPlugin` rename at the 6 call sites) without resurrecting VST3-era coupling. `_receivedFileFromWebUI` and the rendering-device-change WebUI handler are already stubbed with `// TODO(renderer pass)` comments marking the exact call sites for `changeCustomShader`/`changeRenderingDevice`.
- The REAPER video-tap access path confirmed for CLAP (different from VST3's `IReaperHostApplication::getReaperParent`): `clap_host_t::get_extension(host, "cockos.reaper_extension")` → cast to `reaper_plugin_info_t*` → `GetFunc("clap_get_reaper_context")` → call with `sel=4` ("FxDsp") → that pointer is the `fxctx` for `GetFunc("video_CreateVideoProcessor")(fxctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION)`. REAPER's `'RGBA'` video pixel format is packed in memory as **B,G,R,A** (byte0 = B) — build pixel values via `(a<<24)|(r<<16)|(g<<8)|b`, not hex literals (confirmed by an initial off-by-color-channel bug during testing).

## Build

There is no test suite or lint step. Verification is manual, in REAPER, using the project in `test/`.

### Prerequisites

- **CMake ≥ 3.21** and **Ninja**. That's it — everything else bootstraps itself (see below).
- **CLAP headers** are vendored under `external/clap` (plain copied headers, MIT-licensed, no build step, not a submodule) — nothing to install.
- `reaper-sdk`, `restinio`, `boxer`, `cwalk`, `json`, `WDL`, `glm`, `VulkanMemoryAllocator`, `tinyobjloader`, and `stb` under `external/` are all compiled into (or wired up for) the build (see CMakeLists.txt); run `git submodule update --init --recursive` after cloning to pull in the ones that are git submodules (everything except `clap`, `restinio`, `cwalk`, and `boxer`, which are plain vendored copies). The Vulkan SDK itself is the one dependency still installed manually — it's a full installer, not something to vendor — and it's **not required** to build today (GLM/VMA/tinyobjloader/STB are header-only and always available via their submodules; glslang/SPIRV-Cross are looked up optionally from the SDK install via `find_library` and just get skipped with a status message if it's missing). All of this only matters once the renderer is wired in.

**`cmake-git-versioning` is a git submodule**, not an auto-clone: `CMakeLists.txt` hard-fails configure with `FATAL_ERROR` if `external/cmake-git-versioning/cmake-git-versioning.cmake` isn't present, so it needs `git submodule update --init --recursive` like the other submodules above (a previous version of this doc claimed it auto-clones into `build/deps/` — that logic doesn't actually exist in `CMakeLists.txt`).

### Configure & build

**Quickest path**: run the **`build`** task (`.vscode/tasks.json` — Ctrl/Cmd+Shift+B, it's the default build task; prompts for debug/release via a `pickString` input). There's also a **`clean`** task that wipes the whole preset build directory (CMake cache, object files, staged assets, the built `.clap`) for a fresh reconfigure.

The `build` task is `cmake -DPROFILE=<profile> -P build.cmake`, identical on every OS — [build.cmake](build.cmake) picks the right preset for the host OS and runs configure + build. `CMakePresets.json`'s base preset sets `architecture: {value: "x64", strategy: "external"}`, i.e. it expects a compiler-capable environment to already be active rather than discovering one itself (fast, but means the compiler/Ninja must already be reachable on `PATH`). The project builds with **clang** (`clang++`, GNU-driver mode, not `clang-cl`) on Windows, not MSVC's `cl` — `CMakePresets.json` does not pin `CMAKE_CXX_COMPILER`, so whatever compiler CMake finds first on `PATH` is what gets used; keep that in mind if multiple compilers are installed.

The user doesn't use VS Code's CMake Tools extension for this project — just the `build`/`clean` tasks above. Otherwise, from the CLI (from an environment with the compiler/Ninja already reachable):

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

(`macos-debug`/`linux-debug` presets exist too, untested — Windows is the only platform actually exercised so far.)

This single `cmake --build` step does everything `sln-make.inix` + `scripts/win/*.ps1` used to do by hand, generator-agnostically via plain `add_custom_command()`s in `CMakeLists.txt`:
- compiles `src/shaders/*.glsl` to SPIR-V via `glslc` if it's found (skipped with a warning otherwise — the precompiled `.spv` files already committed under `src/shaders` are used as a fallback; nothing in the build actually consumes them yet, since the renderer isn't wired in)
- stages `res/images`, `res/meshes`, `src/shaders`, and the `rsui` frontend (minus `styles/`) next to the built plugin
- deploys the built `.clap` to the **per-user** CLAP plugin folder (`%LOCALAPPDATA%\Programs\Common\CLAP` on Windows, `~/Library/Audio/Plug-Ins/CLAP` on macOS, `~/.clap` on Linux) — no admin rights needed, unlike the old VST3 flow's system-folder deploy.

### Manual testing

Load "ReaShader" as a CLAP plugin on a REAPER track's FX chain. A small native window (Win32, no VSTGUI) should appear in the FX chain UI with an "Open Web UI" button, which shell-opens the default browser at the embedded `RSUIServer`'s URL — that browser tab is still where the real UI lives, unchanged. REAPER's own generic parameter list should show "Audio Gain", "Video Param", and "Rendering Device", editable from either side. Opening REAPER's Video window will show Phase A's diagnostic pattern (a color cycling R/G/B every 2s + a sweeping bar) — the renderer isn't wired in yet, so this is expected, not a bug.

## Architecture

### One unified plugin object — `ReaShaderPlugin`

[ReaShaderPlugin](src/reashader/reashaderplugin.h) is the single object holding all real logic: the parameter list (`rsParams`), the embedded `RSUIServer`, and the REAPER video-tap wiring. It's owned by the CLAP shell ([src/clap/reashader_clap.cpp](src/clap/reashader_clap.cpp)'s `ClapPluginState`, declared in [plugin_state.h](src/clap/plugin_state.h)), which forwards CLAP's `get_extension()` calls for `clap.audio-ports`/`clap.params`/`clap.state`/`clap.gui` to it. There is **no** processor/controller split and **no** relay layer between two copies of anything — that split, and the hand-rolled JSON+`IMessage` relay it required, only ever existed because VST3 forced two separate objects (still visible, for reference, in the untouched `src/vst3/` + the git history of `rscontroller.*`/`rsprocessor.*`, now deleted).

- **`clap.params`**: `ReaShaderPlugin::automatableParamCount()`/`getAutomatableParamInfo()` enumerate the `NumericParameter`/`Int8u`-typed entries of `rsParams` (see rsparams below) as CLAP parameters. Host automation arrives as `CLAP_EVENT_PARAM_VALUE` events, scanned in `process()`/`flush()` (both funnel through the shared `handleParamEvents()` helper in `reashader_clap.cpp`) and applied via `applyHostParamValue()`. Web-UI-originated edits go the other way: `ReaShaderPlugin` queues them (`_queueHostNotification`), and the same `handleParamEvents()` drains the queue into `out_events` so the host/its automation lane sees them too.
- **`clap.state`**: `ReaShaderPlugin::saveState()`/`loadState()` serialize/deserialize all of `rsParams` directly against the `clap_ostream_t`/`clap_istream_t` CLAP hands over — see rsparams below for the format.
- **`clap.gui`**: [reashader_clap_gui_win32.cpp](src/clap/reashader_clap_gui_win32.cpp) — Win32-only, embedded model (`set_parent` → `SetParent`). Plain Win32 API, no VSTGUI: a fixed-size child window with one button that calls `ShellExecuteW` to open the browser at `ReaShaderPlugin::getWebUIUrl()`. This is the direct replacement for the old `VST3Editor`-based panel + `system("start ...")` combo — same role (a tiny native launcher for the real, browser-hosted UI), no VST3/VSTGUI dependency. macOS/Linux aren't implemented (`is_api_supported` just returns false there).
- **REAPER video tap**: `ReaShaderPlugin::activate(host)` does `host->get_extension(host, "cockos.reaper_extension")` → `GetFunc("clap_get_reaper_context")` → `clap_get_reaper_context(host, 4)` ("FxDsp") → `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`, mirroring the confirmed-working path from the Phase A spike. Track info uses `clap_get_reaper_context(host, 1)` (parent track) the same way.
- **Not owned yet**: a `ReaShaderRenderer`. The REAPER video callbacks (`processVideoFrame`/`getVideoParam`, free functions in `reashader_clap.cpp` since they're CLAP/REAPER-glue rather than `ReaShaderPlugin`'s own domain logic) still paint/read the Phase A diagnostic pattern rather than driving real Vulkan rendering.

### Per-frame video path (current vs. pending)

Today, REAPER calls `processVideoFrame` (`src/clap/reashader_clap.cpp`), which just paints the diagnostic pattern. Once the renderer is ported, the intended path (matching the pre-port VST3 code, still visible via git history / `rsrenderer.h`) is:
1. `ReaShaderRenderer::loadBitsToImage` uploads REAPER's raw pixel buffer into a Vulkan image.
2. `ReaShaderRenderer::updateVirtualScene` + `drawFrame` run the Vulkan render pass (custom fragment shader, optional 3D scene, post-process pass).
3. `ReaShaderRenderer::transferFrame` copies the result back into REAPER's buffer.

### Vulkan layer (`vkt/` namespace) -- not in the build yet

A hand-rolled Vulkan wrapper toolkit used exclusively by `ReaShaderRenderer` ([rsrenderer.h](src/reashader/rsrenderer.h)). Neither is compiled into the current target (they still reference the deleted `ReaShaderProcessor` type; wiring them back in is the renderer-port pass). Key convention (see [doc/Development.md](doc/Development.md)), which still applies once that happens:

- Every `vkt` object pushes its own cleanup into a `vkt::deletion_queue` supplied at construction — **never delete `vkt` objects manually**. `ReaShaderRenderer` keeps four separate queues (`vktMain`, `vktFrameResized`, `vktPhysicalDeviceChanged`, `vktCustomShaderChanged`) flushed at the appropriate granularity so a GPU switch or shader hot-swap doesn't tear down the whole context.
- All raw `Vk*` handles are initialized to `VK_NULL_HANDLE`.
- `changeCustomShader` recompiles GLSL to SPIR-V on the fly (glslang) and rebuilds only the pipeline, not the whole renderer, using `vktCustomShaderChanged`.
- `changeRenderingDevice` lets the user switch physical GPU at runtime (device list is enumerated and sent to the web UI).

### Web UI protocol (`rsui/`)

- [api.h](src/reashader/rsui/api.h) defines the JSON message contract shared conceptually between C++ and JS: a `MessageType`/`RequestType` enum pair, a fluent `MessageHandler` (`.reactToX(...).reactToY(...).fallbackWarning(...)`) for parsing incoming messages, and `MessageBuilder` for constructing outgoing ones. Types are plain `Parameters::Id` (`uint32_t`)/`double` now, not Steinberg types. When adding a new message type, update the enum, `typeStrings`, and add both a `reactTo*` and a `build*` method — the two sides are matched purely by the `"type"` string field.
- [backend/backend.h](src/reashader/rsui/backend/backend.h) (`RSUIServer`) is a RESTinio-based embedded HTTP/WebSocket server, now owned directly by `ReaShaderPlugin`. Binary WebSocket frames prefixed with the `"RS"` magic sequence are file uploads, reassembled by `FileUploadProcess` (chunked, with a timeout watchdog thread) — used e.g. for uploading a custom shader (the receiving end, `ReaShaderPlugin::_receivedFileFromWebUI`, is currently a stub pending the renderer port).
- Frontend lives in `src/reashader/rsui/frontend/` (plain HTML/CSS/JS + SCSS partials, no build tooling/bundler — SCSS is compiled ahead of time, e.g. via the Live Sass Compile VS Code extension configured in [.vscode/settings.json](.vscode/settings.json)).
- **Restinio header note**: restinio's headers conflict with `Windows.h` if not carefully ordered. `RSUIServer`'s public header ([backend.h](src/reashader/rsui/backend/backend.h)) uses `std::any` to hide restinio types entirely; restinio types only appear inside [rsuiserver.cpp](src/reashader/rsui/backend/rsuiserver.cpp)/`wshandler.cpp`. Follow this pattern for any new server-side code — do not leak restinio types into headers included elsewhere.
- Resources (`RSUI_DIR`/`ASSETS_DIR`, in [tools/paths.h](src/reashader/tools/paths.h)) resolve relative to the plugin binary's own directory now — CLAP is a flat file with no VST3-style bundle folder, so there's no "go up a level to Resources/" step anymore.

### Custom parameter system (`rsparams/`)

`Parameters::IParameter` ([rsparams.h](src/reashader/rsparams/rsparams.h)) is a polymorphic, JSON- and binary-serializable parameter type, so the web UI can define/add parameters dynamically (not just ones registered as CLAP params at startup). `TypeInstantiator` is a factory keyed by `Parameters::Type`; new parameter subclasses must be registered in `_registerParameterInstantiator` and must implement the `...Derived` virtuals (`toJsonDerived`, `fromJsonDerived`, `serializeDerived`, `deserializeDerived_v1`, `setValueFromJson`). `NumericParameter` (was `VSTParameter` under VST3) is the host-automatable numeric type exposed via `clap.params`; `Int8u` maps to a stepped/enum CLAP param; `String` (e.g. the custom shader name) is UI/state-only, never host-automatable — same as before.

Binary (de)serialization goes through [paramstream.h](src/reashader/rsparams/paramstream.h)'s `ParamWriter`/`ParamReader`, a small wrapper over CLAP's `clap_ostream_t`/`clap_istream_t` (replacing VST3's `IBStreamer`) — byte layout is unchanged from the VST3-era format. `Parameters::Preset::write()`/`read()` (free functions, not a stateful class — CLAP hands `save`/`load` distinct stream types, unlike VST3's single bidirectional `IBStream`) serialize the whole `rsParams` vector.

There's only **one** `rsParams` vector now, owned by `ReaShaderPlugin` — not two kept in sync via messaging.

### Logging / errors

Use `LOG(...)` from [tools/logging.h](src/reashader/tools/logging.h) (destinations are OR'd flags like `toConsole | toFile`) rather than ad-hoc `std::cout`/`OutputDebugString`. Vulkan calls that return a `VkResult` should be wrapped in `VK_CHECK_RESULT(...)` ([vktcommon.h](src/reashader/vkt/vktcommon.h)), which throws `std::runtime_error` on failure — low-level Vulkan init failures are caught and guarded in `ReaShaderRenderer::_initVulkanGuarded`/`_cleanupVulkanGuarded` to avoid SEH unwinding issues (once the renderer is back in the build).

### Vendored dependency notes (learned the hard way during the Phase B port)

- `external/reaper-sdk`'s `video_frame.h` needs `wdltypes.h` (`WDL_FIXALIGN`, `INT_PTR`) included first — it doesn't include it itself, so any file pulling in `video_processor.h`/`video_frame.h` must `#include "wdltypes.h"` immediately before. `IVideoFrame::get_bits()` returns `char*` in the currently-vendored SDK version, not `int*` — cast at call sites.
- `.c` sources (`external/cwalk/cwalk.c`, restinio's bundled `http_parser.c`) need `LANGUAGES CXX C` in `project()` — CMake silently drops `.c` files from the build otherwise (no error, they just don't get compiled, which only shows up as a *link* error later).
- Windows headers care about include order: `<shellapi.h>` must come after `<windows.h>`, not before.
- Clang's GNU-driver mode (`clang++`, as opposed to `clang-cl`) doesn't imply `UNICODE`/`_UNICODE` the way a typical MSVC project default does — code calling the bare `GetModuleHandleEx`/`GetModuleFileName` macros can silently resolve to the ANSI (`...A`) overloads and fail to compile against wide-string arguments; call the explicit `...W` functions instead.
- **`CreateWindowExW` with `WS_CHILD` and a null `hWndParent` fails with `GetLastError() == 1406`** (`ERROR_TLW_WITH_WSCHILD`, "top-level window with WS_CHILD style") — easy to misdiagnose as a class-registration or environment problem (the error name doesn't obviously map to "wrong hWndParent", and confusingly `RegisterClassW`/`GetClassInfoExW` can still report the class as present while `CreateWindowExW` itself fails). The fix used in [reashader_clap_gui_win32.cpp](src/clap/reashader_clap_gui_win32.cpp): don't create the window in `clap_plugin_gui::create()` (no real parent HWND exists yet at that point) — only register the window class there, and defer the actual `CreateWindowExW` call to `set_parent()`, where the host hands you the real parent HWND to pass in directly.
- The deploy step must copy `assets/`/`rsui/` alongside the `.clap`, not just the binary — `RSUIServer` resolves `ASSETS_DIR`/`RSUI_DIR` relative to wherever the plugin binary *actually ends up running from* (`tools::paths::getDynamicLibraryDir()`), not the build tree. Deploying only the `.clap` file leaves the web UI unable to find `rsui.html` at runtime even though the build tree looks correct.
- **glslang/SPIRV-Cross now resolve from the installed Vulkan SDK, not a manual build**: the SDK's `Lib/` folder ships `glslang.lib` + 4 companion libs (`OSDependent`, `MachineIndependent`, `GenericCodeGen`, `OGLCompiler`) and `spirv-cross-core.lib`/`spirv-cross-glsl.lib` directly (confirmed via LunarG's own SPIR-V toolchain docs) — `CMakeLists.txt`'s `find_library` block picks these up automatically, no vendoring/building needed. One caveat worth remembering when the renderer pass actually links against these: **the SDK ships release-only builds of these libs, no debug variant** — linking them into a Debug configuration is a deliberate, documented tradeoff (avoids a from-source build entirely) rather than an oversight, and should be empirically verified once the renderer is wired in (per this project's own "confirmed empirically, not just from docs" ethos) since it could in principle hit a debug/release CRT (`_ITERATOR_DEBUG_LEVEL`) mismatch if STL objects cross the linkage boundary. If that turns out to be a real problem, `RS_SPVC_PATH` (and the `find_library` `HINTS` for glslang) can be pointed at a manually built checkout instead — that fallback is intentionally still supported, not removed.
