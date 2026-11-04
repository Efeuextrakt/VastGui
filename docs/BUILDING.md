# Building from source

[← Back to the README](../README.md)

## Prerequisites

**Windows (primary)**

- MinGW-w64 **g++ 13+** targeting the UCRT, e.g. [MSYS2](https://www.msys2.org) **UCRT64**:
  `pacman -S mingw-w64-ucrt-x86_64-gcc`, with `C:\msys64\ucrt64\bin` (`g++`, `windres`) on `PATH`. Releases and
  CI use this toolchain. The legacy MINGW64 (`msvcrt.dll`) toolchain still builds, but MSYS2 has deprecated it. **Or**:
- **MSVC 2022** (Visual Studio 2022 / Build Tools with the C++ workload).
- **CMake 3.20+** and **Ninja** on `PATH` (MSYS2: `pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`;
  Visual Studio ships both with its "C++ CMake tools for Windows" component).

**Linux (secondary)**: g++ 13+, `libglfw3-dev`, `libgl-dev`, `libcurl4-openssl-dev`.

All other dependencies are vendored as source in `third_party/` (Dear ImGui docking, ImPlot,
nlohmann/json, doctest, IconFontCppHeaders, Monocypher). No package manager, no prebuilt DLLs.

## Build

```bat
build.bat
```

That is the whole build (MinGW-w64, UCRT64). Other modes:

| Command                 | Result                                                             |
|-------------------------|--------------------------------------------------------------------|
| `build.bat`             | release build -> `VastGui.exe`                                     |
| `build.bat debug`       | debug build (`-O0 -g`)                                             |
| `build.bat run`         | build, then launch `VastGui.exe` (combinable: `debug run`, `msvc run`); aborts if it is already running |
| `build.bat test`        | builds and runs the unit tests -> `VastGuiTests.exe`               |
| `build.bat package`     | release build + tests, then `release\VastGui-<ver>-win64.zip`, release notes |
| `build.bat clean`       | removes `build\` and the executables                               |
| `build.bat msvc [mode]` | same with `cl.exe` (`/MT /W4`); finds VS 2022 via `vswhere` if needed |

`build.bat` is a thin wrapper around CMake + Ninja: the build tree lives in `build\cmake-<toolchain>-<config>\`,
all sources compile in parallel, and rebuilds only recompile changed files. The executable is copied to the
project root.

### Making a release

1. Set the version in `CMakeLists.txt` (`project(VastGui VERSION x.y.z)`), `res/app.rc` (`VASTGUI_VERSION_BIN` /
   `VASTGUI_VERSION_STR`) and `src/App.hpp` (`kAppVersion`).
2. Rename `## Unreleased` in `CHANGELOG.md` to `## x.y.z - <date>`.
3. `build.bat package` - builds, runs the tests and writes `release\VastGui-x.y.z-win64\`, the zip (exe, README,
   LICENSE, third-party licenses, changelog, docs) and `release\RELEASE_NOTES-x.y.z.md` (body for the GitHub
   release, with the zip's SHA-256). It stops if the three versions disagree or the changelog has no section for
   the version.

Linux: `./build.sh` (`./build.sh test`, `./build.sh clean`, `./build.sh debug`, `./build.sh run`).

CMake can also be used directly:

```sh
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake && ctest --test-dir build/cmake
```

### Verify the executable is self-contained

```bat
objdump -p VastGui.exe | findstr "DLL Name"
```

Expected (all Windows system DLLs): `bcrypt.dll comdlg32.dll d3d11.dll D3DCOMPILER_47.dll dwmapi.dll GDI32.dll
KERNEL32.dll SHELL32.dll USER32.dll WINHTTP.dll` plus twelve `api-ms-win-crt-*-l1-1-0.dll` (the Universal C
Runtime that ships with Windows 10/11). With MSVC use `dumpbin /dependents VastGui.exe`.
Why each DLL is needed: [DLL_DEPENDENCIES.md](DLL_DEPENDENCIES.md).

## Project layout

```
build.bat  build.sh  CMakeLists.txt  README.md
res/            app.rc (icon, manifest, version info), app.ico, app.manifest, fonts/*.ttf (sources of Fonts.hpp)
src/main.cpp    Win32 + DX11 (per-monitor DPI v2) / GLFW + OpenGL3 setup and main loop
src/App.*       state, navigation, sidebar, docking, status bar, fonts; Theme.* ; Config.* ; Log.* ; Platform.* ;
                SecretBox.* (API key encryption at rest)
src/backend/    ApiCall.hpp, Commands (REST request catalogue), RestBackend, Http, ThreadPool.hpp, VastClient, ApiKey
src/model/      Offer, Instance, Template, Machine (+ tolerant JSON accessors)
src/logic/      QueryBuilder, Metrics, DefectFilter, SearchFilters, Regions, Format, DockerOptions (no ImGui)
src/views/      Search, Instances, Templates, Storage, Serverless, Billing(+Earnings), Team(+Audit logs),
                Keys, Hosting, Console (Activity log page), Settings
src/widgets/    OfferCard, RangeSlider, Toast, Spinner, Badge, Widgets (JSON table/tree, buttons)
src/embedded/   Fonts.hpp - Roboto, Cousine, Font Awesome 6 as compressed C arrays (tools/gen_fonts.bat)
tests/          doctest unit tests with embedded JSON fixtures
tools/          gen_fonts.bat, make_icon.py, binary_to_compressed_c.cpp
```

## Tests

`build.bat test` builds `VastGuiTests.exe` (doctest) and runs it: query builder (filters -> vastai query,
`parse_query` port, REST body, quoting), metrics (TFLOPS/$/hr, DLP/$/hr, division-by-zero/missing values,
percentile colouring, sorting), defective-machine filter (every rule + thresholds), plus backend helpers (REST
URLs, request catalogue incl. the copy / cloud copy / update workers ports, docker options, formatting) and
security checks (API key encryption and settings migration, owner-only files, https-only key transport, redirect
origin checks, request bodies never logged).

## Diagnostics

| Environment variable | Effect |
|----------------------|--------|
| `VASTGUI_SCREENSHOT=<file.bmp>` | writes the back buffer to a BMP after `VASTGUI_SCREENSHOT_FRAME` frames (default 180) |
| `VASTGUI_SCREENSHOT_EXIT=1` | exit after the screenshot |
| `VASTGUI_PAGE=<n>` | start on page *n* (0 = Search, 2 = Instances, 10 = Settings, 12 = Activity log, ...) |
| `VASTGUI_OPEN_TEMPLATE_PICKER=<tab>` | open the Search page's template picker on start (0 = Recommended, 1 = My templates, 2 = Search, -1 = default tab) |

Example - regenerate the README screenshot (`docs/screenshots/search.png`):

```bat
set VASTGUI_SCREENSHOT=shot.bmp & set VASTGUI_SCREENSHOT_FRAME=600 & set VASTGUI_SCREENSHOT_EXIT=1 & VastGui.exe
```
