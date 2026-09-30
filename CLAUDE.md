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

**LUT support (`.cube`) and chains of shaders and LUTs are done** (2026-09-30; how they were built is in `.claude/history.md`, the chains' plan and decisions in [.claude/plan-chains.md](.claude/plan-chains.md)). Other LUT formats are not planned (`.cube` only). There is no queued roadmap: ask the user what's next.

**The fixed param list is done** (2026-09-30), after envelopes were lost on project reopen: how it was found (three REAPER probes) is in `.claude/history.md`, what REAPER does in `doc/gotchas.md`. Every param id exists from the start (Audio Gain + 16 × 40 node slots, index = id), unused slots hidden with an empty name, chain edits via `rescan(INFO | VALUES)`: no restarts. The host sees every param as 0..1 over its range (a range can't change after the first scan). A removed node's uid is reused at once (the user's choice), since REAPER keeps envelopes and modulation after `params.clear`.

**Closed (2026-09-30): a removed node's envelope/LFO can't be pruned from the plugin.** REAPER keeps them on the id after `params.clear` (any flags), and even when the param leaves the list with `rescan(ALL)` for a restart (probed: dropped and restored at once, and dropped until the next edit). REAPER's own Video Processor seems to prune them, but that happens inside REAPER. Documented for users (README) and in `doc/gotchas.md`; don't retry without new information.

**Design on hold: a global param pool** (discussed 2026-09-30; the user chose 40 fixed slots per node for now and may ask to revisit it: "look at the pool design"). Instead of 16 uids × 40 slots, one pool of P ids (e.g. 512 or 640; still a fixed CLAP list, unused ids hidden and nameless):
- each node owns an explicit list of ids, one per slider in its content's order (a LUT: its Mix), allocated when it's added, saved in the state with the node (`"ids": [...]`), restored as is on load; the renderer reads values through that list (`ChainNode` gets the id list instead of `firstParam`/`paramCount`);
- reordering never touches ids, so automation and values stay (as now); letters stay as node tags, no longer tied to ids;
- `nodeSet` (swap): sliders with the same name keep their id (value and automation carry over), gone ones free their ids, new ones take free ids;
- allocation: smallest free ids, contiguous when possible (REAPER lists params by id); a shader needing more than the free ids is rejected (status: "Not enough free sliders: X needs N, M left"), never loaded in part; the add list could show each shader's slider count and the free total;
- migration: v3 onto their old ids (shader 1..n, LUT Mix's), v4 (no id lists) onto today's formula `1 + uid * 40 + slot`, in a new state version;
- costs: an id list per node in the state, allocation and swap logic, more tests; and freed ids are reused by arbitrary nodes' sliders, so REAPER's leftover envelopes/modulation land on less predictable params than a whole letter (the user's concern: interleaving). Gains: no per-shader cap below P, small shaders don't reserve slots.

**The test application is complete** (86 tests). What's left is keeping the fake host faithful to REAPER.

**Host test rules:** host tests talk to the plugin only through CLAP and the REAPER extension, with no test hooks in the plugin. A shader arrives through state (`test::projectState`, which compiles with the linked `gpu::compileShader` as tooling).

**Host facts still to verify in REAPER** (all marked *assumed* in `test/host/`):
- whether `state.load` is ever wrapped in `deactivate` (observed 2026-09-30: opening a project, REAPER activates the plugin *before* `state.load`);
- how and when `request_restart` is answered (the host: `idle()`, `deactivate` + `activate`; the plugin no longer asks for restarts);
- whether `params.clear` with `CLEAR_AUTOMATIONS | CLEAR_MODULATIONS` (sent since 2026-09-30) makes REAPER drop an id's envelope or modulation (with `CLEAR_ALL` alone it didn't; the host keeps them);
- `force_format`, the input frame's row padding, and the parmlist order (the host: CLAP param index order);
- which thread `params.flush` runs on when not processing;
- state streams in chunks (the host: 1000 bytes);

When one is verified, change the host, mark it *observed*, and update `doc/testing.md` §4.

**Not yet checked by the user in REAPER** (as of 2026-09-30; everything else was):
- the `ParamList::takeFlaggedForHost` race fix (web UI slider edits reaching REAPER);
- the reflection check that rejects descriptor sets other than 0 (a user shader with `layout(set = 1)` should show an error in the UI);
- the debug build with sync validation on (performance and `rs.log` in REAPER);
- the rebuilt installer: uninstall asking about uploaded shaders and LUTs, and no tasks page;
- all of LUT support (the manual test in `doc/testing.md` §5, steps 6–8), including the `SHADER_READ` barrier change (`gpu.h`) on an Intel GPU;
- chains (batches 3–4): the chain editor (add, the same shader twice, reorder, bypass, swap, remove, a shader's LUT), automation surviving a reorder, save/reload, a project saved before chains (v3) opening as the matching chain, `grain.frag` (`iFrame`), and REAPER's generic param list labels (`<node>: <slider>`): the manual test in `doc/testing.md` §5, steps 4–9 (the user checked chains, automation included, on 2026-09-30);
- the fixed param list (2026-09-30): envelopes surviving save/reopen and reorders, no restart on chain edits, only used params in REAPER's menus (by node letter), ranges (e.g. pixelate's 1..128 px: REAPER's envelopes, LFO and generic UI work 0..1 but show `64.500`), the "LUT (iLut)" selector only for shaders that use a LUT, and whether the extra `params.clear` flags make REAPER drop a removed node's envelope/modulation. Node ids changed with 40 slots (node 1 starts at 41): envelopes in older test projects need recreating. Also: a shader with more than 40 sliders rejected with a status message (upload, add, swap, project load).

**Repo notes:**
- `.claude/` is in `.gitignore`, but `.claude/history.md` is tracked (moved with `git mv`), so it's still committed. Whether it should stay tracked is the user's call.
- The user starts a new session for the next features. There is no queued roadmap, so ask what's next.
