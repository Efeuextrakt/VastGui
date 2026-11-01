#include "widgets/RangeSlider.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <cmath>

namespace vast::ui {

namespace {

double toT(double v, double lo, double hi, bool logarithmic) {
    if (hi <= lo) return 0.0;
    v = std::clamp(v, lo, hi);
    if (logarithmic) return std::log1p(v - lo) / std::log1p(hi - lo);
    return (v - lo) / (hi - lo);
}

double fromT(double t, double lo, double hi, bool logarithmic) {
    t = std::clamp(t, 0.0, 1.0);
    if (logarithmic) return lo + std::expm1(t * std::log1p(hi - lo));
    return lo + t * (hi - lo);
}

double roundTo(double v, int decimals) {
    const double m = std::pow(10.0, decimals);
    return std::round(v * m) / m;
}

}  // namespace

bool RangeSlider(const char* id, double* vmin, double* vmax, double lo, double hi, int decimals, bool logarithmic,
                 float width) {
    ImGui::PushID(id);
    const Palette& p = palette();
    const float h = ImGui::GetFrameHeight() * 0.8f;
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float grabR = h * 0.36f;
    const float x0 = pos.x + grabR;
    const float x1 = pos.x + width - grabR;
    const float cy = pos.y + h * 0.5f;

    ImGui::InvisibleButton("##range", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID handleKey = ImGui::GetID("##handle");

    bool changed = false;
    if (ImGui::IsItemActivated()) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float pmin = x0 + static_cast<float>(toT(*vmin, lo, hi, logarithmic)) * (x1 - x0);
        const float pmax = x0 + static_cast<float>(toT(*vmax, lo, hi, logarithmic)) * (x1 - x0);
        int handle = std::abs(mx - pmin) <= std::abs(mx - pmax) ? 0 : 1;
        if (std::abs(pmin - pmax) < 1.0f) handle = mx < pmin ? 0 : 1;
        storage->SetInt(handleKey, handle);
    }
    if (active) {
        const int handle = storage->GetInt(handleKey, 0);
        const double t = (ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0);
        double v = roundTo(fromT(t, lo, hi, logarithmic), decimals);
        if (t <= 0.0) v = lo;
        if (t >= 1.0) v = hi;
        if (handle == 0) {
            v = std::min(v, *vmax);
            if (v != *vmin) {
                *vmin = v;
                changed = true;
            }
        } else {
            v = std::max(v, *vmin);
            if (v != *vmax) {
                *vmax = v;
                changed = true;
            }
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float trackH = std::max(3.0f, h * 0.16f);
    dl->AddRectFilled(ImVec2(x0, cy - trackH * 0.5f), ImVec2(x1, cy + trackH * 0.5f), col(p.border), trackH);
    const float pmin = x0 + static_cast<float>(toT(*vmin, lo, hi, logarithmic)) * (x1 - x0);
    const float pmax = x0 + static_cast<float>(toT(*vmax, lo, hi, logarithmic)) * (x1 - x0);
    const bool isFull = *vmin <= lo && *vmax >= hi;
    const ImVec4 fill = isFull ? lerpColor(p.border, p.accent, 0.35f) : p.accent;
    dl->AddRectFilled(ImVec2(pmin, cy - trackH * 0.5f), ImVec2(pmax, cy + trackH * 0.5f), col(fill), trackH);
    const ImU32 grabCol = col((hovered || active) ? p.accentHover : p.accent);
    for (float gx : {pmin, pmax}) {
        dl->AddCircleFilled(ImVec2(gx, cy), grabR, grabCol, 20);
        dl->AddCircleFilled(ImVec2(gx, cy), grabR * 0.45f, col(p.panel), 16);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##rangeedit");
    if (hovered && !active) ImGui::SetTooltip("Drag handles, right-click for exact values");
    if (ImGui::BeginPopup("##rangeedit")) {
        const char* fmtStr = decimals <= 0 ? "%.0f" : decimals == 1 ? "%.1f" : decimals == 2 ? "%.2f" : decimals == 3 ? "%.3f" : "%.4f";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
        if (ImGui::InputDouble("Min", vmin, 0, 0, fmtStr)) {
            *vmin = std::clamp(*vmin, lo, *vmax);
            changed = true;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
        if (ImGui::InputDouble("Max", vmax, 0, 0, fmtStr)) {
            *vmax = std::clamp(*vmax, *vmin, hi);
            changed = true;
        }
        if (ImGui::Button("Reset")) {
            *vmin = lo;
            *vmax = hi;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace vast::ui
