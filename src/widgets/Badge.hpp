#pragma once

#include "imgui.h"

namespace vast::ui {

// Rounded "pill" label drawn inline. Returns true when clicked.
bool Badge(const char* text, const ImVec4& color, bool filled = false, const char* tooltip = nullptr);
// Badge exactly one text line high, so it never makes its line taller than plain text.
bool InlineBadge(const char* text, const ImVec4& color, bool filled = false, const char* tooltip = nullptr);
// Size the badge will occupy (for right alignment).
[[nodiscard]] ImVec2 BadgeSize(const char* text);
// Small count bubble drawn at an absolute position (used by the sidebar).
void CountBubble(ImDrawList* dl, const ImVec2& center, int count, const ImVec4& color);

}  // namespace vast::ui
