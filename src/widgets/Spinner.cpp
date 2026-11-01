#include "widgets/Spinner.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <cmath>

namespace vast::ui {

void DrawSpinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness, ImU32 color) {
    const float t = static_cast<float>(ImGui::GetTime());
    const float start = t * 6.0f;
    const float sweep = 3.14159f * (0.9f + 0.5f * std::sin(t * 2.2f));
    dl->PathClear();
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = start + sweep * static_cast<float>(i) / kSegments;
        dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
    }
    dl->PathStroke(color, 0, thickness);
}

void Spinner(const char* id, float radius, float thickness) {
    ImGui::PushID(id);
    const float h = ImGui::GetTextLineHeight();
    if (radius <= 0) radius = h * 0.42f;
    if (thickness <= 0) thickness = std::max(2.0f, radius * 0.28f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(radius * 2.0f + thickness, std::max(h, radius * 2.0f + thickness));
    ImGui::Dummy(size);
    DrawSpinner(ImGui::GetWindowDrawList(), ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), radius, thickness,
                col(palette().accent));
    ImGui::PopID();
}

void SpinnerText(const char* id, const char* text) {
    Spinner(id);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text);
}

}  // namespace vast::ui
