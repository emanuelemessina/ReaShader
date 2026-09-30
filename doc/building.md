# Building ReaShader

How to build ReaShader from source, deploy it to REAPER, and package the installer.

Contents:

1. [Prerequisites](#1-prerequisites)
2. [Building](#2-building)
3. [Debug and release](#3-debug-and-release)
4. [What the build does](#4-what-the-build-does)
5. [Dependencies](#5-dependencies)
6. [Packaging](#6-packaging)
7. [Platforms](#7-platforms)

---

## 1. Prerequisites

- [CMake](https://cmake.org/) **3.25+** (`CMakePresets.json` uses schema v6) and [Ninja](https://ninja-build.org/).
- [clang](https://releases.llvm.org/) (`clang++`, the GNU driver; not `clang-cl` or MSVC `cl`) on `PATH`. The Windows presets pin `clang`/`clang++`.
- The [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/). Its installer sets `VULKAN_SDK`, which is how the build finds it (`find_package(Vulkan COMPONENTS shaderc_combined)`). glslc (build-time shaders) and shaderc (shaders uploaded at runtime) come with it, and the matching debug variant is picked automatically.
- [Dart Sass](https://sass-lang.com/install/) (`sass`) on `PATH`: `choco install sass`, `npm install -g sass` or `brew install sass/sass/sass`. Configure fails with an install hint without it.
- **The submodules,** which hold every other dependency:

  ```bash
  git submodule update --init --recursive
  ```

  (or clone with `git clone --recurse-submodules`). Configure fails without them.
- **Network, once:** the first configure downloads the WebView2 headers from NuGet, if no system copy is found.
- **Packaging only:** [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`choco install innosetup`, or `winget install --id JRSoftware.InnoSetup --scope user`), and a Visual Studio or Build Tools install, for `vc_redist.x64.exe`.

---

## 2. Building

### VS Code tasks

| Task | Does |
|---|---|
| **build+deploy** (default, Ctrl+Shift+B) | configures (the first time), builds, and deploys the plugin folder to your CLAP folder |
| **test** | builds the plugin without deploying, then builds and runs the test application (see [testing.md](testing.md)) |
| **package** | builds the release preset, then the installer (see [Packaging](#6-packaging)) |
| **clean** | wipes the preset's build directory and its tests' build directory, for a fresh configure |

`build+deploy` and `test` ask for a profile: `debug` or `release`.

### Command line

Every task runs `build.cmake`:

```bash
cmake -DPROFILE=debug -P build.cmake
```

| Option | Effect |
|---|---|
| `-DPROFILE=debug` / `release` | the preset: `windows-debug` or `windows-release` |
| `-DTEST=ON` | build without deploying, then build and run the tests |
| `-DTEST_ARGS="<doctest options>"` | options for the test run, e.g. `--test-suite=render` |
| `-DPACKAGE=ON` | build the installer instead of deploying (release only) |

To build without deploying, use the presets directly:

```bash
cmake --preset windows-debug
```

```bash
cmake --build --preset windows-debug
```

### Deploy

`build+deploy` copies the `.clap` plus `resources/` and `ui/` into their own folder in the per-user CLAP folder. Hosts search CLAP folders recursively.

| OS | CLAP folder |
|---|---|
| Windows | `%LOCALAPPDATA%\Programs\Common\CLAP` |
| macOS | `~/Library/Audio/Plug-Ins/CLAP` |
| Linux | `~/.clap` |

- **REAPER holding the plugin:** if REAPER has the `.clap` open, the deploy is skipped with a warning. Close REAPER and build again.
- **Uploaded shaders survive:** the deploy replaces `resources/images`, `resources/meshes`, `resources/shaders/examples` and `ui` one by one, so `resources/shaders/compiled` (the user's uploaded shaders) is kept.

### IntelliSense

IntelliSense reads `build/windows-debug/compile_commands.json`, which configure writes. On a fresh clone, run the build task once.

---

## 3. Debug and release

Debug and release are **separate plugins**, so both can be installed side by side. `CMakeLists.txt` sets the identity from `CMAKE_BUILD_TYPE` (the `REASHADER_NAME`/`REASHADER_ID` defines and `OUTPUT_NAME`), and `build.cmake` reads the file name from the build tree's cache (`PLUGIN_FILE_NAME`).

| | Release | Debug |
|---|---|---|
| Name in REAPER | `ReaShader` | `ReaShader (Debug)` |
| CLAP id | `com.emanuelemessina.reashader` | `com.emanuelemessina.reashader.debug` |
| Binary / deploy folder | `ReaShader.clap` in `ReaShader/` | `ReaShader-Debug.clap` in `ReaShader-Debug/` |

Projects saved with one don't load the other.

Debug builds also:
- enable the Vulkan validation layer with synchronization validation (see [rendering.md](rendering.md#9-debugging));
- create the webview with DevTools on (right click → Inspect).

---

## 4. What the build does

**Generates**, into `build/<preset>/generated/`:
- `src/shaders/internal/*` → `<name>.inc`: SPIR-V as a C array (glslc `-mfmt=num`), `#include`d by the renderer. Internal shaders are never read from disk.
- `src/ui/styles/ui.scss` → `index.css` (compressed, with `sass`). The CSS is a build output, not committed.

**Stages** next to the `.clap`, on every build (the `stage` target), so that a UI or shader edit alone gets staged too:
- `res/images`, `res/meshes` → `resources/images`, `resources/meshes`;
- `src/shaders/examples` → `resources/shaders/examples`;
- `src/ui` (minus `styles/`, plus `index.css`) → `ui`.

The staged `resources/` and `ui/` are wiped first, so no stale files are left. The plugin finds `resources/` and `ui/` relative to its own binary, so they must travel with it.

**Example shaders** are sources for the user to try, and the plugin never reads that folder. User shaders are compiled only on upload, so the repo has no `.spv` files.

**Version:** `REASHADER_VERSION` comes from the last git tag (through [cmake-git-versioning](https://github.com/emanuelemessina/cmake-git-versioning)). It is shown in the CLAP descriptor and the UI's about box.

---

## 5. Dependencies

- **Everything in `external/` is a git submodule,** pinned (e.g. CLAP at `1.2.10`, webview at `0.12.0`, doctest at `v2.5.3`). Only the Vulkan SDK is installed separately.
- **vk-bootstrap and SPIRV-Reflect are pinned to the installed SDK's version** (`v1.4.357`, `vulkan-sdk-1.4.357.0`). Update them together with the SDK.
- **Third-party code** is compiled with warnings off: its headers are `SYSTEM` includes, and its sources build with `-w`. `spirv_reflect.cpp` exists upstream to build the C file as C++, so the project stays C++ only.
- **GLM and VMA stay submodules** even though the Vulkan SDK ships both. The SDK's GLM is older and doesn't compile with this code, and taking only VMA from the SDK would source dependencies two different ways for no gain.

---

## 6. Packaging

The **`package`** task (`cmake -DPROFILE=release -DPACKAGE=ON -P build.cmake`) builds the release preset, then runs CPack instead of deploying. The output is `build/windows-release/package/ReaShader-<version>-win64-setup.exe`. It's release only, because the debug build needs the debug CRT, which can't be redistributed.

**One CMake description, one native installer per OS:** `install()` rules in `CMakeLists.txt` lay out the plugin folder (`.clap`, `resources/`, `ui/`), and a CPack generator per OS builds the installer. Windows uses CPack's Inno Setup generator, with extras in `installer/windows/`:
- `reashader.iss`: extra sections;
- `installer.pas`: installer code.

**The Windows installer:**
- **Where it installs:** per user, into `%LOCALAPPDATA%\Programs\Common\CLAP\<plugin file name>`, with no admin rights. The folder must be writable for uploaded shaders. Its `AppId` is the CLAP id.
- **Upgrades:** `[InstallDelete]` in `reashader.iss` replaces the shipped folders like the deploy does, and keeps `resources/shaders/compiled`.
- **Uninstall:** asks whether to delete the uploaded shaders (a silent uninstall keeps them), then removes the folders left empty.
- **VC++ runtime:** the release binary imports `MSVCP140`/`VCRUNTIME140`. A static CRT isn't possible, because the SDK's `shaderc_combined` is built with `/MD`. So CMake finds the newest `VC/Redist/MSVC/<version>/vc_redist.x64.exe` and passes its path and version to the script (`CPACK_INNOSETUP_DEFINE_*`). The installer runs it when the registry's runtime (`VisualStudio\14.0\VC\Runtimes\x64`) is missing or older.
- **No tasks page:** CPack always emits a "desktop icon" task, so `installer.pas` skips that page.
- **What the target machine needs:** Vulkan (from the GPU driver) and WebView2 (built into Windows 11).
- **`ISCC`** is looked up in Program Files, Program Files (x86) and `%LOCALAPPDATA%\Programs`.
- **Debugging the installer:** the generated script is `build/windows-release/package/_CPack_Packages/win64/INNOSETUP/ISScript.iss`.

---

## 7. Platforms

Only Windows is built and tested. The presets and `CMakeLists.txt` have macOS and Linux branches, which print `TODO` warnings for what's missing:
- the plugin window (`clap.gui` is Win32 only; webview itself supports WKWebView and WebKitGTK);
- Boxer (message boxes), untested there;
- the installers: only the Windows CPack generator exists.
