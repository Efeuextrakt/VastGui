#pragma once

#include "imgui.h"

#include <array>
#include <string>

namespace vast::ui {

struct Palette {
    ImVec4 bg;
    ImVec4 sidebar;
    ImVec4 panel;
    ImVec4 card;
    ImVec4 cardHover;
    ImVec4 border;
    ImVec4 frame;
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 caution;  // yellow
    ImVec4 danger;
    ImVec4 info;
    bool dark = true;
};

// Current palette (valid after applyTheme()).
[[nodiscard]] const Palette& palette();

// Rebuilds ImGui style from scratch: colors + sizes scaled by dpi * uiScale.
void applyTheme(const std::string& theme, const std::array<float, 3>& accent, float dpiScale, float uiScale,
                float fontSize);

[[nodiscard]] ImU32 col(const ImVec4& c, float alphaMul = 1.0f);
[[nodiscard]] ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t);
// 0 = best (green) .. 1 = worst (red), via amber.
[[nodiscard]] ImVec4 rankColor(float t);
[[nodiscard]] ImVec4 statusColor(const std::string& status);
// Offer verification: verified green, deverified orange, unverified yellow.
[[nodiscard]] ImVec4 verificationColor(const std::string& verification);

}  // namespace vast::ui
