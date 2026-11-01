#pragma once

#include "imgui.h"

namespace vast::ui {

// Rotating arc spinner sized to the current line height when radius <= 0.
void Spinner(const char* id, float radius = 0.0f, float thickness = 0.0f);
// Spinner followed by a dimmed label on the same line.
void SpinnerText(const char* id, const char* text);
// Draws only the animated arc (no layout); used inside buttons and fixed slots.
void DrawSpinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness, ImU32 color);

}  // namespace vast::ui
