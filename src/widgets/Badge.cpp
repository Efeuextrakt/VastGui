#include "widgets/Badge.hpp"

#include "Theme.hpp"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>

namespace vast::ui {

namespace {

ImVec2 badgeSize(const char* text, float padYFactor) {
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, true);
    const float padX = ImGui::GetFontSize() * 0.55f;
    const float padY = ImGui::GetFontSize() * padYFactor;
    return ImVec2(ts.x + padX * 2.0f, ts.y + padY * 2.0f);
}

bool badgeImpl(const char* text, const ImVec4& color, bool filled, const char* tooltip, float padYFactor);

}  // namespace

ImVec2 BadgeSize(const char* text) { return badgeSize(text, 0.12f); }

bool Badge(const char* text, const ImVec4& color, bool filled, const char* tooltip) {
    return badgeImpl(text, color, filled, tooltip, 0.12f);
}

bool InlineBadge(const char* text, const ImVec4& color, bool filled, const char* tooltip) {
    return badgeImpl(text, color, filled, tooltip, 0.0f);
}

namespace {

bool badgeImpl(const char* text, const ImVec4& color, bool filled, const char* tooltip, float padYFactor) {
    const ImVec2 size = badgeSize(text, padYFactor);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    // Line up the badge text with neighbouring text that follows the frame-padding baseline (tables, AlignTextToFramePadding).
    // Only the badge itself is offset; the line start stays put so following items keep the same baseline.
    const float padY = (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    const float offY = std::max(0.0f, ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset - padY);
    const ImVec2 pos(start.x, start.y + offY);
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const ImRect bb(pos, max);
    ImGui::PushID(text);
    const ImGuiID id = ImGui::GetID("##badge");
    ImGui::PopID();
    ImGui::ItemSize(ImVec2(size.x, offY + size.y));
    bool hovered = false, held = false, clicked = false;
    if (ImGui::ItemAdd(bb, id)) clicked = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = size.y * 0.5f;
    if (filled) {
        dl->AddRectFilled(pos, max, col(color, hovered ? 1.0f : 0.9f), rounding);
    } else {
        dl->AddRectFilled(pos, max, col(color, hovered ? 0.28f : 0.18f), rounding);
        dl->AddRect(pos, max, col(color, 0.55f), rounding);
    }
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, true);
    const ImVec4 textCol = filled ? ImVec4(1, 1, 1, 1) : color;
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), col(textCol), text,
                ImGui::FindRenderedTextEnd(text));
    if (tooltip && hovered) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

}  // namespace

void CountBubble(ImDrawList* dl, const ImVec2& center, int count, const ImVec4& color) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", count);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const float h = ImGui::GetFontSize() * 1.15f;
    const float w = std::max(h, ts.x + h * 0.6f);
    const ImVec2 min(center.x - w * 0.5f, center.y - h * 0.5f);
    const ImVec2 max(center.x + w * 0.5f, center.y + h * 0.5f);
    dl->AddRectFilled(min, max, col(color), h * 0.5f);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), IM_COL32_WHITE, buf);
}

}  // namespace vast::ui
