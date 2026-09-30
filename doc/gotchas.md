# Gotchas and debugging

Traps in the platform, the SDKs and the toolchain that have cost real time, and how to debug a crash or hang inside REAPER without a debugger IDE.

Contents:

1. [Gotchas](#1-gotchas)
2. [Debugging crashes and hangs](#2-debugging-crashes-and-hangs)

## 1. Gotchas

### REAPER

- **`video_frame.h` needs `wdltypes.h` included first:** it uses `WDL_FIXALIGN` and `INT_PTR` without including them.
- **`IVideoFrame::get_bits()` returns `char*`** in the vendored SDK. Cast at the call site.
- **`'RGBA'` frames are B,G,R,A in memory** (byte 0 = B). See [CONTRIBUTING.md](../CONTRIBUTING.md#2-runtime-rules) for the other runtime rules.
- **REAPER binds a CLAP plugin's envelopes by param id, and freezes their names.** A project stores an FX envelope as `<PARMENV <index>:<CLAP id> ... "<param name> / <FX name>"`. The index follows the param list, but the name is the one the param had when the envelope was created, and a later rescan doesn't update it. So param labels must never change for the same param (a position in a label goes stale in envelopes).
- **REAPER opens a project by activating the plugin, then loading its state, then binding the envelopes by id.** Params that appear later (on a main-thread callback, or after a restart) aren't found: the envelope loses its param, or lands on another one (e.g. Audio Gain). Since `rescan(ALL)` isn't allowed while active, every id must exist from the start: that's why the param list is fixed.
- **A CLAP param's range and default are fixed after the first scan:** changing them needs `rescan(ALL)`, which isn't allowed while active (`rescan(INFO)` covers only the name, module, hidden and periodic flags). REAPER then clamps and maps automation and modulation with the old range. So every param is 0..1 to the host, mapped to its real range in the plugin.
- **REAPER keeps a removed param's envelope and parameter modulation** (a `PROGRAMENV` block in the project) on its id, even after the plugin calls `params.clear` (observed with `CLEAR_ALL`). Deleting the envelope lane doesn't remove the modulation (a separate REAPER feature, turned off in its own window), whether or not the param still exists. Whatever later takes that id is driven by them.
- **REAPER and hidden params:** a `CLAP_PARAM_IS_HIDDEN` param is left out of the generic UI but still listed, greyed, in the FX's param menus (track envelope, modulation, learn), unless its name is empty: then it's in none. `rescan(INFO)` (names, hidden) is honored while active, and an envelope stays bound to its id while the param is hidden.

### Build

- **The project only enables `CXX`.** A `.c` file needs `C` added to `project(LANGUAGES ...)`, or else CMake silently skips it and the failure shows up later as a link error.
- **A bizarre native crash is usually a compiler warning that got ignored.** A function missing its `return` compiles, and clang can turn it into a hardware trap (`STATUS_ILLEGAL_INSTRUCTION`). Grep the build log for `-Wreturn-type`/`-Wuninitialized` before suspecting the toolchain.
- **vk-bootstrap and SPIRV-Reflect are pinned to the installed Vulkan SDK's version.** A different SDK may need them updated (see [building.md](building.md#5-dependencies)).
- **GLM from the Vulkan SDK doesn't compile with this code:** it's older than the submodule, even though its `GLM_VERSION` macro looks the same. Check `GLM_VERSION_MINOR`/`PATCH` in `detail/setup.hpp` instead.

### Vulkan

- **Combine flags into the `...Flags` type** (e.g. `VkShaderStageFlags`), never the `...FlagBits` enum. `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT` stored as a `FlagBits` is a different, invalid value.

### Windows

- **`<shellapi.h>` goes after `<windows.h>`.**
- **No `NOMINMAX` is defined** in the plugin, so `std::max`/`std::min` break in files that include `<windows.h>`. Use plain comparisons there. (The test application's `test/host/reaper_sdk.h` defines it, for test code only.)
- **clang's GNU driver doesn't define `UNICODE`,** so the bare Win32 macros (`GetModuleFileName`, ...) resolve to the ANSI `...A` versions. Call the explicit `...W` functions.
- **`CreateWindowExW` with `WS_CHILD` and a null parent fails** with error 1406 (`ERROR_TLW_WITH_WSCHILD`), even though the window class is registered. Create the window in `set_parent()`, where the host provides the parent, not in `gui::create()`.
- **The GUI container needs `WS_EX_CONTROLPARENT`.** REAPER's FX window is a dialog. Once the webview has keyboard focus, the dialog's tab navigation (`GetNextDlgTabItem`) starts from the focused window and climbs its parents. It can only climb out through parents marked `WS_EX_CONTROLPARENT`, so an unmarked container makes it loop forever on the main thread: REAPER "Not Responding" at 100% of one core.
- **Error codes:** look a Win32 error up in `winerror.h` rather than trusting memory. 1406 is easy to misread as a class-registration problem.

### Web UI

- **`webview::terminate()` isn't cross-thread-safe on Win32** (a bare `PostQuitMessage`), despite the library's docs. Dispatch it onto the webview thread (see [architecture.md](architecture.md#4-the-embedded-web-ui)).
- **webview's window classes are process-wide as shipped.** webview registers `webview_widget` and `webview_message` under `GetModuleHandle(nullptr)`, the _host's_ module (reaper.exe), not the plugin's. Every plugin in the process that links webview then shares those classes: the first one loaded owns their window procedures, and the next one's windows run the first one's code with its own data. With ReaShader and ReaShader (Debug) in one session, that crashed REAPER inside the other plugin's `.clap`. The build patches a copy of the header to register them under the plugin's own module (see [building.md](building.md#4-what-the-build-does)). Don't include `external/webview/.../webview.h` directly, bypassing that copy.
- **UTF-16 frontend files break the UI:** a `file://` page gets no encoding detection, so UTF-16 shows as garbage text. Keep them UTF-8, and check with `file` after rewriting one.
- **ES modules don't load from `file://`.** Use plain `<script>` tags in dependency order.

## 2. Debugging crashes and hangs

No WinDbg needed: `lldb` and `llvm-symbolizer` come with clang.

### Crash dumps

- Windows Error Reporting writes full dumps to `%LOCALAPPDATA%\CrashDumps\reaper.exe.<pid>.dmp`.
- List crash records in PowerShell:

  ```powershell
  Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'}
  ```

- Open a dump with `lldb -c <dump>`, then run `thread list` / `bt all`.
- If a stack won't unwind, run `memory read --format A --count 3000 $rsp` and look for `_CxxThrowException` and return addresses inside `ReaShader-Debug.clap` (or `ReaShader.clap` for release).
- **An exception escaping into REAPER** shows up as `abort()` with exception `0x40000015` at a fixed offset "inside reaper.exe". Look for the plugin frame that threw.

### Symbolizing

```bash
llvm-symbolizer --obj=build/windows-debug/ReaShader-Debug.clap <address>
```

where `<address>` is the crash address − the module base + `0x180000000`. This only works if the binary hasn't been rebuilt since the crash.

### Hangs

Attach to the running REAPER and dump every thread's stack:

```powershell
"" | lldb -p <pid> -o "bt all" -o "process detach" -o quit
```

(not `--batch`). Get the module base with `(Get-Process -Id <pid>).Modules`.

### GPU faults

- Recurring `nvlddmkm` events in the Windows System log mean the Vulkan code is doing something invalid.
- In the plugin, a GPU hang shows up as "Rendering failed" (the 2-second fence timeout), then passthrough until re-activation.

### Vulkan validation

- Debug builds request the Khronos validation layer (if installed), with synchronization validation on. Warnings and errors go to `rs.log` next to the plugin.
- Normal use produces none, so **any message is a bug**. The test application fails a GPU test on any of them.
- `VK_LOADER_DEBUG=layer` shows whether the loader found the layer.
- See [rendering.md §9](rendering.md#9-debugging) for the renderer's side, and [testing.md](testing.md) for running the GPU code without REAPER (`--test-suite=render`).
