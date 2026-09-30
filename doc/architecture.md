# How ReaShader is put together

ReaShader is a **CLAP** video-effect plugin for REAPER. It taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back. Its HTML/JS/SCSS UI is embedded in REAPER's FX window through a native webview ([webview/webview](https://github.com/webview/webview)).

This doc maps the pieces: the plugin object, the video path, the web UI and its protocol, parameters, the renderer, the shader contract, LUTs and logging. The renderer has its own, deeper guide: [rendering.md](rendering.md).

Contents:

1. [Layout](#1-layout)
2. [The plugin](#2-the-plugin)
3. [The per-frame video path](#3-the-per-frame-video-path)
4. [The embedded web UI](#4-the-embedded-web-ui)
5. [The web UI protocol](#5-the-web-ui-protocol)
6. [Parameters](#6-parameters)
7. [The renderer](#7-the-renderer)
8. [The shader contract](#8-the-shader-contract)
9. [Logging and paths](#9-logging-and-paths)

## 1. Layout

```
src/clap/plugin_entry.cpp    CLAP entry, descriptor, extension callbacks (forward to ReaShaderPlugin)
src/clap/plugin_state.h      ClapPluginState: clap_plugin_t + ReaShaderPlugin + Gui, one per instance
src/clap/gui_win32.cpp       clap.gui (Win32): Gui = container window + WebUIHost
src/clap/webui_host.*        WebUIHost: webview on its own thread, JSON bridge to the plugin (Win32)
src/plugin/plugin.*          ReaShaderPlugin: params, state, web UI messages, REAPER video tap
src/plugin/params.*          Param struct + ParamList (lock-free values)
src/render/renderer.*        ReaShaderRenderer: owns the GPU objects below, renders frames, never throws
src/render/context.*         gpu::Context: vk-bootstrap instance/device, queue, command buffer + fence, VMA
src/render/frame_targets.*   gpu::FrameTargets: upload/readback buffers + input/output/work images (per frame size),
                             recordPasses (chains the passes)
src/render/pass.*            gpu::Pass: the fullscreen pass interface, and what passes share (fullscreen.vert, ...)
src/render/shader_compiler.* gpu::compileShader (contract preamble, shaderc, SPIRV-Reflect, //@param), stored JSON form
src/render/shader_pass.*     gpu::ShaderPass: fullscreen pipeline for a compiled shader
src/render/lut.*             gpu::Lut (a LUT as a 3D image) + gpu::LutPass (the frame through a LUT, blended by LUT Mix)
src/render/scene.*           gpu::Scene: textured meshes (tinyobjloader, stb) + depth, drawn over the frame (the logo)
src/render/lut_file.*        .cube LUT parser, 1D/domain baking, stored JSON form (base64 half floats)
src/render/gpu.*             Vulkan helpers: VK_CHECK, Buffer, Image, createPipeline(), transition(); VMA implementation
src/render/frame_view.h      FrameView: a CPU frame (BGRA, rowBytes)
src/util/                    logging, paths, shell (openUrl), base64
src/ui/                      the web UI: index.html, scripts/{api,ui,client}.js, styles/ui.scss; staged as ui/
src/shaders/examples/        example shader sources + README for users, staged as resources/shaders/examples/
src/shaders/internal/        shaders built into the plugin (fullscreen.vert, lut.frag, scene.vert/.frag), compiled at build time
res/                         images and meshes, staged as resources/
installer/windows/           Inno Setup extras for the CPack installer: reashader.iss (sections), installer.pas (code)
test/                        the test application (see testing.md)
external/                    dependencies (git submodules)
doc/                         developer docs
```

## 2. The plugin

`ReaShaderPlugin` (`src/plugin/plugin.*`) is one object per instance. There's no processor/controller split: it holds params, state, the web UI connection, the REAPER video tap and the renderer.

### Threads

Listed in `plugin/plugin.h`:

| Thread  | Does                                              |
| ------- | ------------------------------------------------- |
| main    | lifecycle, state, `onMainThread()`                |
| audio   | host param events, lock-free values only          |
| video   | renderer, `try_lock` only                         |
| webview | UI messages, device switch, shader and LUT upload |

### `clap.params`

- **Which params the host sees:** params with `automatable = true` are exposed as CLAP params: Audio Gain (host only, not in the web UI), LUT Mix, and every shader param.
- **Host automation** arrives in `process()`/`flush()` (`handleParamEvents()`) and goes into `applyHostParamValue()`, which is lock-free. It then requests a main-thread callback, and `onMainThread()` echoes the values to the web UI.
- **Web UI edits** are flagged with `ParamList::flagForHost()`, plus `host_params->request_flush()`. `takeParamChangeForHost()` drains them into `out_events`, lock-free.

### `clap.state`

One JSON document: `{ version: 3, params: { name: value }, device, logo, shader: { name, compiled }, lut: { name, mode, data } }`.

- **The compiled shader and the LUT** (their stored JSON) are embedded, so projects are self-contained and never recompile or re-parse.
- **Unknown or old state** loads defaults.
- **Shader param values** are restored by name, once the shader is loaded.

### The REAPER video tap

`activate()`:

1. calls `host->get_extension(host, "cockos.reaper_extension")`, cast to `reaper_plugin_info_t*`;
2. calls `GetFunc("clap_get_reaper_context")`: with `sel=4` it gives the FxDsp context, with `1` the parent track;
3. calls `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`;
4. calls `reaShaderRenderer->init()`.

`deactivate()` deletes the video processor. The renderer stays initialized across activate/deactivate cycles, and a failed renderer starts over on the next `activate()`.

### Shaders

- **Compiled once, on upload (the only place):** `_uploadShader()` runs `gpu::compileShader`, writes `resources/shaders/compiled/<stem>.json`, then uses it.
- **The shader list** is the `*.json` files in `util::paths::compiledShadersDir()`. `shaderSelect` loads one, and `""` means none.
- **The current shader** is `shaderName` + `shaderData` (the stored JSON), saved in state. There is none at start, so video passes through.
- **Loading:** `_useShader()` parses the stored JSON (`gpu::fromJson`) and hands it to `ReaShaderRenderer::setShader()`. `_clearShader()` unloads it.
- **Status:** every outcome goes to the UI as `shaderStatus`: `Loaded <name>` or the error. The UI itself shows `busy` (a spinner) while it waits.
- **Writable plugin folder:** uploading writes into the plugin folder. The per-user CLAP folder is writable; a system-wide install might not be.

### Shader params are host params (restart + rescan)

CLAP allows the param list to change only while the plugin is deactivated. So:

1. `setShaderParams()`, called from any thread, stores the new params as pending and requests a main-thread callback.
2. `onMainThread()` applies them now if the plugin is inactive, or else calls `host->request_restart()`.
3. On restart, the host's `deactivate()` applies them.

Applying means `ParamList::replaceShaderParams()` + `host_params->rescan(CLAP_PARAM_RESCAN_ALL)` + a snapshot to the UI. While params are pending, frames give the shader's sliders their defaults.

### LUTs

- **Parsed once, on upload (the only place):** `_uploadLut()` runs `gpu::parseCube` (`render/lut_file.*`), writes `resources/luts/<stem>.json`, then uses it. Only `.cube` files are read. A 1D LUT is baked into a 33³ cube and a `DOMAIN` other than 0..1 is resampled onto 0..1, so the stored form is always a 0..1 cube: `{ version, title, size, data }`, where `data` is base64 of half-float RGB (`util/base64.*`).
- **The LUT list** is the `*.json` files in `util::paths::lutsDir()`. `lutSelect` loads one, and `""` means none.
- **The current LUT** is `lutName` + `lutData` (the stored JSON), saved in state. There is none at start.
- **Loading:** `_useLut()` parses the stored JSON (`gpu::lutFromJson`) and hands it to `ReaShaderRenderer::setLut()`. `_clearLut()` unloads it.
- **Status:** every outcome goes to the UI as `lutStatus`, like `shaderStatus`.
- **The mode** (`lutMode`: `before` / `after` the shader, or `shader`) isn't a host param: it lives in state and the web UI, and goes to `ReaShaderRenderer::setLutMode()`. Unknown modes are ignored. In `shader` mode the shader samples the LUT itself, as `iChannel1` (see [the shader contract](#8-the-shader-contract)); with no shader, the LUT pass runs alone.
- **LUT Mix** is a fixed host param (see [Parameters](#6-parameters)): 0 = the frame as is, 1 = fully through the LUT. It applies to the LUT pass; in `shader` mode, blending is up to the shader.

### Rendering device

The GPU choice isn't a host param: it lives in state and the web UI. Changing it (from the UI or on state load) calls `changeRenderingDevice()`.

### The logo (an easter egg)

- `showLogo`, off by default, saved in state as `logo`. Not a host param.
- The UI's header logo opens the about box, which sends `logo { enabled: true }`. Closing it (×, a click outside, Escape) sends `false`. The box is open whenever the snapshot's `logo` is true.
- It calls `ReaShaderRenderer::setLogoEnabled()`, and a 3D logo spins in the video window.

### Version

`REASHADER_VERSION`, a compile definition from the last git tag, is used by the CLAP descriptor and the snapshot (the about box).

### Renderer access

`ReaShaderRenderer` reaches plugin data only through `getRenderingDeviceIndex`, `setRenderingDeviceIndex`, `setRenderingDevicesList` and `setShaderParams`. The plugin's param values come in with each frame (`FrameInputs`): the fixed ones by id (e.g. `Parameters::LutMix`), then the shader's.

## 3. The per-frame video path

REAPER calls `ReaShaderPlugin::_processVideoFrame` (`plugin/plugin.cpp`), which `activate()` installs:

1. `vproc->renderInputVideoFrame(0, 'RGBA')` gets the upstream frame. It is immutable, and is `Release()`d before returning.
2. Param values at video time come from `parmlist` (`[0]` = wet/dry, param `i` at `[i + 1]`), falling back to `ParamList` for params REAPER doesn't know yet.
3. `ReaShaderRenderer::renderFrame()`, under `try_lock(frameMutex)`:
   1. (re)creates `FrameTargets` if the size or row stride changed;
   2. `memcpy`s into the mapped upload buffer, and writes the shader's `Params` into its mapped UBO;
   3. records one command buffer: buffer → input image → the passes (the shader and the LUT, in the LUT mode's order, through work images; a plain copy when there are none) → output image → logo scene on top (if enabled) → readback buffer;
   4. submits once and waits on one fence (a 2 s timeout means a GPU hang, and sets `failed`);
   5. `memcpy`s out into a new `vproc->newVideoFrame`.
4. If `renderFrame` returns `false` (inactive, busy, failed, or no shader, LUT or logo), the input frame is passed through unchanged.

## 4. The embedded web UI

`WebUIHost` (`src/clap/webui_host.*`) hosts the webview inside the FX window.

- **Its own thread:** the webview and its message loop run on a background thread owned by `WebUIHost`. `webview::webview`'s constructor blocks for seconds while WebView2 initializes, so the `WebUIHost` constructor only spawns that thread.
- **Creation:** `set_parent()` creates the container `HWND` and the `WebUIHost` synchronously. It can't be deferred: the host calls `show()` right after.
- **Cross-thread calls:** every call into the webview from another thread goes through `webview::dispatch(fn)`, because WebView2's COM objects are single-thread-affine. `Impl::mutex` guards the `webview` pointer.
- **Teardown:**
  - `webview::terminate()` is **not** cross-thread-safe on Win32: it is a bare `PostQuitMessage`. So `~WebUIHost()` dispatches `terminate()` onto the webview thread.
  - It then waits with `MsgWaitForMultipleObjects(QS_SENDMESSAGE)` + `PeekMessageW`, not a plain `join()`. Child-window teardown can `SendMessage` to the container on the UI thread, and a plain `join()` from the UI thread deadlocks REAPER.
- **Sizing:**
  - The embedded widget starts at size 0, so the webview thread sizes it with `MoveWindow` after `navigate()`.
  - The container fills the host window (`fillParent()`) in `set_parent()` and `show()`: REAPER doesn't call `set_size()` when switching from its generic UI to ours.
- **DevTools:** debug builds create the webview with DevTools on (right click → Inspect), for the console and the DOM.
- **Transport:**
  - JS → C++: `bind("postToNative")` → `ReaShaderPlugin::handleWebUIMessage`.
  - C++ → JS: `eval()` → `window.__reashaderOnMessage`.
  - The plugin holds a `WebUISender` (`std::function`), called under its mutex so that `clearWebUISender()` waits for any send in flight. It does nothing when no sender is registered.
- **Ownership:** `ClapPluginState::gui` is a `unique_ptr<Gui, GuiDeleter>`. `Gui` and its deleter are defined in `gui_win32.cpp`, so the shell never needs the full type. `gui_destroy` is just `gui.reset()`: the webview is torn down first, then the window.
- **Frontend:** `index.html` loads plain sequential `<script>` tags (no ES modules, which `file://` blocks). `styles/ui.scss`, with partials in `styles/components/_*.scss`, is compiled by the build to `index.css`.

## 5. The web UI protocol

Messages are plain JSON objects with a `"type"` field. In C++ they're documented and handled in `plugin/plugin.cpp` (the web UI section), with an `if`/`else` on the type. On the JS side, `client.js` switches on the type, and `api.js` sends.

| Direction | Message           | Payload / effect                                                                                                                                                                                                                                                                   |
| --------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| to UI     | `snapshot`        | `{ version, track, params, devices, logo, shader, shaders, lut: { name, mode }, luts }`. The UI rebuilds itself from it (except the status lines), and its shader and LUT lists are rescanned each time. Sent on `ready`, activate, state load, and device, shader and LUT changes |
| to UI     | `paramValue`      | `{ id, value }`: host automation                                                                                                                                                                                                                                                   |
| to UI     | `shaderStatus`    | `{ status, state }`, with state = `busy`/`ok`/`error`                                                                                                                                                                                                                              |
| to UI     | `lutStatus`       | `{ status, state }`, as `shaderStatus`                                                                                                                                                                                                                                             |
| from UI   | `ready`           | —                                                                                                                                                                                                                                                                                  |
| from UI   | `shaderSelect`    | `{ name }`: a compiled shader, `""` = none (passthrough)                                                                                                                                                                                                                           |
| from UI   | `paramValue`      | `{ id, value }`                                                                                                                                                                                                                                                                    |
| from UI   | `renderingDevice` | `{ index }`                                                                                                                                                                                                                                                                        |
| from UI   | `logo`            | `{ enabled }`: the 3D logo, on while the about box is open                                                                                                                                                                                                                         |
| from UI   | `openUrl`         | `{ url }`: `https://` only, opened in the system browser (`util::shell::openUrl`); the webview itself never navigates away                                                                                                                                                         |
| from UI   | `shaderUpload`    | `{ name, source }`: GLSL sent as text                                                                                                                                                                                                                                              |
| from UI   | `lutSelect`       | `{ name }`: a stored LUT, `""` = none                                                                                                                                                                                                                                              |
| from UI   | `lutUpload`       | `{ name, source }`: a `.cube` file sent as text                                                                                                                                                                                                                                    |
| from UI   | `lutMode`         | `{ mode }`: `before` / `after` the shader, or `shader`                                                                                                                                                                                                                             |

## 6. Parameters

`src/plugin/params.*`:

- **`Param`** is one plain struct: id, name (the state key), label (display), group (`Main`, `Lut` or `Shader`), units, default, min, max, automatable. Values are plain numbers within min..max. The defaults are 0..1, and shader params use their `//@param` range. CLAP param info uses the same range.
- **Ids:** a param's id is its index in the list, which is also its CLAP param id. The plugin's fixed params come first (`DefaultId`): `AudioGain` (group `Main`, not shown in the web UI) and `LutMix` (group `Lut`, shown with the LUT). Then come the shader's (group `Shader`), from `DefaultCount` on.
- **`ParamList`:**
  - Metadata is behind a mutex.
  - Values are a fixed array of `std::atomic<double>` (`maxCount` = 256), so the audio and video threads never lock.
  - `replaceShaderParams()` swaps the `Shader` group whenever a shader is loaded.

## 7. The renderer

`src/render/`, explained in depth, barrier by barrier, in [rendering.md](rendering.md). In short:

- **Vulkan 1.3** with dynamic rendering and synchronization2, so there are no render pass or framebuffer objects.
- **The GPU list:** usable GPUs are the ones vk-bootstrap selects, and the UI's device index is an index into that list.
- **Lifetimes: plain structs with `create()`/`destroy()` listing their handles, no deletion queues:**
  - `Context` (instance, device);
  - `FrameTargets` (frame size), with two work images created the first time a chain of passes needs them;
  - `ShaderPass` (per shader);
  - `LutPass` and an identity `Lut` (with the device), and the current `Lut` (per LUT);
  - `Scene` (from the first time the logo is on until the device goes; its depth buffer per frame size).

  A device switch destroys the scene, the passes, the LUTs, the targets and the device, then recreates the device, the LUT pass, the LUT, the shader pass and the scene. The targets come back with the next frame.

- **Errors:** `VK_CHECK` throws `std::runtime_error`, and `ReaShaderRenderer`'s public functions catch everything.
- **Shader changes:** the renderer never compiles. `setShader(CompiledShader)` swaps the pass under `frameMutex`. Frames render one at a time and wait on the fence, so the old pass is idle. With no device (inactive), the shader is kept and installed by the next `init()`. `clearShader()` removes it. With no shader, no LUT and no logo, `renderFrame` returns `false` (passthrough).
- **LUT changes:** the renderer never parses. `setLut(LutData)` uploads the table under `frameMutex` and swaps it in, like `setShader`, and the `LutData` is kept for the next `init()` or device switch. `clearLut()` removes it. `setLutMode()` picks the order.
- **The passes:** each frame, `renderFrame` lists them from the LUT mode (LUT then shader, shader then LUT, or the shader alone with the LUT as its `iChannel1`), and `FrameTargets::recordPasses` chains them: the first samples the input, the last renders to the output, the ones between go through two ping-pong work images. The shader's `iChannel1` is a 17³ identity unless the mode is `shader`.
- **Internal shaders** (`fullscreen.vert`, `lut.frag`, `scene.vert`, `scene.frag`) are SPIR-V arrays compiled at build time, never read from disk.
- **The scene (`scene.*`):**
  - `Mesh` (.obj via tinyobjloader, host-visible vertex/index buffers) and `Texture` (stb, staged to a device image) can be reused for more 3D content.
  - `Scene` holds objects (mesh + texture descriptor set + local transform), a depth buffer (`prepare()` per frame size) and one pipeline (push constant: the object's MVP matrix).
  - It is drawn with `loadOp = LOAD` over the output.
  - It is created on `setLogoEnabled(true)`, or with the device if the logo is on, and destroyed with the device.
  - Camera: z = -5, 70° FOV, the frame's aspect, y flipped via `proj[1][1] *= -1`. The logo spins one degree per video frame (`time * frameRate`), wobbling with time.
- **Frame layouts:** after the passes, `output` is always `COLOR_ATTACHMENT_OPTIMAL` (the last pass, or the plain copy), which `recordDownload()` expects.

## 8. The shader contract

Implemented in `shader_compiler.cpp` (`kShaderPreamble`). The user-facing guide is the [examples README](../src/shaders/examples/README.md).

- **User shaders write only `main()`,** plus an optional `uniform Params { ... };` block.
- **Prepended automatically:**
  - `#version 450`;
  - `in vec2 uv` (0..1, top left = 0,0);
  - `out vec4 fragColor`;
  - `sampler2D iChannel0` (the input frame);
  - `sampler3D iChannel1` (the LUT in `shader` mode, otherwise an identity) and `vec3 iLut(vec3 color)`, which samples it at the texel centers;
  - push constants `iResolution`, `iTime`, `iFrameRate`, `iFrame`.

  A user `#version` is dropped, and `#extension` lines are hoisted above the preamble. `#line 1` keeps error line numbers matching the user's file.

- **`Params`:** members must be `float`/`vec2`/`vec3`/`vec4`. Each component becomes one slider, named `member` or `member.x`, in reflection order. The block is bound to binding 1 automatically (shaderc shifts uniform-block bindings by 1, explicit ones too). `iChannel0` is binding 0 and `iChannel1` binding 2. Any other resource (samplers are shifted to 3 and up), or anything outside descriptor set 0, is rejected with an error.
- **Annotations:** `//@param member 'Label' default min max` (anywhere in the source; label and numbers optional, in that order) sets a slider's label, default and range. Without one: label = member name, default 0.5, range 0..1. This is inspired by REAPER's video processor `//@param`, but keyed by member name, not index.
- **Keep in sync:** `gpu::ShaderInputs` must match `ReaShaderInputs` in the preamble (std430 push-constant layout, 20 bytes). See [rendering.md §8](rendering.md#8-how-to-extend) for adding an input.

## 9. Logging and paths

- **Logging:** `LOG(level, toConsole | toFile | toBox, sender, title, message)` from `util/logging.h`.
- **The log file** is `<plugin dir>/rs.log`. It is kept open, and truncated on the first write of each process.
- **`toBox`:** message boxes are modal, so they are never shown on the calling thread.
  - `LOG` queues the box and calls the host's `request_callback()`.
  - `on_main_thread` then shows it (`showQueuedBoxes()`).
  - Each plugin instance registers the requester in `plugin_init`.
- **Paths:** `util::paths::pluginDir()`, `resourcesDir()`, `uiDir()`, `compiledShadersDir()` and `lutsDir()` return `std::filesystem::path`. Pass `.string()` to narrow file APIs (`fopen`, `ifstream`).
