# How ReaShader is put together

ReaShader is a **CLAP** video-effect plugin for REAPER. It taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back. Its HTML/JS/SCSS UI is embedded in REAPER's FX window through a native webview ([webview/webview](https://github.com/webview/webview)).

This doc maps the pieces: the plugin object and its chain of shaders and LUTs, the video path, the web UI and its protocol, parameters, the renderer, the shader contract and logging. The renderer has its own, deeper guide: [rendering.md](rendering.md).

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
src/plugin/plugin.*          ReaShaderPlugin: the chain, params, state, web UI messages, REAPER video tap
src/plugin/params.*          Param struct + ParamList (lock-free values)
src/render/renderer.*        ReaShaderRenderer: owns the GPU objects below, renders frames, never throws
src/render/context.*         gpu::Context: vk-bootstrap instance/device, queue, command buffer + fence, VMA
src/render/frame_targets.*   gpu::FrameTargets: upload/readback buffers + input/output/work images (per frame size),
                             recordPasses (chains the passes)
src/render/pass.*            gpu::Pass: the fullscreen pass interface, and what passes share (fullscreen.vert, ...)
src/render/shader_compiler.* gpu::compileShader (contract preamble, shaderc, SPIRV-Reflect, //@param), stored JSON form
src/render/shader_pass.*     gpu::ShaderPass: fullscreen pipeline for a compiled shader
src/render/lut.*             gpu::Lut (a LUT as a 3D image) + gpu::LutPass (the frame through a LUT, blended by its Mix)
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

`ReaShaderPlugin` (`src/plugin/plugin.*`) is one object per instance. There's no processor/controller split: it holds the chain, params, state, the web UI connection, the REAPER video tap and the renderer.

### Threads

Listed in `plugin/plugin.h`:

| Thread  | Does                                              |
| ------- | ------------------------------------------------- |
| main    | lifecycle, state, `onMainThread()`                |
| audio   | host param events, lock-free values only          |
| video   | renderer, `try_lock` only                         |
| webview | UI messages, device switch, chain edits, uploads |

### `clap.params`

- **Which params the host sees:** a fixed list of 641 CLAP params, index = id (see [Parameters](#6-parameters)): Audio Gain (host only, not in the web UI), then 40 slots per chain node uid (a shader's sliders, a LUT's Mix). Every id exists from the moment the plugin is created. A slot no node uses is `CLAP_PARAM_IS_HIDDEN` with an empty name, which REAPER leaves out of its menus and generic UI (see [gotchas.md](gotchas.md#reaper)).
- **Host automation** arrives in `process()`/`flush()` (`handleParamEvents()`) and goes into `applyHostParamValue()`, which is lock-free. It then requests a main-thread callback, and `onMainThread()` echoes the values to the web UI.
- **Web UI edits** are flagged with `ParamList::flagForHost()`, plus `host_params->request_flush()`. `takeParamChangeForHost()` drains them into `out_events`, lock-free.

### `clap.state`

One JSON document, version 4:

```
{ version: 4, params: { name: value }, device, logo,
  chain: [ { uid, kind: "shader", name, bypass, data, lut: { name, data } },
           { uid, kind: "lut", name, bypass, data } ] }
```

- **Each node's stored JSON** (`data`: the compiled shader or the LUT, and a shader node's LUT) is embedded, so projects are self-contained and never recompile or re-parse.
- **Invalid nodes** (unknown kind, a uid out of range or repeated, data that doesn't parse) are skipped with a warning.
- **Version 3** (from before chains: one shader, one LUT and a LUT mode) is migrated on load (`migrateV3`): the shader becomes node 0 and the LUT node 1, so their params keep their ids. Mode `before` puts the LUT node first, `after` second, and `shader` attaches the LUT to the shader node. Param names get their node's prefix (`brightness` → `0/brightness`), and `LUT Mix` becomes `1/mix`. Older or unknown state keeps the current state.
- **Param values** are restored by name, once the nodes' params exist.

### The REAPER video tap

`activate()`:

1. calls `host->get_extension(host, "cockos.reaper_extension")`, cast to `reaper_plugin_info_t*`;
2. calls `GetFunc("clap_get_reaper_context")`: with `sel=4` it gives the FxDsp context, with `1` the parent track;
3. calls `GetFunc("video_CreateVideoProcessor")(fxctx, VERSION)`;
4. calls `reaShaderRenderer->init()`.

`deactivate()` deletes the video processor. The renderer stays initialized across activate/deactivate cycles, and a failed renderer starts over on the next `activate()`.

### The chain

The plugin's video effect is an ordered chain of up to 16 nodes (`ReaShaderPlugin::Node`). Each node is a stored shader or a stored LUT:

- **`uid`:** 0..15, the node's for its whole life, shown as a letter tag (`A`..`P`, `tagOf`). Its params' ids (`nodeParamId(uid, slot)`), names (`"<uid>/<member>"`) and labels derive from it, so moving a node never moves its automation. A new node takes the smallest free uid, so a removed node's uid (and its param ids and letter) comes back with the next node, on purpose: REAPER keeps a removed node's envelopes and modulation on its ids (see below), and it's better to see that at once than after 16 nodes.
- **`name` + `data`:** the stored file's name and its JSON, saved in state, and the parsed form (`shared_ptr<const CompiledShader>` or `shared_ptr<const LutData>`) that goes to the renderer. The pointer changes only when the content does, so the renderer keeps the GPU objects of nodes that didn't change.
- **`bypass`:** the node is left out of the frame, but keeps its params.
- **A shader node's LUT** (`lutName` + `lutData`): what the shader samples as `iChannel1` (see [the shader contract](#8-the-shader-contract)); none means an identity.

**Every change goes through `_editChain(edit, paramsChange)`:**

1. under `chainMutex`, it copies the chain and runs `edit` on the copy;
2. it hands the result to the renderer (`setChain`, with each node's param indices, see [The renderer](#7-the-renderer));
3. on success it keeps the copy, and when nodes were added, removed, moved or swapped (`paramsChange`) it replaces the nodes' params right away (`ParamList::replaceNodeParams`), then tells the host (below). Bypass and a shader's LUT change no params.

On any error (from `edit` or the renderer), the chain stays as it was, and the error goes to the UI as `chainStatus`.

**Stored shaders and LUTs:**

- **At most 40 sliders per shader** (`Parameters::kNodeSlots`): an upload with more is rejected and not stored, and a stored or saved one with more (e.g. from an older version) is never added, swapped in or loaded; the error goes to the UI.
- **Compiled or parsed once, on upload (the only place):** `_upload()` runs `gpu::compileShader` or `gpu::parseCube` (`render/lut_file.*`), writes `resources/shaders/compiled/<stem>.json` or `resources/luts/<stem>.json`, then appends a node with it. On a full chain the file is still stored.
- **LUTs:** only `.cube` files are read. A 1D LUT is baked into a 33³ cube and a `DOMAIN` other than 0..1 is resampled onto 0..1, so the stored form is always a 0..1 cube: `{ version, title, size, data }`, where `data` is base64 of half-float RGB (`util/base64.*`).
- **The lists** are the `*.json` files in `util::paths::compiledShadersDir()` and `util::paths::lutsDir()`. Nodes are added (`nodeAdd`) or swapped (`nodeSet`) by name, and names from the UI are reduced to a file name (no paths).
- **Writable plugin folder:** uploading writes into the plugin folder. The per-user CLAP folder is writable; a system-wide install might not be.

**Status:** every outcome goes to the UI as `chainStatus`: what was done (`Added tint`, `Moved invert`, ...) or the error. The UI itself shows `busy` (a spinner) while it waits for an upload.

### Nodes' params are host params (a fixed list, no restarts)

CLAP allows the param list to change size (`CLAP_PARAM_RESCAN_ALL`) only while the plugin is deactivated, and REAPER opens a project by activating the plugin, then loading its state, then binding the project's envelopes by id. So the list never changes size: every node slot exists from the start, and a chain change only renames, shows, hides and sets slots, which the host takes while active:

1. `_editChain` replaces the nodes' params right away (`ParamList::replaceNodeParams`), from any thread, and requests a main-thread callback.
2. `_notifyHostParams()` (from `onMainThread()`, or directly in `loadState()`, which runs on the main thread) calls `host_params->rescan(CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_VALUES)`: new names and visibility, and new values.

- **A loaded state's values** apply with its params, in the same call (`ParamsChange::Load`), so a project's envelopes find their ids and values as soon as `loadState()` returns, whether the plugin is active or not.
- **Params that go away** after an edit from the UI (a node removed, or swapped for content whose param at that id has another name) are cleared in the host with `host_params->clear(id, CLAP_PARAM_CLEAR_ALL | _AUTOMATIONS | _MODULATIONS)`, before the rescan, so its automation doesn't drive whatever takes the id later (`ParamsChange::Edit`). REAPER keeps them anyway (see [gotchas.md](gotchas.md#reaper)). A loaded state clears nothing (`ParamsChange::Load`): the project's envelopes come with it.

**Params per node** (`paramsOfNodes`): a shader node gets one param per slider of its `Params` block, a LUT node one `Mix` (0 = the frame as is, 1 = fully through the LUT, default 1; in a shader node's LUT, blending is up to the shader). Names are `"<uid>/<member>"` (`"3/brightness"`, `"5/mix"`), labels `"[<tag>] <node name>: <label>"` (`"[B] brightness: Brightness"`): the tag tells the same shader twice apart in REAPER's windows, and never changes, because REAPER keeps the name an envelope had when it was created. The web UI shows labels without the prefix, which the card already shows.

### Rendering device

The GPU choice isn't a host param: it lives in state and the web UI. Changing it (from the UI or on state load) calls `changeRenderingDevice()`.

### The logo (an easter egg)

- `showLogo`, off by default, saved in state as `logo`. Not a host param.
- The UI's header logo opens the about box, which sends `logo { enabled: true }`. Closing it (×, a click outside, Escape) sends `false`. The box is open whenever the snapshot's `logo` is true.
- It calls `ReaShaderRenderer::setLogoEnabled()`, and a 3D logo spins in the video window.

### Version

`REASHADER_VERSION`, a compile definition from the last git tag, is used by the CLAP descriptor and the snapshot (the about box).

### Renderer access

`ReaShaderRenderer` reaches plugin data only through `getRenderingDeviceIndex`, `setRenderingDeviceIndex` and `setRenderingDevicesList`. The plugin hands it the chain (`setChain`, built by `rendererChain`), each node naming its params by index in the param list, and the values come in with each frame (`FrameInputs`).

**Locks:** `_editChain` holds `chainMutex` from copying the chain (under `stateMutex`) to the renderer's `setChain`, so two threads never build chains from each other's stale copies. The lock order is `chainMutex`, then the renderer's `frameMutex`, then `stateMutex`.

## 3. The per-frame video path

REAPER calls `ReaShaderPlugin::_processVideoFrame` (`plugin/plugin.cpp`), which `activate()` installs:

1. `vproc->renderInputVideoFrame(0, 'RGBA')` gets the upstream frame. It is immutable, and is `Release()`d before returning.
2. Param values at video time come from `parmlist` (`[0]` = wet/dry, then param `i` at `[i + 1]`, index = id), falling back to `ParamList::value()` for any REAPER doesn't pass.
3. `ReaShaderRenderer::renderFrame()`, under `try_lock(frameMutex)`:
   1. (re)creates `FrameTargets` if the size or row stride changed;
   2. `memcpy`s into the mapped upload buffer, and writes the shader's `Params` into its mapped UBO;
   3. records one command buffer: buffer → input image → the chain's passes (bypassed nodes left out, through work images; a plain copy when there are none) → output image → logo scene on top (if enabled) → readback buffer;
   4. submits once and waits on one fence (a 2 s timeout means a GPU hang, and sets `failed`);
   5. `memcpy`s out into a new `vproc->newVideoFrame`.
4. If `renderFrame` returns `false` (inactive, busy, failed, or no pass and no logo), the input frame is passed through unchanged.

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

### The UI's code

`src/ui/`, staged next to the plugin as `ui/` and loaded from `file://`. Its style rules are in [CONTRIBUTING.md](../CONTRIBUTING.md#frontend-srcui).

```
index.html                   the page's fixed frame: background, logo, the fieldsets #renderingDevice and #chain,
                             the #about box; loads the scripts
scripts/api.js               native: the only sender, one method per message to the plugin
scripts/ui.js                builds the page from a snapshot: renderSnapshot, renderX per section, createX helpers
scripts/client.js            receives: window.__reashaderOnMessage dispatches on type; page events (about box);
                             sends ready last
styles/ui.scss               the page layout and its sections' styles, compiled to index.css
styles/components/           _palette.scss (every color) and one partial per kind of control
```

- **Load order:** plain `<script defer>` tags (no ES modules, which `file://` blocks) in dependency order: `api.js`, `ui.js`, `client.js`. They share the global scope. `client.js` runs last and sends `ready`.
- **The plugin owns the state.** The page asks with `ready`, and the plugin answers with a `snapshot` (and sends a new one after every change: chain edits, device, state load). `renderSnapshot` rebuilds every section from it: each `renderX` empties its container (`replaceChildren`) and builds it again, so rendering the same snapshot twice gives the same page. Between snapshots, the page is only patched by `paramValue` (`setParamValue`, a slider moved by host automation) and `chainStatus` (`setStatus`, the status line, which survives snapshots).
- **User actions only send.** A control's handler calls a `native.*` method and waits for the plugin's answer to change the page. Slow requests (uploads, adding or swapping a node) first show a busy status.
- **Kinds of chain node** differ only through the `KINDS` table in `ui.js` (titles, accepted files, texts, upload call), so the chain editor's code is shared by shaders and LUTs.

**Adding a control that changes the plugin:**
1. the message: a `native` method in `api.js`, and its handler in `ReaShaderPlugin::handleWebUIMessage` (`plugin/plugin.cpp`, documented there);
2. the state it changes goes into the `snapshot` (`plugin.cpp`), and the plugin sends a new snapshot after the change;
3. the control: built in the section's `renderX` in `ui.js` (or a new section: a fieldset in `index.html` and a `renderX` called from `renderSnapshot`), reading its value from the snapshot and calling the `native` method;
4. its styles in `ui.scss` (or a partial, for a new kind of control), with colors from the palette;
5. the [protocol table](#5-the-web-ui-protocol) below, and a `protocol` test (see [testing.md](testing.md)).

## 5. The web UI protocol

Messages are plain JSON objects with a `"type"` field. In C++ they're documented and handled in `plugin/plugin.cpp` (the web UI section), with an `if`/`else` on the type. On the JS side, `client.js` switches on the type, and `api.js` sends.

| Direction | Message           | Payload / effect                                                                                                                                                                                                                                                                                            |
| --------- | ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| to UI     | `snapshot`        | `{ version, track, params, devices, logo, chain, shaders, luts }`, with `chain` = `[{ uid, tag, kind, name, bypass, lut, samplesLut }]` (shader nodes only: `lut`, its LUT's name, and `samplesLut`, whether the shader uses a LUT at all; the UI shows a shader's LUT selector only when it does, or while it has one). The UI rebuilds itself from it (except the status line), and its shader and LUT lists are rescanned each time. Sent on `ready`, activate, state load, device changes and chain changes |
| to UI     | `paramValue`      | `{ id, value }`: host automation                                                                                                                                                                                                                                                                            |
| to UI     | `chainStatus`     | `{ status, state }`, with state = `busy`/`ok`/`error`                                                                                                                                                                                                                                                       |
| from UI   | `ready`           | —                                                                                                                                                                                                                                                                                                           |
| from UI   | `paramValue`      | `{ id, value }`                                                                                                                                                                                                                                                                                             |
| from UI   | `renderingDevice` | `{ index }`                                                                                                                                                                                                                                                                                                 |
| from UI   | `logo`            | `{ enabled }`: the 3D logo, on while the about box is open                                                                                                                                                                                                                                                  |
| from UI   | `openUrl`         | `{ url }`: `https://` only, opened in the system browser (`util::shell::openUrl`); the webview itself never navigates away                                                                                                                                                                                  |
| from UI   | `shaderUpload`    | `{ name, source }`: GLSL sent as text, compiled, stored, and appended as a node                                                                                                                                                                                                                             |
| from UI   | `lutUpload`       | `{ name, source }`: a `.cube` file sent as text, parsed, stored, and appended as a node                                                                                                                                                                                                                    |
| from UI   | `nodeAdd`         | `{ kind, name, index }`: a stored shader or LUT (`kind` = `shader` / `lut`) as a new node at `index` (none, or past the end: last)                                                                                                                                                                         |
| from UI   | `nodeRemove`      | `{ uid }`                                                                                                                                                                                                                                                                                                   |
| from UI   | `nodeMove`        | `{ uid, index }`: past the end is last                                                                                                                                                                                                                                                                      |
| from UI   | `nodeBypass`      | `{ uid, bypass }`                                                                                                                                                                                                                                                                                           |
| from UI   | `nodeSet`         | `{ uid, name }`: another stored shader or LUT, of the node's kind                                                                                                                                                                                                                                           |
| from UI   | `nodeLut`         | `{ uid, name }`: a shader node's LUT (`iChannel1`), a stored LUT, `""` = none                                                                                                                                                                                                                              |

## 6. Parameters

`src/plugin/params.*`:

- **`Param`** is one plain struct: id, name (the state key), label (display), group (`Main` or `Node`), units, default, min, max, and its node's uid (a used `Node` slot). An unused slot has no node, name or label (`Param::used()`). A param's range is min..max: 0..1 by default, a shader param's `//@param` range.
- **The host sees every param as 0..1** over its range (`Param::toHost` / `toReal`): CLAP lets a param's range and default change only with `rescan(ALL)`, which isn't allowed while active, so the host keeps the range it first scanned. `ParamList` stores host values; the plugin converts at the edges: the web UI, the state and `realValue()` use real values, `value_to_text` shows the real value in the host (`64.500`, or `50.0 %` for `%` params) and `text_to_value` reads one, and the renderer maps a shader's slots onto its fields' ranges.
- **A fixed list, index = id** (`kParamCount` = 641): Audio Gain is id 0 (group `Main`, not shown in the web UI), then 16 node uids × 40 slots (group `Node`): a node's params get `nodeParamId(uid, slot)` = `1 + uid * 40 + slot` (node 0's are ids 1..40, node 1's 41..80, and so on). 40 matches REAPER's own video processor, and fits about 99% of shaders in public collections (ISF, OBS shaderfilter, DCTL). The id is the CLAP param id, used by host automation, the web UI's `paramValue`, `ParamList::value()` and REAPER's `parmlist`, and it never changes for a node's param. REAPER lists the used params in id order, i.e. by node tag, not chain order.
- **`ParamList`:**
  - Metadata is behind a mutex. `at(id)` gives any slot, `find(id)` and `list()` the used ones.
  - Values are a fixed array of `std::atomic<double>` by id (host values), and so is `used(id)`, so the audio and video threads never lock.
  - `replaceNodeParams()` sets every node slot whenever the chain's nodes change, and returns the ids that went away (or now hold another param). Each value comes from the loaded state by name, else from the param that had the same id and name (so a moved node keeps its values, clamped to the new range), else from its default.
  - A node keeps at most 40 params (`kNodeSlots`). The plugin never gets that far: a shader with more sliders is rejected wherever it would enter the chain (`checkSliders`: on upload, where it isn't stored; `nodeAdd`; `nodeSet`; a loaded state, where the node is left out and the UI told), never loaded in part.

## 7. The renderer

`src/render/`, explained in depth, barrier by barrier, in [rendering.md](rendering.md). In short:

- **Vulkan 1.3** with dynamic rendering and synchronization2, so there are no render pass or framebuffer objects.
- **The GPU list:** usable GPUs are the ones vk-bootstrap selects, and the UI's device index is an index into that list.
- **Lifetimes: plain structs with `create()`/`destroy()` listing their handles, no deletion queues:**
  - `Context` (instance, device);
  - `FrameTargets` (frame size), with two work images created the first time a chain of passes needs them;
  - per chain node: its pass (`ShaderPass` or `LutPass`) and its `Lut`, kept across `setChain` calls while the node's content is the same;
  - an identity `Lut` (with the device);
  - `Scene` (from the first time the logo is on until the device goes; its depth buffer per frame size).

  A device switch destroys the scene, the nodes' objects, the identity, the targets and the device, then recreates the device, the identity, the nodes' objects and the scene. The targets come back with the next frame.

- **Errors:** `VK_CHECK` throws `std::runtime_error`, and `ReaShaderRenderer`'s public functions catch everything.
- **The chain:** the renderer never compiles or parses. `setChain(nodes)` takes an ordered list of `ChainNode`s: a shader (`shared_ptr<const CompiledShader>`, with an optional LUT as its `iChannel1`) or a LUT (`shared_ptr<const LutData>`), a uid, a bypass flag, and the node's params (a range of indices into `FrameInputs::paramValues`). At most 16 nodes, uids unique. Under `frameMutex`, it builds the objects of nodes that are new or whose content changed (by uid and pointer), keeps the others, and swaps; on error the current chain stays and the error is returned. With no device (inactive), the chain is kept and installed by the next `init()`. With no pass (every node bypassed, or none) and no logo, `renderFrame` returns `false` (passthrough).
- **The passes:** each frame, `renderFrame` lists the non-bypassed nodes' passes in order, and `FrameTargets::recordPasses` chains them: the first samples the input, the last renders to the output, the ones between go through two ping-pong work images. A shader node without a LUT gets a 17³ identity as `iChannel1`. Each shader pass counts its own `iFrame`.
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
  - `sampler3D iChannel1` (the shader node's LUT, otherwise an identity) and `vec3 iLut(vec3 color)`, which samples it at the texel centers. `CompiledShader::samplesLut` says whether `main()` reaches `iChannel1` (directly or through `iLut`): SPIRV-Reflect's entry-point bindings, computed on compile and on `fromJson`, never stored;
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
