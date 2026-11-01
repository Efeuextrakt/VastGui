# VastGui.exe – DLL dependency report

This file explains why `VastGui.exe` (Windows, MinGW-w64 GCC, `-static -static-libgcc -static-libstdc++`) imports each DLL.

Import table (`objdump -p VastGui.exe | findstr "DLL Name"`):

```
bcrypt.dll    comdlg32.dll d3d11.dll  D3DCOMPILER_47.dll  dwmapi.dll  GDI32.dll
KERNEL32.dll  msvcrt.dll   SHELL32.dll USER32.dll         WINHTTP.dll
```

They are all Windows system DLLs that live in `%WINDIR%\System32`, so the
executable runs on its own without any redistributable.

## Summary

| DLL | Pulled in by | Purpose in VastGui | Required at runtime? |
|-----|--------------|--------------------|----------------------|
| `KERNEL32.dll` | App code, CRT, libstdc++, ImGui | Core OS services: files, env vars, UTF-8/UTF-16 conversion, threads | Yes, needed by every program |
| `USER32.dll` | `src/main.cpp`, ImGui Win32 backend, ImGui core | Window, message loop, input, clipboard, DPI | Yes |
| `GDI32.dll` | ImGui Win32 backend | DPI fallback (`GetDeviceCaps`) and the region used for alpha compositing | Loaded; rarely called (see notes) |
| `d3d11.dll` | `src/main.cpp`, ImGui DX11 backend | Direct3D 11 device and swap chain used for rendering | Yes |
| `D3DCOMPILER_47.dll` | ImGui DX11 backend | Compiles ImGui's vertex and pixel shaders at startup | Yes |
| `dwmapi.dll` | ImGui Win32 backend | `ImGui_ImplWin32_EnableAlphaCompositing()` | Loaded; the code path is never called (see notes) |
| `WINHTTP.dll` | `src/backend/Http.cpp` | HTTPS calls to the vast.ai REST API | Yes |
| `SHELL32.dll` | `src/Platform.cpp`, ImGui core | Opens URLs and folders in the default browser or Explorer | Yes |
| `comdlg32.dll` | `src/Platform.cpp` | Native Open/Save file dialogs | Yes |
| `bcrypt.dll` | `src/Platform.cpp` | `BCryptGenRandom`: random key and nonces for encrypting the API key, unique temp file names | Yes |
| `msvcrt.dll` | MinGW-w64 C runtime | C runtime (`malloc`, `printf`, `fopen`, `strtod`, …) that libstdc++ is built on | Yes |

## Details per DLL

### KERNEL32.dll – core OS API
Every Win32 program uses it. In this project it is used directly for:
- **Platform helpers** ([src/Platform.cpp](../src/Platform.cpp)): `MultiByteToWideChar` / `WideCharToMultiByte` (UTF-8 ↔ UTF-16), `GetEnvironmentVariableW` (`VAST_API_KEY`, `APPDATA`, `USERPROFILE`, …), `GetModuleFileNameW` (locates the exe for portable mode), `CreateFileW(CREATE_NEW)` (exclusive creation of `secret.key` and temp files).
- **Indirect use**: `std::thread`/`std::mutex` (thread pool), `std::filesystem`, and file I/O in the statically linked libstdc++ and winpthreads.

### USER32.dll – windowing and input
- [src/main.cpp](../src/main.cpp): `RegisterClassExW`, `CreateWindowExW`, `LoadIconW` (icon from `res/app.rc`), `ShowWindow`, `UpdateWindow`, `PeekMessageW`/`DispatchMessageW` (message loop), `GetWindowPlacement` (saves the window position), `MessageBoxW` (error if D3D11 init fails).
- ImGui Win32 backend (`third_party/imgui/backends/imgui_impl_win32.cpp`): keyboard and mouse input, cursor shapes, `MonitorFromWindow`, `GetDC`/`ReleaseDC`. It looks up the DPI-awareness functions with `GetProcAddress`.
- ImGui core (`imgui.cpp`): clipboard (`OpenClipboard`, `SetClipboardData`, …) for copy and paste in text fields.

### GDI32.dll – legacy graphics (minimal use)
Only the ImGui Win32 backend uses it:
- `GetDeviceCaps(LOGPIXELSX/Y)` is the DPI fallback when `shcore.dll!GetDpiForMonitor` is missing (Windows < 8.1).
- `CreateRectRgn` / `DeleteObject` are used in `ImGui_ImplWin32_EnableAlphaCompositing()`.

The backend requests the library with `#pragma comment(lib, "gdi32")`, and
CMake links `gdi32`. On Windows 10/11 the DLL is loaded but almost never called.

### d3d11.dll – Direct3D 11 renderer
- [src/main.cpp](../src/main.cpp): `D3D11CreateDeviceAndSwapChain` creates the device with the hardware driver. If that fails, it falls back to `D3D_DRIVER_TYPE_WARP` (software rasterizer).
- ImGui DX11 backend (`imgui_impl_dx11.cpp`): creates buffers, textures (font atlas, icons), and states, and draws the UI.

DXGI is used through COM interfaces that the D3D11 device returns. That is
why `dxgi.dll` is linked in CMake but does not appear in the import table.

### D3DCOMPILER_47.dll – HLSL shader compiler
- ImGui DX11 backend: `D3DCompile()` compiles the small vertex shader (`vs_4_0`) and pixel shader (`ps_4_0`) when the device objects are created. The backend also links it with `#pragma comment(lib, "d3dcompiler")`.
- The DLL ships with Windows 8.1 and later (and with updated Windows 7). If it is missing, the renderer cannot start. Removing the dependency would require precompiled shader bytecode in place of `D3DCompile`.

### dwmapi.dll – Desktop Window Manager
- ImGui Win32 backend: `DwmIsCompositionEnabled`, `DwmGetColorizationColor`, and `DwmEnableBlurBehindWindow` inside `ImGui_ImplWin32_EnableAlphaCompositing()`. The backend requests it with `#pragma comment(lib, "dwmapi")`, and CMake links `dwmapi`.
- VastGui never calls `ImGui_ImplWin32_EnableAlphaCompositing()`. The import is there only because the backend is compiled in and the linker does not strip unused functions. The DLL is still loaded at startup, which is harmless because it exists on every supported Windows version.

### WINHTTP.dll – HTTPS client
- [src/backend/Http.cpp](../src/backend/Http.cpp) uses the `WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, `WinHttpSendRequest`, `WinHttpReceiveResponse`, `WinHttpQueryHeaders`, `WinHttpReadData`, `WinHttpSetOption` (TLS 1.2/1.3, gzip decompression), and `WinHttpSetTimeouts` functions, among others.
- This is the transport for every call to the vast.ai API, such as offers, instances, billing, and keys.
- It uses the system TLS stack (Schannel) and system proxy settings, so no OpenSSL or libcurl is bundled. On Linux, libcurl takes its place.

### SHELL32.dll – shell integration
- [src/Platform.cpp](../src/Platform.cpp): `ShellExecuteW(..., "open", ...)` backs `platform::openUrl` / `platform::openPath`. It is used for:
  - "Add credit" and other links to cloud.vast.ai (Billing, Instances, Storage, Team, Settings views)
  - opening Jupyter for an instance
  - "Open config folder" in Settings
- ImGui core: `ShellExecuteW` as the default handler for opening links (`TextLinkOpenURL`).

### comdlg32.dll – common dialogs
- [src/Platform.cpp](../src/Platform.cpp): `GetOpenFileNameW` / `GetSaveFileNameW` in `platform::openFileDialog` / `saveFileDialog`. They are used for:
  - selecting an SSH public key (Keys view)
  - exporting CSV (Billing, Team audit logs), saving instance logs, and saving the console log

### msvcrt.dll – C runtime
- MinGW-w64 GCC targets the system `msvcrt.dll` by default. libstdc++ and libgcc are linked statically (`-static-libstdc++ -static-libgcc`). The C runtime itself is still the system DLL, which supplies memory allocation, stdio, string and number conversion, locale, and similar functions.
- No Visual C++ Redistributable (`vcruntime140.dll`, `msvcp140.dll`) is needed.

## Linked in CMake but not imported

`CMakeLists.txt` lists some libraries that do not show up in the import table:

| Library | Why it is not imported |
|---------|------------------------|
| `dxgi` | Only COM interfaces obtained from the D3D11 device are used. No `dxgi.dll` export is called. |
| `ole32` | No `Co*` function is referenced in the final binary. |
| `imm32` | ImGui turns off its default IME functions under non-MSVC compilers (`IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS`), so `ImmGetContext` and related functions are not compiled in. |

## Loaded dynamically at runtime (not in the import table)

The ImGui Win32 backend loads these with `LoadLibraryA`/`GetModuleHandleA` + `GetProcAddress`. If any of them is missing, the code skips that feature:

| DLL | Purpose | If missing |
|-----|---------|------------|
| `shcore.dll` | `SetProcessDpiAwareness`, `GetDpiForMonitor` (per-monitor DPI on Windows 8.1+) | Falls back to `GetDeviceCaps` (GDI32) |
| `xinput1_4.dll` … `xinput1_1.dll` | Gamepad navigation | Gamepad support is disabled |
| `ntdll.dll` | `RtlVerifyVersionInfo` for Windows version checks | Always present |
| `user32.dll` | `SetProcessDpiAwarenessContext`, `AdjustWindowRectExForDpi` (Windows 10+) | Older DPI APIs are used |
