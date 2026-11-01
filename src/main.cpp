// Platform entry point: Win32 + DirectX 11 on Windows, GLFW + OpenGL 3 elsewhere.

#include "App.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "views/SearchView.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace {

// Main OS window placement, persisted as a custom section in imgui.ini.
struct Placement {
    int x = 0, y = 0, w = 0, h = 0;
    bool maximized = false;
    bool valid = false;
};
Placement g_placement;
Placement (*g_queryPlacement)() = nullptr;

void registerPlacementHandler() {
    ImGuiSettingsHandler h;
    h.TypeName = "VastGui";
    h.TypeHash = ImHashStr("VastGui");
    h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
        return std::string_view(name) == "Window" ? &g_placement : nullptr;
    };
    h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        auto* p = static_cast<Placement*>(entry);
        int a = 0, b = 0;
        if (std::sscanf(line, "Pos=%d,%d", &a, &b) == 2) {
            p->x = a;
            p->y = b;
        } else if (std::sscanf(line, "Size=%d,%d", &a, &b) == 2) {
            p->w = a;
            p->h = b;
            p->valid = a > 200 && b > 150;
        } else if (std::sscanf(line, "Maximized=%d", &a) == 1) {
            p->maximized = a != 0;
        }
    };
    h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
        // The query falls back to the last known placement, so the final save after the window is gone still works.
        const Placement p = g_queryPlacement ? g_queryPlacement() : g_placement;
        if (!p.valid) return;
        buf->appendf("[%s][Window]\nPos=%d,%d\nSize=%d,%d\nMaximized=%d\n\n", handler->TypeName, p.x, p.y, p.w, p.h,
                     p.maximized ? 1 : 0);
    };
    ImGui::AddSettingsHandler(&h);
}

void setupContexts(vast::App& app) {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = app.iniPath();
    registerPlacementHandler();
    ImGui::LoadIniSettingsFromDisk(app.iniPath());
}

}  // namespace

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    T* get() const { return p; }
};

struct D3D {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    bool occluded = false;
    UINT resizeW = 0, resizeH = 0;

    void createRenderTarget() {
        ComPtr<ID3D11Texture2D> back;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(back.put())))) {
            device->CreateRenderTargetView(back.get(), nullptr, rtv.put());
        }
    }

    bool create(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                                   &sd, swapChain.put(), device.put(), &got, context.put());
        if (hr == DXGI_ERROR_UNSUPPORTED) {
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                               swapChain.put(), device.put(), &got, context.put());
        }
        if (FAILED(hr)) return false;
        ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(swapChain->GetParent(IID_PPV_ARGS(factory.put())))) {
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        createRenderTarget();
        return true;
    }
};

D3D g_d3d;
vast::App* g_app = nullptr;
HWND g_hwnd = nullptr;

// Writes the current back buffer as a 32-bit top-down BMP (diagnostics / README screenshots).
void saveBackBuffer(const char* path) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(g_d3d.swapChain->GetBuffer(0, IID_PPV_ARGS(back.put())))) return;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(g_d3d.device->CreateTexture2D(&desc, nullptr, staging.put()))) return;
    g_d3d.context->CopyResource(staging.get(), back.get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(g_d3d.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return;
    FILE* f = std::fopen(path, "wb");
    if (f) {
        const std::uint32_t w = desc.Width, h = desc.Height, imageSize = w * h * 4;
        BITMAPFILEHEADER fh{};
        BITMAPINFOHEADER ih{};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(ih);
        fh.bfSize = fh.bfOffBits + imageSize;
        ih.biSize = sizeof(ih);
        ih.biWidth = static_cast<LONG>(w);
        ih.biHeight = -static_cast<LONG>(h);
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        ih.biCompression = BI_RGB;
        std::fwrite(&fh, sizeof(fh), 1, f);
        std::fwrite(&ih, sizeof(ih), 1, f);
        std::vector<std::uint8_t> row(w * 4);
        for (std::uint32_t y = 0; y < h; ++y) {
            const auto* src = static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch;
            for (std::uint32_t x = 0; x < w; ++x) {
                row[x * 4 + 0] = src[x * 4 + 2];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 0];
                row[x * 4 + 3] = 255;
            }
            std::fwrite(row.data(), row.size(), 1, f);
        }
        std::fclose(f);
    }
    g_d3d.context->Unmap(staging.get(), 0);
}

// Keeps g_placement in sync with the window: the normal (restored) rectangle in screen coordinates and the
// maximized flag. Minimized windows keep their last values.
void trackPlacement() {
    if (!g_hwnd || !IsWindow(g_hwnd) || IsIconic(g_hwnd)) return;
    g_placement.maximized = IsZoomed(g_hwnd) != FALSE;
    if (g_placement.maximized) return;
    RECT r{};
    if (!GetWindowRect(g_hwnd, &r)) return;
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 200 || h <= 150) return;
    g_placement.x = r.left;
    g_placement.y = r.top;
    g_placement.w = w;
    g_placement.h = h;
    g_placement.valid = true;
}

Placement queryPlacement() {
    trackPlacement();
    return g_placement;
}

void placementChanged() {
    trackPlacement();
    if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) placementChanged();
            if (wParam == SIZE_MINIMIZED) return 0;
            g_d3d.resizeW = LOWORD(lParam);
            g_d3d.resizeH = HIWORD(lParam);
            return 0;
        case WM_EXITSIZEMOVE:
            placementChanged();
            break;
        case WM_CLOSE:
            trackPlacement();  // last chance while the window still exists
            break;
        case WM_DPICHANGED: {
            const float scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
            if (g_app) g_app->setDpiScale(scale);
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 800;
            mmi->ptMinTrackSize.y = 500;
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nShowCmd) {
    ImGui_ImplWin32_EnableDpiAwareness();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto app = std::make_unique<vast::App>();
    g_app = app.get();
    setupContexts(*app);
    g_queryPlacement = &queryPlacement;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = L"VastGuiWindow";
    RegisterClassExW(&wc);

    const float primaryScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = static_cast<int>(1440 * primaryScale), h = static_cast<int>(900 * primaryScale);
    if (g_placement.valid) {
        RECT r{g_placement.x, g_placement.y, g_placement.x + g_placement.w, g_placement.y + g_placement.h};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            x = g_placement.x;
            y = g_placement.y;
            w = g_placement.w;
            h = g_placement.h;
        }
    }
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"VastGui - vast.ai desktop", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                             nullptr, hInstance, nullptr);
    if (!g_hwnd || !g_d3d.create(g_hwnd)) {
        MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"VastGui", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, g_placement.valid && g_placement.maximized ? SW_SHOWMAXIMIZED : (nShowCmd ? nShowCmd : SW_SHOWDEFAULT));
    UpdateWindow(g_hwnd);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_d3d.device.get(), g_d3d.context.get());
    app->init(ImGui_ImplWin32_GetDpiScaleForHwnd(g_hwnd));

    const float clear[4] = {0.055f, 0.07f, 0.125f, 1.0f};
    // Diagnostics: VASTGUI_SCREENSHOT=<file.bmp> dumps the back buffer after VASTGUI_SCREENSHOT_FRAME frames.
    const char* shot = std::getenv("VASTGUI_SCREENSHOT");
    const int shotFrame = std::getenv("VASTGUI_SCREENSHOT_FRAME") ? std::atoi(std::getenv("VASTGUI_SCREENSHOT_FRAME")) : 180;
    int frameNo = 0;
    if (const char* page = std::getenv("VASTGUI_PAGE")) app->navigate(static_cast<vast::Page>(std::atoi(page)));
    if (const char* tab = std::getenv("VASTGUI_OPEN_TEMPLATE_PICKER")) app->searchView().openTemplatePicker(std::atoi(tab));
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_d3d.occluded && g_d3d.swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            app->client().poll();
            Sleep(15);
            continue;
        }
        g_d3d.occluded = false;
        if (g_d3d.resizeW != 0 && g_d3d.resizeH != 0) {
            g_d3d.rtv.reset();
            g_d3d.swapChain->ResizeBuffers(0, g_d3d.resizeW, g_d3d.resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_d3d.resizeW = g_d3d.resizeH = 0;
            g_d3d.createRenderTarget();
        }

        app->preFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app->frame();
        ImGui::Render();
        ID3D11RenderTargetView* rtv = g_d3d.rtv.get();
        g_d3d.context->OMSetRenderTargets(1, &rtv, nullptr);
        g_d3d.context->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (shot && ++frameNo == shotFrame) {
            saveBackBuffer(shot);
            if (std::getenv("VASTGUI_SCREENSHOT_EXIT")) done = true;
        }
        const HRESULT hr = g_d3d.swapChain->Present(1, 0);
        g_d3d.occluded = hr == DXGI_STATUS_OCCLUDED;
    }

    app->shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    g_app = nullptr;
    app.reset();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    g_d3d.rtv.reset();
    g_d3d.swapChain.reset();
    g_d3d.context.reset();
    g_d3d.device.reset();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

#else  // GLFW + OpenGL 3

#include <GLFW/glfw3.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace {

GLFWwindow* g_window = nullptr;

Placement queryPlacement() {
    // Keep the last normal (non-maximized) rectangle; fall back to it once the window is gone.
    if (!g_window || glfwGetWindowAttrib(g_window, GLFW_ICONIFIED) == GLFW_TRUE) return g_placement;
    g_placement.maximized = glfwGetWindowAttrib(g_window, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (!g_placement.maximized) {
        Placement p = g_placement;
        glfwGetWindowPos(g_window, &p.x, &p.y);
        glfwGetWindowSize(g_window, &p.w, &p.h);
        if (p.w > 200 && p.h > 150) {
            p.valid = true;
            g_placement = p;
        }
    }
    return g_placement;
}

}  // namespace

int main(int, char**) {
    glfwSetErrorCallback([](int err, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", err, desc); });
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto app = std::make_unique<vast::App>();
    setupContexts(*app);
    g_queryPlacement = &queryPlacement;

    const int w = g_placement.valid ? g_placement.w : 1440;
    const int h = g_placement.valid ? g_placement.h : 900;
    g_window = glfwCreateWindow(w, h, "VastGui - vast.ai desktop", nullptr, nullptr);
    if (!g_window) return 1;
    if (g_placement.valid) glfwSetWindowPos(g_window, g_placement.x, g_placement.y);
    if (g_placement.valid && g_placement.maximized) glfwMaximizeWindow(g_window);
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(g_window, app.get());
    glfwSetWindowContentScaleCallback(g_window, [](GLFWwindow* win, float sx, float) {
        if (auto* a = static_cast<vast::App*>(glfwGetWindowUserPointer(win))) a->setDpiScale(sx);
    });

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(g_window, &sx, &sy);
    app->init(sx);

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(g_window, GLFW_ICONIFIED) != 0) {
            app->client().poll();
            ImGui_ImplGlfw_Sleep(15);
            continue;
        }
        app->preFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app->frame();
        ImGui::Render();
        int dw = 0, dh = 0;
        glfwGetFramebufferSize(g_window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.055f, 0.07f, 0.125f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
    }

    app->shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    app.reset();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}

#endif
