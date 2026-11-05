# CMake build, compiled in parallel by Ninja. build.bat (Windows) is a thin wrapper
# around it; build.sh (Linux) still calls the compiler directly. Direct use:
#
#   cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/cmake
#   ctest --test-dir build/cmake
cmake_minimum_required(VERSION 3.20)
project(VastGui VERSION 1.4.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    # Match build.bat / build.sh (CMake's GCC/Clang Release default would be -O3).
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)
set(INCLUDES src ${TP} ${TP}/imgui ${TP}/imgui/backends ${TP}/imgui/misc/cpp ${TP}/implot ${TP}/IconFontCppHeaders)

# ---- third-party (no warnings) ----------------------------------------------
set(TP_SOURCES
    ${TP}/imgui/imgui.cpp ${TP}/imgui/imgui_draw.cpp ${TP}/imgui/imgui_tables.cpp ${TP}/imgui/imgui_widgets.cpp
    ${TP}/imgui/misc/cpp/imgui_stdlib.cpp ${TP}/implot/implot.cpp ${TP}/implot/implot_items.cpp)
if(WIN32)
    list(APPEND TP_SOURCES ${TP}/imgui/backends/imgui_impl_win32.cpp ${TP}/imgui/backends/imgui_impl_dx11.cpp)
else()
    list(APPEND TP_SOURCES ${TP}/imgui/backends/imgui_impl_glfw.cpp ${TP}/imgui/backends/imgui_impl_opengl3.cpp)
endif()
add_library(vastgui_thirdparty STATIC ${TP_SOURCES})
target_include_directories(vastgui_thirdparty PUBLIC ${INCLUDES})
target_compile_options(vastgui_thirdparty PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)
# The app only plots double; ImPlot's default of 10 numeric types makes implot_items.cpp take ~70 s to compile.
target_compile_definitions(vastgui_thirdparty PUBLIC "IMPLOT_CUSTOM_NUMERIC_TYPES=(double)")
if(WIN32)
    target_compile_definitions(vastgui_thirdparty PUBLIC UNICODE _UNICODE _WIN32_WINNT=0x0A00)
endif()

# ---- UI-independent core (logic, model, backend) ------------------------------
# Monocypher (XChaCha20-Poly1305 for the API key at rest), compiled as C++ through a one-line wrapper.
add_library(vastgui_crypto STATIC ${TP}/monocypher/monocypher_cpp.cpp)
target_include_directories(vastgui_crypto PUBLIC ${TP})
target_compile_options(vastgui_crypto PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)

add_library(vastgui_core STATIC
    src/logic/QueryBuilder.cpp src/logic/SearchFilters.cpp src/logic/Metrics.cpp src/logic/DefectFilter.cpp
    src/logic/Regions.cpp src/logic/Format.cpp src/logic/DockerOptions.cpp src/logic/GpuCatalog.cpp
    src/backend/Commands.cpp src/backend/RestBackend.cpp src/backend/Http.cpp
    src/backend/ApiKey.cpp src/backend/VastClient.cpp src/Platform.cpp src/Log.cpp
    src/SecretBox.cpp)
target_include_directories(vastgui_core PUBLIC ${INCLUDES})
target_link_libraries(vastgui_core PUBLIC vastgui_crypto)
target_compile_options(vastgui_core PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W4 /utf-8 /permissive- /Zc:__cplusplus,-Wall -Wextra>)
if(WIN32)
    target_compile_definitions(vastgui_core PUBLIC UNICODE _UNICODE _WIN32_WINNT=0x0A00 NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(vastgui_core PUBLIC winhttp comdlg32 shell32 ole32 bcrypt)
else()
    find_package(CURL REQUIRED)  # all vast.ai calls go through the REST API
    target_link_libraries(vastgui_core PUBLIC CURL::libcurl)
    find_package(Threads REQUIRED)
    target_link_libraries(vastgui_core PUBLIC Threads::Threads)
endif()

# ---- application ----------------------------------------------------------------
add_executable(VastGui WIN32
    src/main.cpp src/App.cpp src/Config.cpp src/Theme.cpp
    src/views/BillingView.cpp src/views/ConsoleView.cpp src/views/HostingView.cpp src/views/InstancesView.cpp
    src/views/KeysView.cpp src/views/SearchView.cpp src/views/ServerlessView.cpp src/views/SettingsView.cpp
    src/views/StorageView.cpp src/views/TeamView.cpp src/views/TemplatesView.cpp
    src/widgets/Badge.cpp src/widgets/OfferCard.cpp src/widgets/RangeSlider.cpp src/widgets/Spinner.cpp
    src/widgets/Toast.cpp src/widgets/Widgets.cpp)
target_compile_options(VastGui PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W4 /utf-8 /permissive- /Zc:__cplusplus,-Wall -Wextra>)
target_link_libraries(VastGui PRIVATE vastgui_core vastgui_thirdparty)
if(WIN32)
    target_sources(VastGui PRIVATE res/app.rc)
    target_link_libraries(VastGui PRIVATE d3d11 dxgi d3dcompiler dwmapi gdi32 user32 imm32)
    if(MSVC)
        # app.rc already embeds app.manifest; link.exe's default /MANIFEST:EMBED would add a duplicate (CVT1100).
        target_link_options(VastGui PRIVATE /MANIFEST:NO)
    endif()
    if(MINGW)
        target_link_options(VastGui PRIVATE -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    target_link_libraries(VastGui PRIVATE glfw OpenGL::GL ${CMAKE_DL_LIBS})
    target_link_options(VastGui PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---- tests ------------------------------------------------------------------------
add_executable(VastGuiTests tests/tests_main.cpp tests/test_query_builder.cpp tests/test_metrics.cpp
                            tests/test_defect_filter.cpp tests/test_backend.cpp tests/test_security.cpp
                            tests/test_gpu_catalog.cpp src/Config.cpp)
target_link_libraries(VastGuiTests PRIVATE vastgui_core)
if(MINGW)
    target_link_options(VastGuiTests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
enable_testing()
add_test(NAME VastGuiTests COMMAND VastGuiTests)
