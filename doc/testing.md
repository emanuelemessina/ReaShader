# Testing ReaShader

ReaShader has a **test application**: a program that exercises the plugin so new features and stability can be checked automatically, **before** the manual test in REAPER. It lives in `test/`, fully separate from the plugin's build.

It tests at two levels:
- **Unit tests** compile pieces of the plugin's source (the renderer, the shader compiler, the parameter list, the plugin class for its web UI protocol) into the test program and call them directly.
- **Host tests** load the **built plugin** (`build/windows-<profile>/ReaShader[-Debug].clap`) into a fake REAPER and drive it through CLAP and the REAPER API, the way REAPER does.

Contents:

1. [Running the tests](#1-running-the-tests)
2. [Layout](#2-layout)
3. [Writing a test](#3-writing-a-test)
4. [The fake REAPER host](#4-the-fake-reaper-host)

---

## 1. Running the tests

**VS Code:** run the **`test`** task and pick the profile (debug or release). It:
1. builds the plugin's preset, like `build+deploy`, but **doesn't deploy**;
2. configures (first time only) and builds the test application in `build/tests-<profile>/`;
3. runs it, and fails the task if any test fails.

**Command line:**

```
cmake -DTEST=ON -P build.cmake
cmake -DPROFILE=release -DTEST=ON -P build.cmake
cmake -DTEST=ON -DTEST_ARGS="--test-suite=render" -P build.cmake
```

Or run the binary directly, which is faster when only tests changed: `build/tests-debug/reashader_tests`. It takes [doctest's command line](https://github.com/doctest/doctest/blob/master/doc/markdown/commandline.md):

| Option | Effect |
|---|---|
| `--test-suite=render` | only one area (suites are listed in [Layout](#2-layout)) |
| `--test-case="*logo*"` | only matching test cases |
| `--success` | also print passing assertions |
| `--list-test-cases` | list everything |
| `--help` | all options |

**Output:** a summary line, `[doctest] Status: SUCCESS!` or `FAILURE!`, and exit code 0 on success. Each failure prints the file and line, the expression with both sides' values, and the context (e.g. which GPU).

**Tests are manual.** They don't run on `build+deploy`, so an experiment can still be deployed to REAPER while a test is red.

**Profiles:**
- **Debug** runs the GPU code with the Vulkan validation layer, including synchronization validation (see [rendering.md](rendering.md#9-debugging)), and any validation message fails the test that caused it.
- **Release** runs without validation, closer to what users run.

---

## 2. Layout

```
test/
  CMakeLists.txt       the test application's own CMake project (never included by the main build)
  main.cpp             doctest's runner, plus teardown of the shared GPU instance
  support/             helpers shared by the cases (no tests here): support.* for unit tests, host_helpers.h for host tests
  host/                the fake REAPER host (no tests here; Windows only)
  cases/               the tests: one file per area, each a TEST_SUITE
  shaders/             fixtures: broken.frag
  luts/                fixtures: invert.cube, curve_1d.cube, domain.cube, broken.cube
```

Test suites:

| Suite | File | Covers |
|---|---|---|
| `params` | `cases/params.cpp` | the parameter list: the fixed Audio Gain, the shader group (ids, values saved by name or defaults, replacement, the size limit), values to and from JSON, `toJson` for the UI, params flagged for the host taken once with their latest value. |
| `protocol` | `cases/protocol.cpp` | the web UI protocol on a plugin that's never activated (no GPU): `ready`'s snapshot, `paramValue` (to the host through a flush, unknown ids ignored), host automation echoed on the main thread, `shaderUpload` (stored, loaded, params rescanned; a broken one keeps the current shader), `shaderSelect` (by name, `""` for none, no paths), `logo` and `renderingDevice` saved with the project, malformed messages ignored. Uploads are stored in `resources/shaders/compiled` next to the test binary, emptied at the start of each test. `openUrl` isn't tested, since a valid URL opens the browser. |
| `shader_compiler` | `cases/shader_compiler.cpp` | the shader contract: the examples compile, `Params` reflection and offsets, `//@param`, error line numbers, rejected resources, the stored JSON form. No GPU. |
| `lut` | `cases/lut.cpp` | the `.cube` parser: the table as written (red fastest), comments/CRLF/unknown keywords, a 1D LUT baked into a cube, a `DOMAIN` resampled onto 0..1, errors with file and line, the stored JSON form at half precision, base64. No GPU. |
| `render` | `cases/render.cpp` | the renderer's building blocks on **every GPU**, checked pixel by pixel: an example shader at an odd width with padded rows, `Params` values and B,G,R,A order, defaults for params not given, the logo scene over a plain copy. |
| `host` | `cases/host_lifecycle.cpp` | the built plugin in the fake host: its descriptor, the initial param list, activate/process/deactivate twice (audio unchanged at gain 1, the video processor created and deleted), video passthrough with no shader, destroying an active plugin. |
| `host` | `cases/host_scenarios.cpp` | project scenarios: a shader arriving with a project while active (restart, rescan, its params, frames through it) or before activation (no restart); param values at video time vs. the plugin's own; the state round trip (shader, values by name, logo); the logo over video; an unrecognized state. |

**The build (`test/CMakeLists.txt`)** follows the main build's structure:
- it compiles the plugin sources under unit test (`src/plugin/*`, `src/render/*`, `src/util/*`, everything except the CLAP shell and the GUI) straight into `reashader_tests`, with the plugin's warning flags;
- the host tests load the plugin built by the main build's preset of the same profile (`REASHADER_CLAP`), which the `test` task builds first;
- it compiles the internal shaders with `glslc`, like the main build;
- it stages `res/meshes` and `res/images` next to the binary, because the render code finds `resources/` next to its own binary.

**Framework:** [doctest](https://github.com/doctest/doctest), a single header, as the submodule `external/doctest` (pinned to `v2.5.3`).

---

## 3. Writing a test

One file per area in `test/cases/`, all in the same format. Add a new file to `target_sources` in `test/CMakeLists.txt`.

```cpp
#include "support/support.h"

#include <doctest/doctest.h>

TEST_SUITE("area")
{
    TEST_CASE("what it checks, as a sentence")
    {
        REQUIRE(precondition);     // stops this test case if false
        CHECK(a == b);             // records a failure and continues
        CHECK_THROWS_WITH(f(), doctest::Contains("message part"));
        INFO("context: ", value);  // printed with any failure after it, in this scope
    }
}
```

**Rules:**
- **Name tests as a sentence** saying what they check. Failures print it.
- **Prefer `CHECK` over `REQUIRE`**, except for a precondition the rest of the test can't do without (e.g. a list's size before indexing it).
- **Leave `std::rand`, timing and wall-clock values out of checks:** results must be the same on every run and every GPU. Tolerances (e.g. ±1 per channel) are fine where the GPU rounds.

**GPU tests** go through `test::forEachGpu`:

```cpp
test::forEachGpu([&](gpu::Context& context) {
    gpu::ShaderPass pass;
    pass.create(context, shader);
    gpu::FrameTargets targets;
    test::render(context, targets, input, output, { .pass = &pass, .params = { 0.2f } });
    CHECK(...);
    pass.destroy(context);    // destroy what you created, before the device goes
    targets.destroy(context);
});
```

- **One Vulkan instance for the whole run.** `forEachGpu` creates a device per GPU, runs the body, then destroys the device, even if a `REQUIRE` throws.
- **Failures are tagged with the GPU** (`GPU 0: <name>`).
- **Validation:** any validation message logged meanwhile (debug builds) fails the test. The messages come from `rs.log` next to the test binary.
- **`test::render`** records a frame exactly like `ReaShaderRenderer::renderFrame`: upload → shader pass (or a plain copy) → scene → download.
- **`test::TestFrame(width, height, padding)`** is a BGRA frame with `padding` extra bytes per row, like REAPER's row stride. Use odd widths and padded rows where layout matters.

---

## 4. The fake REAPER host

`test/host/` is a small REAPER stand-in, used by the `host` suite. It is our **model of REAPER**: what the plugin can expect from REAPER is written down there, as code.

**What it does** (`host::Reaper`, `test/host/reaper.h`):
- **Loading:** `LoadLibrary` on the `.clap`, `clap_entry.init`, the plugin factory. The thread that creates the `host::Reaper` is REAPER's **main thread**. There is one `host::Reaper` at a time, because REAPER's `GetFunc` has no context argument.
- **One plugin instance**, driven like an FX on a track: `createPlugin()` (`create_plugin` + `init` + a param scan), `activate()`, `deactivate()`, `destroyPlugin()`.
- **REAPER's threads:** `start_processing`, `process` and `stop_processing` run on an **audio** thread, and `process_frame` runs on a **video** thread (`host::HostThread`, `test/host/thread.h`). Each call waits for its thread, so a test reads top to bottom, while the plugin still sees the calls come from the right threads.
- **`idle()`** is REAPER's main-thread timer: it calls `on_main_thread` when the plugin asked for a callback, restarts the plugin (`deactivate` + `activate`) when it asked for a restart, and flushes params when asked.
- **Audio:** `processAudio(blocks, input)` runs stereo 256-sample blocks of a constant input and returns the output.
- **Video:** `renderVideo(frame, time)` hands the frame to `process_frame` as REAPER's upstream frame, with wet/dry 1 and the host's param values, and returns what the plugin returned. It also says whether that was the input frame itself (passthrough).
- **State:** `saveState()` / `loadState(data)`, on the main thread, like saving and opening a project. The host feeds the state to the plugin in chunks of 1000 bytes.
- **Automation:** `automate(id, value)` changes the host's value right away (what `process_frame` gets), and queues a CLAP param event for the next audio block. `params()` and `param(name)` are the host's view after the last scan. `pluginValue(id)` asks the plugin (`params.get_value`).
- **The REAPER API** the plugin uses, through the `cockos.reaper_extension` host extension and its `GetFunc`:
  - `clap_get_reaper_context` (4 = the FX's FxDsp, 1 = its track);
  - `video_CreateVideoProcessor`, which returns a `host::VideoProcessor` that the plugin deletes on deactivate;
  - `GetSetMediaTrackInfo(P_NAME)` and `GetMediaTrackInfo_Value(IP_TRACKNUMBER)`.
- **Frames** (`host::VideoFrame`, `test/host/video.h`) are reference-counted, `'RGBA'` (B,G,R,A in memory), with padded rows (`host::rowspanFor`).

**What it checks.** `reaper.problems()` lists everything that went wrong. Every host test ends by failing on each one. It contains:
- **message boxes** the plugin showed: a watcher closes them within ~20 ms, so a run never hangs;
- **Vulkan validation messages** in the plugin's `rs.log` (debug builds), which the host deletes before loading the plugin;
- **broken contracts:**
  - `params.rescan(ALL)` while active or off the main thread;
  - a `GetFunc` name the host doesn't emulate;
  - a second video processor created before the first was deleted;
  - frames the plugin neither returned nor released;
  - `process_frame` returning nothing.

**Observed and assumed.** Every REAPER behavior in the host is commented as either:
- **observed:** verified in REAPER (e.g. the REAPER extension and the contexts 4 and 1, which the plugin's working video tap relies on);
- **assumed:** not verified yet (e.g. the order of calls in `idle()`, the frames' row padding, `force_format` 0, state in 1000-byte chunks, the plugin counting as inactive from the start of `deactivate`).

When REAPER turns out to behave differently from the host, fix the host first and mark it observed. Then add a test that fails the way REAPER did, and fix the plugin.

**Shaders in host tests** arrive the way they do in a saved project, with no test hooks in the plugin. `test::projectState("brightness.frag", logo)` (`support/host_helpers.h`) compiles an example shader with the shader compiler, as the plugin would have on upload, and embeds it in a state document. Load it with `loadState()`, then `idle()` to let the plugin apply the new params (a restart if it's active).

**A host test:**

```cpp
TEST_CASE("what it checks")
{
    host::Reaper reaper(host::builtPlugin());
    reaper.createPlugin();
    reaper.activate();

    host::Reaper::VideoResult result = reaper.renderVideo(frame, 0.0);
    CHECK(result.passthrough);

    reaper.idle();
    reaper.destroyPlugin();
    test::checkNoProblems(reaper); // FAIL_CHECK on each of reaper.problems()
}
```
