#include "Theme.hpp"

#include "implot.h"

#include <algorithm>

namespace vast::ui {

namespace {

Palette g_palette;

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, a);
}

ImVec4 scale(const ImVec4& c, float f) {
    return ImVec4(std::min(c.x * f, 1.0f), std::min(c.y * f, 1.0f), std::min(c.z * f, 1.0f), c.w);
}

Palette darkPalette(const ImVec4& accent) {
    Palette p;
    p.bg = rgb(14, 18, 32);
    p.sidebar = rgb(9, 12, 23);
    p.panel = rgb(19, 24, 41);
    p.card = rgb(24, 31, 52);
    p.cardHover = rgb(30, 40, 67);
    p.border = rgb(38, 49, 77);
    p.frame = rgb(27, 35, 56);
    p.text = rgb(232, 236, 245);
    p.textDim = rgb(140, 150, 174);
    p.accent = accent;
    p.accentHover = scale(accent, 1.18f);
    p.success = rgb(34, 197, 94);
    p.warning = rgb(245, 158, 11);
    p.caution = rgb(250, 204, 21);
    p.danger = rgb(239, 68, 68);
    p.info = rgb(56, 189, 248);
    p.dark = true;
    return p;
}

Palette lightPalette(const ImVec4& accent) {
    Palette p;
    p.bg = rgb(244, 246, 251);
    p.sidebar = rgb(255, 255, 255);
    p.panel = rgb(255, 255, 255);
    p.card = rgb(255, 255, 255);
    p.cardHover = rgb(238, 243, 252);
    p.border = rgb(214, 220, 233);
    p.frame = rgb(236, 240, 247);
    p.text = rgb(22, 28, 45);
    p.textDim = rgb(98, 108, 132);
    p.accent = accent;
    p.accentHover = scale(accent, 0.88f);
    p.success = rgb(22, 163, 74);
    p.warning = rgb(217, 119, 6);
    p.caution = rgb(202, 138, 4);
    p.danger = rgb(220, 38, 38);
    p.info = rgb(2, 132, 199);
    p.dark = false;
    return p;
}

}  // namespace

const Palette& palette() { return g_palette; }

ImU32 col(const ImVec4& c, float alphaMul) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul)); }

ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 rankColor(float t) {
    const Palette& p = g_palette;
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.5f) return lerpColor(p.success, p.warning, t * 2.0f);
    return lerpColor(p.warning, p.danger, (t - 0.5f) * 2.0f);
}

ImVec4 statusColor(const std::string& s) {
    const Palette& p = g_palette;
    if (s == "running") return p.success;
    if (s == "loading" || s == "created" || s == "rebooting" || s == "scheduling" || s == "starting" ||
        s == "creating" || s == "initializing" || s == "stopping" || s == "provisioning") {
        return p.info;
    }
    if (s == "stopped" || s == "frozen" || s == "inactive" || s == "unavailable") return p.warning;
    if (s == "exited" || s == "offline" || s == "unknown" || s == "error" || s == "outbid" || s == "expired" ||
        s == "insufficient credits") {
        return p.danger;
    }
    return p.textDim;
}

ImVec4 verificationColor(const std::string& v) {
    const Palette& p = g_palette;
    if (v == "verified") return p.success;
    if (v == "deverified") return p.warning;
    return p.caution;
}

void applyTheme(const std::string& theme, const std::array<float, 3>& accentRgb, float dpiScale, float uiScale,
                float fontSize) {
    const ImVec4 accent(accentRgb[0], accentRgb[1], accentRgb[2], 1.0f);
    g_palette = theme == "light" ? lightPalette(accent) : darkPalette(accent);
    const Palette& p = g_palette;

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (p.dark) {
        ImGui::StyleColorsDark(&style);
    } else {
        ImGui::StyleColorsLight(&style);
    }

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    // List rows in dropdowns, context menus and the GPU picker share the button/field rounding.
    style.SelectableRounding = style.FrameRounding;
    style.MenuItemRounding = style.FrameRounding;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(9, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ItemInnerSpacing = ImVec2(6, 5);
    style.CellPadding = ImVec2(7, 4);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 11.0f;
    style.IndentSpacing = 18.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = p.dark ? ImVec4(p.panel.x, p.panel.y, p.panel.z, 0.98f) : p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.frame;
    c[ImGuiCol_FrameBgHovered] = lerpColor(p.frame, p.accent, 0.18f);
    c[ImGuiCol_FrameBgActive] = lerpColor(p.frame, p.accent, 0.30f);
    c[ImGuiCol_TitleBg] = p.sidebar;
    c[ImGuiCol_TitleBgActive] = p.sidebar;
    c[ImGuiCol_TitleBgCollapsed] = p.sidebar;
    c[ImGuiCol_MenuBarBg] = p.panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = lerpColor(p.border, p.accent, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentHover;
    c[ImGuiCol_Button] = p.accent;
    c[ImGuiCol_ButtonHovered] = p.accentHover;
    c[ImGuiCol_ButtonActive] = scale(p.accent, 0.85f);
    c[ImGuiCol_Header] = lerpColor(p.card, p.accent, 0.25f);
    c[ImGuiCol_HeaderHovered] = lerpColor(p.card, p.accent, 0.40f);
    c[ImGuiCol_HeaderActive] = lerpColor(p.card, p.accent, 0.55f);
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accentHover;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = p.accent;
    c[ImGuiCol_ResizeGripActive] = p.accentHover;
    c[ImGuiCol_Tab] = p.panel;
    c[ImGuiCol_TabHovered] = lerpColor(p.panel, p.accent, 0.45f);
    c[ImGuiCol_TabSelected] = lerpColor(p.panel, p.accent, 0.30f);
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.panel;
    c[ImGuiCol_TabDimmedSelected] = lerpColor(p.panel, p.accent, 0.15f);
    c[ImGuiCol_DockingPreview] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = p.bg;
    c[ImGuiCol_TableHeaderBg] = p.panel;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = lerpColor(p.border, p.bg, 0.4f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = p.dark ? ImVec4(1, 1, 1, 0.025f) : ImVec4(0, 0, 0, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.35f);
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, p.dark ? 0.55f : 0.25f);
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotLines] = p.accent;

    const float s = dpiScale * uiScale;
    style.ScaleAllSizes(s);
    style.FontScaleDpi = dpiScale;
    style.FontScaleMain = uiScale;
    style.FontSizeBase = fontSize;
    style._NextFrameFontSizeBase = fontSize;

    if (ImPlot::GetCurrentContext()) {
        ImPlotStyle& ps = ImPlot::GetStyle();
        if (p.dark) {
            ImPlot::StyleColorsDark(&ps);
        } else {
            ImPlot::StyleColorsLight(&ps);
        }
        ps.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
        ps.Colors[ImPlotCol_PlotBg] = p.dark ? ImVec4(1, 1, 1, 0.02f) : ImVec4(0, 0, 0, 0.02f);
        ps.Colors[ImPlotCol_PlotBorder] = p.border;
    }
}

}  // namespace vast::ui
