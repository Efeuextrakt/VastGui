#include "views/ConsoleView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/Format.hpp"
#include "widgets/Widgets.hpp"

#include <format>

namespace vast {

void ConsoleView::draw(App& app) {
    const ui::Palette& p = ui::palette();
    ui::PageHeader(ICON_FA_LIST, "Activity log", "every API request, its response and errors");

    ImGui::Checkbox("Requests", &showCommands_);
    ImGui::SameLine();
    ImGui::Checkbox("Output", &showOutput_);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &showErrors_);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo_);
    ImGui::SameLine();
    ImGui::Checkbox("Pretty JSON", &prettyJson_);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputTextWithHint("##cfilter", ICON_FA_FILTER " filter", &filter_);
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_TRASH " Clear")) cache_.clear();
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_COPY " Copy all")) {
        std::string all;
        for (const auto& e : cache_) all += fmt::dateTimeFromEpoch(e.time) + " " + e.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_FILE_EXPORT " Save...")) {
        if (const auto path = platform::saveFileDialog("Save console log", "vastgui_console.log")) {
            std::string all;
            for (const auto& e : cache_) all += fmt::dateTimeFromEpoch(e.time) + " [" + logLevelName(e.level) + "] " + e.text + "\n";
            platform::writeTextFileAtomic(*path, all);
        }
    }

    version_ = app.log().copySince(version_, cache_);
    if (cache_.size() > 3000) cache_.erase(cache_.begin(), cache_.end() - 2000);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.sidebar);
    if (ImGui::BeginChild("##console", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
        const std::size_t start = cache_.size() > 600 ? cache_.size() - 600 : 0;
        for (std::size_t idx = start; idx < cache_.size(); ++idx) {
            const LogEntry& e = cache_[idx];
            if (e.level == LogLevel::Command && !showCommands_) continue;
            if (e.level == LogLevel::Output && !showOutput_) continue;
            if (e.level == LogLevel::Error && !showErrors_) continue;
            if ((e.level == LogLevel::Info || e.level == LogLevel::Warning) && !showInfo_) continue;
            if (!filter_.empty() && !fmt::containsCi(e.text, filter_)) continue;
            ImVec4 c = p.text;
            switch (e.level) {
                case LogLevel::Command: c = p.accent; break;
                case LogLevel::Error: c = p.danger; break;
                case LogLevel::Warning: c = p.warning; break;
                case LogLevel::Info: c = p.textDim; break;
                case LogLevel::Output: break;
            }
            std::string text = e.text;
            if (e.level == LogLevel::Output) {
                const auto nl = text.find('\n');
                if (nl != std::string::npos) {
                    const json j = json::parse(text.substr(nl + 1), nullptr, false);
                    if (!j.is_discarded()) text = text.substr(0, nl + 1) + (prettyJson_ ? j.dump(2) : j.dump());
                }
            }
            ImGui::TextDisabled("%s", fmt::dateTimeFromEpoch(e.time).substr(11).c_str());
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::PushID(static_cast<int>(idx));
            ImGui::TextUnformatted(text.c_str());
            if (ImGui::BeginPopupContextItem("##entry")) {
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy")) ImGui::SetClipboardText(text.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::PopStyleColor();
        }
        if (ui::monoFont()) ImGui::PopFont();
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace vast
