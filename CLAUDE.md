# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

ReaShader is a **CLAP** video-effect plugin for REAPER: it taps REAPER's video frames, runs them through a Vulkan pipeline (GLSL shaders) and hands them back, with an HTML/JS/SCSS UI embedded in REAPER's FX window through a native webview.

## Where things are

The project's conventions and descriptions live in human docs, which are the single source of truth. These are imported here:

- @CONTRIBUTING.md (workflow, runtime rules, code style, which doc to update for which change)
- @doc/architecture.md (layout, plugin, video path, web UI + protocol, params, renderer summary, shader contract, logging)
- @doc/building.md (prerequisites, tasks, debug vs release, what the build does, dependencies, packaging)
- @doc/gotchas.md (platform/toolchain traps, debugging crashes and hangs)

Read these when working in their area (not imported, they're long):
- [doc/rendering.md](doc/rendering.md): the renderer for readers who don't know Vulkan, every barrier of a frame, lifetimes and decisions.
- [doc/testing.md](doc/testing.md): the test application, the fake REAPER host, the manual test in REAPER.
- [src/shaders/examples/README.md](src/shaders/examples/README.md): the shader contract for users.

How the code got here (the VST3 → CLAP migration, rejected alternatives, past crash investigations) is in [.claude/history.md](.claude/history.md).

## Working rules for Claude

- **Changes:** before a batch of changes, give the user a brief rationale and wait for approval.
- **Commits:** the user commits. Don't run `git commit` unless asked.
- **The user edits files in parallel,** sometimes while a task is running, and other Claude sessions may be working in the repo. Re-read a file right before editing it, and take the version on disk as current.
- **Where content goes:**
  - human docs (`README.md`, `CONTRIBUTING.md`, `doc/`) describe what exists: no roadmaps, in-progress designs, process rules for Claude, history, or references to CLAUDE.md;
  - roadmaps, in-progress designs and Claude-specific rules go here;
  - investigation narratives and history go in `.claude/history.md`, never in code comments.
- **When a feature lands,** describe it in the human docs listed in CONTRIBUTING.md §4, in the same batch.

## Open items

**In progress: LUT support (`.cube` only), 4 batches, each approved by the user before it starts.** Decided with the user:
- **Staged:** one LUT per instance now, arbitrary shader/LUT chains later. So the renderer becomes an ordered pass list with ping-pong work images now.
- **LUT mode** (UI + state, not a host param): `before` (LUT pass → shader), `after` (shader → LUT pass, default), `shader` (no LUT pass; the user shader samples `iChannel1` via `iLut()`; with no shader, it falls back to a LUT pass).
- **"LUT Mix"** host param at id 1 (shader ids shift; breaking old projects is OK), with state going to version 3.

Batches:
1. **Done:** `render/lut_file.*` (parser, 1D/domain baking, stored form = base64 half floats), `util/base64.*`, `cases/lut.cpp`, `test/luts/`.
2. **Done:** renderer: `Image::create3D`, `render/lut.*` (`gpu::Lut` 3D RGBA16F image, `gpu::LutPass` + `internal/lut.frag`), a `gpu::Pass` interface, `FrameTargets::recordPasses` (ping-pong `work[2]`, used by `test::render` too), `iChannel1` at binding 2 (texture base → 3, identity dummy unless mode `shader`), renderer `setLut`/`clearLut`/`setLutMode`, render tests, `doc/rendering.md`. Found on the way: Intel's driver ignores `VK_ACCESS_2_SHADER_SAMPLED_READ_BIT` for texture-cache invalidation, so every barrier before sampling uses `SHADER_READ_BIT` (comment in `gpu.h`, decision in `doc/rendering.md` §7).
3. **Done:** plugin: `LutMix` param (`Group::Lut`), `paths::lutsDir()` (`resources/luts`), `_uploadLut`/`_useLut`/`_clearLut`, protocol `lutUpload`/`lutSelect`/`lutMode` → `lutStatus` and snapshot `lut {name, mode}` + `luts`, state v3 `lut {name, mode, data}`, test updates (param counts, `projectState`), a host scenario. Docs: `doc/architecture.md` (params, state, protocol, shaders → LUTs, renderer access), `doc/testing.md` (suites, host scenario).
4. UI (`fieldset#lut`, generic picker/status), installer (uninstall asks about shaders and LUTs), `lut_split.frag` example + README. Docs: `README.md` (what it does, using it, plugin folder `resources/luts`), `doc/building.md` (packaging: uninstall, upgrades keep `resources/luts`; deploy keeps it), `doc/testing.md` §5 (a LUT step; "Audio Gain" then "LUT Mix" before the shader's params).

**The test application is complete** (all 4 phases, 37 tests). What's left is keeping the fake host faithful to REAPER.

**Host test rules:** host tests talk to the plugin only through CLAP and the REAPER extension, with no test hooks in the plugin. A shader arrives through state (`test::projectState`, which compiles with the linked `gpu::compileShader` as tooling).

**Host facts still to verify in REAPER** (all marked *assumed* in `test/host/`):
- whether `state.load` comes before or after `activate`, and whether it's wrapped in `deactivate`;
- how and when `request_restart` is answered (the host: `idle()`, `deactivate` + `activate`);
- `force_format`, the input frame's row padding, and the parmlist order (the host: CLAP param index order);
- which thread `params.flush` runs on when not processing;
- state streams in chunks (the host: 1000 bytes);
- the plugin counting as inactive from the start of `deactivate`, where it calls `rescan(ALL)` (the host allows that).

When one is verified, change the host, mark it *observed*, and update `doc/testing.md` §4.

**Not yet checked by the user in REAPER** (as of 2026-09-30; everything else was):
- the `ParamList::takeFlaggedForHost` race fix (web UI slider edits reaching REAPER);
- the reflection check that rejects descriptor sets other than 0 (a user shader with `layout(set = 1)` should show an error in the UI);
- the debug build with sync validation on (performance and `rs.log` in REAPER);
- the rebuilt installer: uninstall asking about uploaded shaders, and no tasks page.

**Repo notes:**
- `.claude/` is in `.gitignore`, but `.claude/history.md` is tracked (moved with `git mv`), so it's still committed. Whether it should stay tracked is the user's call.
- The user starts a new session for the next features. There is no queued roadmap, so ask what's next.
