@echo off
rem ---------------------------------------------------------------------------
rem VastGui build script - wrapper around CMake + Ninja (parallel, incremental)
rem
rem   build.bat                release build with MinGW-w64 g++ (MSYS2 UCRT64) -> VastGui.exe
rem   build.bat debug          debug build (-O0 -g)
rem   build.bat test           build + run unit tests              -> VastGuiTests.exe
rem   build.bat package        release build + tests + zip         -> release\VastGui-<ver>-win64.zip
rem   build.bat clean          remove build\ and the executables
rem   build.bat msvc [mode]    same, but with MSVC cl.exe (/MT, /W4)
rem
rem Build trees live in build\cmake-<toolchain>-<config>; Ninja compiles all
rem sources in parallel and only recompiles what changed.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "MODE=release"
set "TOOLCHAIN=gcc"
for %%A in (%*) do (
    if /i "%%A"=="release" set "MODE=release"
    if /i "%%A"=="debug"   set "MODE=debug"
    if /i "%%A"=="test"    set "MODE=test"
    if /i "%%A"=="package" set "MODE=package"
    if /i "%%A"=="clean"   set "MODE=clean"
    if /i "%%A"=="msvc"    set "TOOLCHAIN=msvc"
    if /i "%%A"=="cl"      set "TOOLCHAIN=msvc"
)

if "%MODE%"=="clean" (
    if exist build rmdir /s /q build
    if exist VastGui.exe del /q VastGui.exe
    if exist VastGuiTests.exe del /q VastGuiTests.exe
    echo Cleaned.
    exit /b 0
)

set "CFG=%MODE%"
if "%MODE%"=="test" set "CFG=release"
if "%MODE%"=="package" set "CFG=release"
if "%CFG%"=="debug" (set "BUILD_TYPE=Debug") else (set "BUILD_TYPE=Release")
set "BDIR=build\cmake-%TOOLCHAIN%-%CFG%"

if "%TOOLCHAIN%"=="msvc" (
    where cl >nul 2>nul
    if errorlevel 1 (
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
        if not exist "!VSWHERE!" (echo ERROR: cl.exe not found and Visual Studio not detected. & exit /b 1)
        for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
        if not defined VSINSTALL (echo ERROR: no Visual Studio C++ toolset found. & exit /b 1)
        call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
    )
    set "COMPILER=cl"
) else (
    where g++ >nul 2>nul || (echo ERROR: g++ not found on PATH. Install MinGW-w64 ^(g++ 13+^). MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc - or use "build.bat msvc". & exit /b 1)
    set "COMPILER=g++"
)
where cmake >nul 2>nul || (echo ERROR: cmake not found on PATH. MSYS2: pacman -S mingw-w64-ucrt-x86_64-cmake & exit /b 1)
where ninja >nul 2>nul || (echo ERROR: ninja not found on PATH. MSYS2: pacman -S mingw-w64-ucrt-x86_64-ninja & exit /b 1)

if not exist "%BDIR%\build.ninja" (
    cmake -S . -B "%BDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_CXX_COMPILER=%COMPILER% || exit /b 1
)

if "%MODE%"=="test" (
    cmake --build "%BDIR%" --target VastGuiTests || exit /b 1
    copy /y "%BDIR%\VastGuiTests.exe" VastGuiTests.exe >nul || exit /b 1
    echo Running tests
    VastGuiTests.exe
    exit /b !ERRORLEVEL!
)

if "%MODE%"=="package" (
    cmake --build "%BDIR%" --target VastGui VastGuiTests || exit /b 1
    echo Running tests
    "%BDIR%\VastGuiTests.exe" || (echo ERROR: tests failed - not packaging. & exit /b 1)
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 -Exe "%BDIR%\VastGui.exe" || exit /b 1
    copy /y "%BDIR%\VastGui.exe" VastGui.exe >nul 2>nul || echo Note: VastGui.exe in the project root is in use and was not updated.
    exit /b 0
)

cmake --build "%BDIR%" --target VastGui || exit /b 1
copy /y "%BDIR%\VastGui.exe" VastGui.exe >nul || exit /b 1
echo.
echo Built VastGui.exe
exit /b 0