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

**LUT support (`.cube`) is done** (2026-09-30; how it was built is in `.claude/history.md`). Next feature: **arbitrary chains of shaders and LUTs in one instance**. The detailed plan, with design, batches, tests and open questions, is in [.claude/plan-chains.md](.claude/plan-chains.md): read it first and follow it batch by batch. Other LUT formats are not planned (`.cube` only). **Batches 1 (stable param ids) and 2 (renderer chain) are done** (2026-09-30); next is batch 3 (plugin node model, state v4, protocol).

**The test application is complete** (69 tests). What's left is keeping the fake host faithful to REAPER.

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
- the rebuilt installer: uninstall asking about uploaded shaders and LUTs, and no tasks page;
- all of LUT support (the manual test in `doc/testing.md` §5, steps 6–8), including the `SHADER_READ` barrier change (`gpu.h`) on an Intel GPU;
- stable param ids (chains batch 1): host automation of LUT Mix and shader sliders, web UI slider edits reaching REAPER, and whether a project saved before it (LUT Mix was id 1, now 65; shader params were 2.., now 1..) keeps its automation envelopes;
- the renderer chain (chains batch 2): a shader and a LUT in each mode (`before`, `after`, `shader`), LUT Mix, switching the shader or LUT while playing, switching the GPU, and `iFrame` in a shader that uses it.

**Repo notes:**
- `.claude/` is in `.gitignore`, but `.claude/history.md` is tracked (moved with `git mv`), so it's still committed. Whether it should stay tracked is the user's call.
- The user starts a new session for the next features. There is no queued roadmap, so ask what's next.
