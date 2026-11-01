#include "widgets/Toast.hpp"

#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace vast::ui {

void Toasts::push(ToastKind kind, std::string title, std::string body, float seconds) {
    // Collapse identical consecutive errors (e.g. repeated auto-refresh failures).
    for (auto& it : items_) {
        if (it.kind == kind && it.title == title && it.body == body) {
            it.created = ImGui::GetTime();
            return;
        }
    }
    if (body.size() > 600) body = body.substr(0, 600) + "...";
    items_.push_back(Item{kind, std::move(title), std::move(body), ImGui::GetTime(), seconds, nextId_++});
    if (items_.size() > 6) items_.erase(items_.begin());
}

void Toasts::draw() {
    const double now = ImGui::GetTime();
    std::erase_if(items_, [&](const Item& t) { return now - t.created > t.seconds; });
    if (items_.empty()) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = ImGui::GetFontSize() * 1.0f;
    const float width = ImGui::GetFontSize() * 24.0f;
    float y = vp->WorkPos.y + vp->WorkSize.y - pad * 2.2f;
    const Palette& p = palette();

    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        Item& t = *it;
        const char* icon = ICON_FA_CIRCLE_INFO;
        ImVec4 color = p.info;
        switch (t.kind) {
            case ToastKind::Success: icon = ICON_FA_CIRCLE_CHECK; color = p.success; break;
            case ToastKind::Warning: icon = ICON_FA_TRIANGLE_EXCLAMATION; color = p.warning; break;
            case ToastKind::Error: icon = ICON_FA_CIRCLE_XMARK; color = p.danger; break;
            case ToastKind::Info: break;
        }
        const float age = static_cast<float>(now - t.created);
        const float alpha = std::clamp(std::min(age * 6.0f, (t.seconds - age) * 2.0f), 0.0f, 1.0f);

        char name[32];
        std::snprintf(name, sizeof(name), "##toast%d", t.id);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - pad, y), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.97f * alpha);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, p.panel);
        ImGui::PushStyleColor(ImGuiCol_Border, color);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
        if (ImGui::Begin(name, nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - pad * 3.0f);
            ImGui::TextUnformatted(t.title.c_str());
            if (!t.body.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
                ImGui::TextUnformatted(t.body.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            if (ImGui::IsWindowHovered()) {
                t.created = std::max(t.created, now - static_cast<double>(t.seconds) + 1.5);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) t.created = now - t.seconds;
            }
            y -= ImGui::GetWindowHeight() + pad * 0.5f;
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}

}  // namespace vast::ui
