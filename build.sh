#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# VastGui build script for Linux (GLFW + OpenGL 3, libcurl for the vast.ai REST API)
#
#   ./build.sh            release build  -> ./VastGui
#   ./build.sh debug      debug build
#   ./build.sh run        build, then launch ./VastGui (combinable: debug run)
#   ./build.sh test       build + run unit tests -> ./VastGuiTests
#   ./build.sh clean      remove build/ and binaries
#
# Requires: g++ 13+ (or clang++ 17+ via CXX=clang++), libglfw3-dev, libgl-dev,
#           libcurl4-openssl-dev.
# Third-party objects are cached in build/obj/linux-<config>/tp and compiled in
# parallel; the application is a single unity translation unit.
# ---------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")"

MODE=release
RUN=0
for arg in "$@"; do
    case "$arg" in
        release|debug|test|clean) MODE="$arg" ;;
        run) RUN=1 ;;
        *) echo "ERROR: unknown option '$arg' (release|debug|run|test|clean)"; exit 1 ;;
    esac
done
CXX="${CXX:-g++}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [[ "$MODE" == "clean" ]]; then
    rm -rf build VastGui VastGuiTests
    echo "Cleaned."
    exit 0
fi

if (( RUN )); then
    for pid in $(pgrep -x VastGui || true); do
        exe="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
        if [[ "${exe% (deleted)}" == "$PWD/VastGui" ]]; then
            echo "ERROR: ./VastGui is running (pid $pid) - close it first."
            exit 1
        fi
    done
fi

CFG="$MODE"
[[ "$MODE" == "test" ]] && CFG=release
if [[ "$CFG" == "debug" ]]; then OPT="-O0 -g"; else OPT="-O2 -DNDEBUG"; fi

OBJ="build/obj/linux-$CFG"
mkdir -p "$OBJ/tp"
INC="-Isrc -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/imgui/misc/cpp -Ithird_party/implot -Ithird_party/IconFontCppHeaders"
CURL_LIB="-lcurl"
CXXFLAGS="-std=c++20 -pipe $OPT"
# The app only plots double; ImPlot's default of 10 numeric types makes implot_items.cpp take ~70 s to compile.
TP_FLAGS="-DIMPLOT_CUSTOM_NUMERIC_TYPES=(double)"

TP_SOURCES=(
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp third_party/imgui/misc/cpp/imgui_stdlib.cpp
    third_party/implot/implot.cpp third_party/implot/implot_items.cpp third_party/monocypher/monocypher_cpp.cpp
)
TEST_SOURCES=(
    tests/tests_main.cpp tests/test_query_builder.cpp tests/test_metrics.cpp tests/test_defect_filter.cpp
    tests/test_backend.cpp tests/test_security.cpp tests/test_gpu_catalog.cpp tests/test_api_permissions.cpp src/logic/QueryBuilder.cpp src/logic/SearchFilters.cpp src/logic/Metrics.cpp
    src/logic/DefectFilter.cpp src/logic/Regions.cpp src/logic/Format.cpp src/logic/DockerOptions.cpp src/logic/GpuCatalog.cpp src/logic/ApiKeyPermissions.cpp
    src/backend/Commands.cpp src/backend/RestBackend.cpp src/backend/Http.cpp
    src/backend/ApiKey.cpp src/Platform.cpp src/Log.cpp src/SecretBox.cpp src/Config.cpp
    src/backend/VastClient.cpp third_party/monocypher/monocypher_cpp.cpp
)

if [[ "$MODE" == "test" ]]; then
    echo "Building VastGuiTests"
    $CXX $CXXFLAGS $INC -Wall -Wextra "${TEST_SOURCES[@]}" -o VastGuiTests -static-libgcc -static-libstdc++ $CURL_LIB -pthread
    echo "Running tests"
    ./VastGuiTests
    exit $?
fi

echo "[1/3] Third-party objects (cached in $OBJ/tp, $JOBS jobs)"
pids=()
for src in "${TP_SOURCES[@]}"; do
    obj="$OBJ/tp/$(basename "${src%.cpp}").o"
    if [[ ! -f "$obj" ]]; then
        echo "      $(basename "$src")"
        $CXX $CXXFLAGS $TP_FLAGS $INC -w -c "$src" -o "$obj" &
        pids+=($!)
        if (( ${#pids[@]} >= JOBS )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
    fi
done
for p in "${pids[@]}"; do wait "$p"; done

echo "[2/3] Application (unity build)"
$CXX $CXXFLAGS $INC -Wall -Wextra -c src/unity.cpp -o "$OBJ/unity.o"

echo "[3/3] Linking VastGui"
$CXX -o VastGui "$OBJ/unity.o" "$OBJ"/tp/*.o -static-libgcc -static-libstdc++ -lglfw -lGL $CURL_LIB -pthread -ldl
[[ "$CFG" == "release" ]] && strip VastGui
echo "Built ./VastGui"
if (( RUN )); then
    echo "Launching ./VastGui"
    nohup ./VastGui >/dev/null 2>&1 &
    disown
fi
