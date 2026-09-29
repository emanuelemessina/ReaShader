# Proposals: handoff

Two proposals by the user. They were deferred until the September 2026 cleanup was done (see [history.md](history.md)), and it is. This document is the handoff for the session that picks them up.

**Status:** 1 (test application) is not started. 2 (render doc) is done: [rendering.md](rendering.md). Its section below is kept as the record of what was asked.

**Before starting**, read [CLAUDE.md](../CLAUDE.md) (the current architecture) and follow its hard rules. In particular:
- give a brief rationale before each batch of changes and wait for approval;
- the user commits;
- comments are brief and schematic, never history;
- CMake files follow the pipeline structure the user set up.

---

## 1. Test application

### Goal (the user's words, condensed)

- A small application that loads the plugin **the way REAPER does**, to verify new features and stability programmatically **before** the manual integration test in REAPER.
- If a REAPER test disagrees with the test app, the test app is updated to reflect what REAPER actually does. The app is a model of REAPER that improves over time.
- **Clearly separate** from the main `CMakeLists.txt` and the plugin's sources, so there's no confusion.
- **Tests are standardized** and kept apart from the test application's own code.

### What exists

- **`test/seed/`:** a standalone CMake project (`gpu_test`), separate from the plugin's build. It compiles the renderer's GPU code (`render/{gpu,context,frame_targets,shader_pass,shader_compiler,scene}.cpp`) with a small `main`, then on **every GPU** of the machine:
  - checks pixels byte by byte for an example shader, at an odd width with padded rows;
  - checks `Params` reflection, UBO offsets and BGRA channel order;
  - checks the stored JSON round trip;
  - checks `//@param` annotations, that the shipped examples compile, and that `broken.frag` fails with the error on the user's line 7;
  - checks that an extra sampler is rejected;
  - checks that the logo scene draws, leaving the rest of the frame untouched.

  It has its own tiny `expect()`. Build and run it:

  ```
  cmake -S test/seed -B build/tests-seed -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
  cmake --build build/tests-seed
  build/tests-seed/gpu_test
  ```

  It prints `ALL PASSED`, exits 0 on success, and was passing on 2026-09-29 (NVIDIA MX130 + Intel UHD 620).
- **`test/shaders/broken.frag`:** a deliberately broken shader.

### What REAPER does to the plugin (what the host must emulate)

Everything the plugin relies on, with the code that uses it:

1. **CLAP lifecycle** (`src/clap/plugin_entry.cpp`):
   1. `clap_entry.init` → `get_factory(CLAP_PLUGIN_FACTORY_ID)` → `create_plugin(host, "com.emanuelemessina.reashader")`;
   2. `init`;
   3. `activate(sample_rate, min, max)` → `start_processing` → `process`/`params.flush`;
   4. `on_main_thread` (after `request_callback`);
   5. `deactivate`;
   6. `destroy`.
2. **Host services the plugin calls:**
   - `request_callback`: shows message boxes and echoes automation to the UI (`util/logging`, `ReaShaderPlugin::onMainThread`);
   - `request_restart`: when a new shader's params are pending while active. REAPER answers with `deactivate` → `activate`.
   - `get_extension(CLAP_EXT_PARAMS)`: `rescan(CLAP_PARAM_RESCAN_ALL)` (called inside `deactivate` when params are pending) and `request_flush`.
3. **REAPER extension** (`ReaShaderPlugin::activate`, `plugin/plugin.cpp`): `host->get_extension(host, "cockos.reaper_extension")` returns a `reaper_plugin_info_t*` whose `GetFunc` provides:
   - `clap_get_reaper_context(host, sel)`: `sel = 4` → the FxDsp context, `sel = 1` → the parent track;
   - `video_CreateVideoProcessor(fxDsp, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION)` → an `IREAPERVideoProcessor` (`external/reaper-sdk/sdk/video_processor.h`). The host implements `newVideoFrame`, `renderInputVideoFrame`, `getNumInputs` and `getInputInfo`. The plugin sets `process_frame`, `get_parameter_value` and `userdata`, and deletes the processor in `deactivate`.
   - `GetSetMediaTrackInfo(track, "P_NAME", nullptr)` and `GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER")`.
4. **Video frames:** REAPER calls `process_frame(vproc, parmlist, nparms, projectTime, frameRate, forceFormat)` on its video thread.
   - `parmlist[0]` is wet/dry, and param `i` is at `parmlist[i + 1]`, **valued at video time**.
   - Frames are `'RGBA'`, laid out **B,G,R,A** in memory, rows `get_rowspan()` bytes apart.
   - The input frame is immutable: it is returned as-is or `Release()`d.
   - A test host should feed frames with odd widths and padded rows.
5. **State:** `state.save`/`state.load` through `clap_ostream_t`/`clap_istream_t` (one JSON document, `ReaShaderPlugin::saveState`/`loadState`). **Open question:** does REAPER call `load` before or after `activate`, and does it `deactivate` around it? Verify in REAPER and encode it in the host.
6. **GUI** (`src/clap/gui_win32.cpp`, Win32 only):
   - order: `is_api_supported` → `create` → `set_parent(HWND)` → `show`;
   - REAPER does **not** call `set_size` when switching from its generic UI to the plugin's;
   - REAPER's FX window is a dialog (the `WS_EX_CONTROLPARENT` gotcha in CLAUDE.md).

   A test host probably skips the GUI at first.
7. **Deployment:** REAPER keeps the `.clap` file locked while loaded. A host loading the built `.clap` from `build/<preset>/` avoids fighting over the deployed copy.

### Suggested design (to agree with the user first)

- **Two levels, both in `test/`, with their own `test/CMakeLists.txt` (never included by the main build):**
  - **Unit level** (what the seed already is): link the plugin's sources directly and test units, such as the renderer's blocks, `ParamList`, and the UI protocol. For the protocol, call `ReaShaderPlugin::handleWebUIMessage()` with a `WebUISender` that records the messages. Fast and precise.
  - **Host level:** a fake REAPER (`test/host/`) that loads the **built `ReaShader.clap`** through `clap_entry`, implements the host services and the REAPER extension above, and plays scenarios:
    - activate → frames → a shader change (restart + rescan) → state save/load → deactivate → destroy;
    - checks the frames coming back, the param list after a rescan, the state round trip, no crash or hang, and an empty `rs.log` (validation layer) in debug builds.

    A shader has to reach the plugin somehow, since the UI is the only entry today. Options:
    - a state blob with an embedded compiled shader (easiest, and already supported);
    - a test-only hook, which should be avoided.
- **Standardized tests:** one file per area under `test/cases/` (e.g. `render.cpp`, `params.cpp`, `protocol.cpp`, `host_lifecycle.cpp`), all in one format. Two options for the user:
  - (a) a minimal self-registering `TEST("name") { EXPECT(...); }` header of our own (about 40 lines, no dependency);
  - (b) [doctest](https://github.com/doctest/doctest) as a submodule (a single header, the de-facto standard).

  The seed moves into `test/cases/render.cpp`.
- **Discrepancy loop:** when REAPER behaves differently from the host (call order, threads, values), fix the fake host first, add a test that fails the way REAPER did, then fix the plugin. Record learned REAPER behaviours in CLAUDE.md.
- **Running:** a VS Code task (`test`) next to `build+deploy`, running `cmake -S tests -B build/tests` + build + run. It should stay separate from the build task.

### Open questions for the user

- Framework: own minimal macros or doctest?
- Host level now, or unit level first?
- Should the test task run automatically before deploy, or stay manual?

---

## 2. Render doc for humans (done: [rendering.md](rendering.md))

### Goal (the user's words, condensed)

- Vulkan is complicated. It's hard to maintain the renderer without knowing its terminology and what things do, and every renderer decision should map to how Vulkan works.
- Today, opening `src/render/` tells you nothing.
- So: a Markdown doc, **for humans**, explaining all of it, **kept up to date with every render change**.
- It is **separate from CLAUDE.md** (which is for Claude). They don't exclude each other: the same facts can appear in both, written for their audience.

### Suggested location and outline

`doc/rendering.md`. Mermaid diagrams render on GitHub. Outline:

1. **The big picture:** a REAPER frame's journey:

   ```
   CPU frame → upload buffer → input image → shader pass (or copy) → output image → logo scene → readback buffer → CPU frame
   ```

   One command buffer, one submit, one fence wait per frame, and why.
2. **Vulkan in ten concepts, each mapped to our code:**
   - instance / physical device / device / queue (`context.*`, vk-bootstrap);
   - command buffer and fence (`Context::beginCommands`/`submitAndWait`);
   - memory and VMA; buffers vs. images; image views; image layouts and barriers (`gpu.*`, `transition()`, synchronization2);
   - descriptor sets and layouts vs. push constants (what each pass binds);
   - pipelines and dynamic rendering (`createPipeline()`, no render passes or framebuffers);
   - SPIR-V and shader modules (glslc at build time for internal shaders, shaderc at upload for user shaders).
3. **File by file:** `renderer`, `context`, `frame_targets`, `shader_pass`, `shader_compiler`, `scene`, `gpu`, `frame_view`. What each owns, and when it is created and destroyed (the lifetime tiers).
4. **A frame, step by step,** with the exact barriers and why each one is needed (`recordUpload`, the pass, `recordInputToOutput`, the scene's load-and-draw-over, `recordDownload`, and the host read after the fence).
5. **The shader contract:** the preamble, `Params`, reflection, `//@param`, and the stored JSON. Link the user-facing `src/shaders/examples/README.md` instead of repeating it.
6. **Threads and safety:**
   - `frameMutex` and why the video thread only `try_lock`s;
   - why nothing throws out;
   - what "failed" and passthrough mean;
   - why the GPU stays up across deactivate.
7. **Decisions and why:**
   - Vulkan 1.3 with dynamic rendering;
   - host-visible buffers for frame I/O;
   - `B8G8R8A8_UNORM` matching REAPER's bytes (no blit, no swizzle);
   - compiling only on upload;
   - one fence per frame instead of pipelining, since REAPER's callback is synchronous;
   - the sRGB logo texture on a UNORM target (kept from the old look).
8. **How to extend:** add a pass; add a scene object or texture (`Mesh`/`Texture`/`Scene::objects`); add a built-in shader input (preamble ↔ `ShaderInputs` in sync, 20 bytes today).
9. **Debugging:** the validation layer to `rs.log`, `VK_LOADER_DEBUG`, the GPU test in `test/seed`, and crash dumps (link CLAUDE.md's section).

### Maintenance rule

Once the doc exists, add a hard rule to CLAUDE.md: *any change in `src/render/` updates `doc/rendering.md` in the same batch*.

---

## Other notes for the next session

- **Release builds work in REAPER** (checked by the user, September 2026). Debug and release are separate plugins (`ReaShader (Debug)`), so both can be installed at once. A Windows installer exists (CLAUDE.md, Build → Packaging). The test application could also cover the release build.
- **Compiled shaders are written into the plugin folder** (`resources/shaders/compiled/`). The installer installs per user, which is writable, but a manual system-wide CLAP install (e.g. Program Files) may not be. If that matters, fall back to a per-user data folder.
- **macOS/Linux:** the GUI (`clap.gui` with webview on WKWebView/WebKitGTK) is not implemented, and Boxer and `util::shell::openUrl` are untested there. The CMake and code `TODO`s mark the spots.
- **Restart + rescan** for shader params: the user reported the shader features working in REAPER after it was added. Worth an explicit host-level test (param list and automation after a shader change).
