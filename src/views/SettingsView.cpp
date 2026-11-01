#include "views/SettingsView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/ApiKey.hpp"
#include "backend/Commands.hpp"
#include "backend/RestBackend.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "views/SearchView.hpp"
#include "widgets/Spinner.hpp"
#include "widgets/Widgets.hpp"

#include <format>

namespace vast {

namespace {

void row(const char* label, float labelW) {
    ImGui::AlignTextToFramePadding();
    ui::TextDim(label);
    ImGui::SameLine(labelW);
}

}  // namespace

void SettingsView::draw(App& app) {
    ui::PageHeader(ICON_FA_GEAR, "Settings", platform::pathToUtf8(app.paths().settingsFile).c_str());
    ImGui::BeginChild("##settings", ImVec2(0, 0));
    if (ImGui::CollapsingHeader(ICON_FA_KEY "  Account / API key", ImGuiTreeNodeFlags_DefaultOpen)) drawAccount(app);
    if (ImGui::CollapsingHeader(ICON_FA_PLUG "  Connection", ImGuiTreeNodeFlags_DefaultOpen)) drawBackend(app);
    if (ImGui::CollapsingHeader(ICON_FA_PALETTE "  Appearance", ImGuiTreeNodeFlags_DefaultOpen)) drawAppearance(app);
    if (ImGui::CollapsingHeader(ICON_FA_FILTER "  Search defaults & defective machines", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawSearch(app);
    }
    if (ImGui::CollapsingHeader(ICON_FA_BAN "  Blacklist")) drawBlacklist(app);
    if (ImGui::CollapsingHeader(ICON_FA_CIRCLE_INFO "  About & files")) drawAbout(app);
    ImGui::EndChild();
}

void SettingsView::drawAccount(App& app) {
    Settings& s = app.settings();
    const float lw = ImGui::GetFontSize() * 11;
    const ui::Palette& p = ui::palette();
    if (!keyInputInit_) {
        keyInput_ = s.apiKey;
        keyInputInit_ = true;
    }
    const ResolvedApiKey& k = app.apiKey();
    row("Active key", lw);
    if (k.key.empty()) {
        ui::TextWithColor(p.danger, "none - set VAST_API_KEY or enter one below");
    } else {
        ImGui::Text("%s  (from %s)", fmt::maskedKey(k.key).c_str(), apiKeySourceLabel(k.source));
    }
    if (k.source == ApiKeySource::Environment) {
        ImGui::SetCursorPosX(lw);
        ui::TextWithColor(p.warning, "VAST_API_KEY overrides the key stored below.");
    }
    row("API key (VastGui.json)", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    ImGui::InputTextWithHint("##apikey", "paste key from console.vast.ai/manage-keys", &keyInput_,
                             showKey_ ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ui::IconButton(showKey_ ? ICON_FA_EYE_SLASH : ICON_FA_EYE, showKey_ ? "Hide" : "Show")) showKey_ = !showKey_;
    ImGui::SetCursorPosX(lw);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save key")) {
        s.apiKey = fmt::trim(keyInput_);
        s.apiKeySealed.clear();
        app.markSettingsDirty();
        app.reconfigureBackend();
        app.refreshUser();
        app.refreshInstances(true);
        app.toasts().success(s.apiKey.empty() ? "Stored key removed" : "API key saved");
    }
    ImGui::SameLine();
    // Testing a key that differs from the stored one has to apply it, so say so instead of saving silently.
    const bool unsavedKey = fmt::trim(keyInput_) != s.apiKey;
    if (ui::BusyButton(ICON_FA_CHECK, unsavedKey ? "Save & test" : "Test key", testing_, false) && !testing_) {
        if (unsavedKey) {
            s.apiKey = fmt::trim(keyInput_);
            s.apiKeySealed.clear();
            app.markSettingsDirty();
            app.reconfigureBackend();
        }
        testing_ = true;
        testResult_.clear();
        app.client().submit(cmd::showUser(), [this, &app](const ApiResult& r) {
            testing_ = false;
            testOk_ = r.ok && r.data.is_object();
            if (testOk_) {
                testResult_ = std::format("OK - {} ({}), credit {}", jsonutil::str(r.data, "username"),
                                          jsonutil::str(r.data, "email"),
                                          fmt::money(jsonutil::num(r.data, "credit"), app.settings().currency, 2));
                app.refreshUser();
                app.refreshInstances(true);
            } else {
                testResult_ = "Failed: " + r.error;
            }
        });
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Get a key")) platform::openUrl("https://cloud.vast.ai/manage-keys/");
    // Result line is always present so the section height never changes.
    ImGui::SetCursorPosX(lw);
    if (testing_) {
        ui::TextDim("checking the key with the vast.ai API...");
    } else if (testResult_.empty()) {
        ui::TextDim("Press 'Test key' to verify the key.");
    } else {
        ui::TextWithColor(testOk_ ? p.success : p.danger, testResult_.c_str());
    }
    ImGui::SetCursorPosX(lw);
    ui::TextDim("Lookup order: VAST_API_KEY env var, then VastGui.json. The key is never logged.");
    ImGui::SetCursorPosX(lw);
    ui::TextDim(std::format("VastGui.json stores the key encrypted; the encryption key is {}.",
                            platform::pathToUtf8(app.paths().keyFile))
                    .c_str());
    ui::HelpMarker("Copying VastGui.json to another PC does not carry the API key along - enter it again there.");
    ImGui::Spacing();
}

void SettingsView::drawBackend(App& app) {
    Settings& s = app.settings();
    const float lw = ImGui::GetFontSize() * 11;
    row("Server URL", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    if (ImGui::InputText("##server", &s.serverUrl, ImGuiInputTextFlags_EnterReturnsTrue)) {
        app.markSettingsDirty();
        app.reconfigureBackend();
    }
    ui::HelpMarker("vast.ai REST API base URL (press Enter to apply). Default: https://console.vast.ai");
    if (std::string why; !RestBackend::serverUrlAllowed(s.serverUrl, why)) {
        ImGui::SetCursorPosX(lw);
        ui::TextWithColor(ui::palette().danger, why.c_str());
    }
    row("Auto-refresh", lw);
    if (ImGui::Checkbox("##autorefresh", &s.autoRefresh)) app.markSettingsDirty();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderInt("##interval", &s.refreshIntervalSec, 5, 600, "every %d s")) app.markSettingsDirty();
    ui::HelpMarker("Instances, keys, volumes... are cached and refreshed in the background at this interval.");
    row("Search auto-refresh", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderInt("##searchref", &s.searchRefreshSec, 0, 600, s.searchRefreshSec == 0 ? "manual" : "every %d s")) {
        app.markSettingsDirty();
    }
    row("Offers per request", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderInt("##searchlimit", &s.searchLimit, kMinSearchLimit, kMaxSearchLimit, "limit %d",
                         ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        app.markSettingsDirty();
    }
    ui::HelpMarker("The limit of each offer search. vast.ai returns a random sample of the matching offers; larger "
                   "limits are more complete (measured: 128 finds ~20% of the true best 10 offers, 1000 finds ~80%).\n"
                   "Without an API key vast.ai caps every request at 64 offers.");
    row("Requests per search", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::InputInt("##searchreq", &s.searchRequests, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.searchRequests = std::max(1, s.searchRequests);
        app.markSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::TextDim(std::format("recommended 3-10, default {}", kDefaultSearchRequests).c_str());
    ui::HelpMarker("Identical requests sent in parallel per search and merged (one offer per machine + GPU count).\n"
                   "Measured with limit 1000: 1 request finds ~80% of the true best 10 offers and ~74% of the best 200; "
                   "3 merged requests find 100% and ~87%, 4 find ~92%. More requests add less and less.\n"
                   "Each request downloads ~2.7 MB at limit 1000.");
    row("Log file", lw);
    if (ImGui::Checkbox("Write VastGui.log", &s.logToFile)) {
        app.markSettingsDirty();
        app.log().setFile(app.paths().logFile, s.logToFile);
    }
    ImGui::Spacing();
}

void SettingsView::drawAppearance(App& app) {
    Settings& s = app.settings();
    const float lw = ImGui::GetFontSize() * 11;
    bool changed = false;
    row("Theme", lw);
    int theme = s.theme == "light" ? 1 : 0;
    changed |= ImGui::RadioButton("Dark", &theme, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Light", &theme, 1);
    s.theme = theme == 1 ? "light" : "dark";
    row("Accent colour", lw);
    changed |= ImGui::ColorEdit3("##accent", s.accent.data(), ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (ui::SecondaryButton("Reset")) {
        s.accent = kDefaultAccent;
        changed = true;
    }
    // Font size and UI scale are staged and only take effect on "Apply appearance".
    if (!pendingInit_) {
        pendingFontSize_ = s.fontSize;
        pendingUiScale_ = s.uiScale;
        pendingInit_ = true;
    }
    row("Font size", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::SliderFloat("##fontsize", &pendingFontSize_, 11.0f, 24.0f, "%.0f px");
    row("UI scale", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::SliderFloat("##uiscale", &pendingUiScale_, 0.75f, 2.0f, "%.2fx");
    ImGui::SameLine();
    ui::TextDim(std::format("monitor DPI scale {:.2f}", app.dpiScale()).c_str());
    if (changed) {
        app.requestAppearanceUpdate();
        app.markSettingsDirty();
    }
    const bool pending = pendingFontSize_ != s.fontSize || pendingUiScale_ != s.uiScale;
    row(" ", lw);
    ImGui::BeginDisabled(!pending);
    if (ImGui::Button(ICON_FA_CHECK " Apply appearance")) {
        s.fontSize = pendingFontSize_;
        s.uiScale = pendingUiScale_;
        app.requestAppearanceUpdate();
        app.markSettingsDirty();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!pending);
    if (ui::SecondaryButton("Revert")) {
        pendingFontSize_ = s.fontSize;
        pendingUiScale_ = s.uiScale;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::TextDim(pending ? "Font size / UI scale changed - press Apply" : "Theme and accent apply immediately");

    row("Currency", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4);
    if (ImGui::InputText("##cursym", &s.currency.symbol)) app.markSettingsDirty();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    if (ImGui::SliderInt("##curdec", &s.currency.decimals, 0, 6, "%d decimals")) app.markSettingsDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox("Symbol after", &s.currency.symbolAfter)) app.markSettingsDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox("Thousands separator", &s.currency.thousandsSeparator)) app.markSettingsDirty();
    ImGui::SameLine();
    ui::TextDim(("e.g. " + fmt::money(1234.5678, s.currency)).c_str());
    ImGui::Spacing();
}

void SettingsView::drawSearch(App& app) {
    Settings& s = app.settings();
    const float lw = ImGui::GetFontSize() * 15;
    row("Default sort", lw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    const auto cur = sortKeyFromId(s.defaultSort).value_or(SortKey::Score);
    if (ImGui::BeginCombo("##defsort", sortOption(cur).label)) {
        for (const auto& o : sortOptions()) {
            if (ImGui::Selectable(o.label, o.key == cur)) {
                s.defaultSort = o.id;
                s.defaultSortDesc = o.defaultDesc;
                app.markSettingsDirty();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Descending", &s.defaultSortDesc)) app.markSettingsDirty();

    DefectThresholds& d = s.defects;
    bool changed = false;
    ImGui::SeparatorText("Defective machine detection");
    changed |= ImGui::Checkbox("Flag low reliability below", &d.checkReliability);
    ImGui::SameLine();
    float rel = static_cast<float>(d.minReliability * 100.0);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::SliderFloat("##minrel", &rel, 50.0f, 100.0f, "%.1f %%")) {
        d.minReliability = rel / 100.0;
        changed = true;
    }
    changed |= ImGui::Checkbox("Flag missing/zero inet_up, inet_down, total_flops, dlperf, disk_bw, cuda_max_good",
                               &d.checkMissingValues);
    changed |= ImGui::Checkbox("Flag DLPerf/TFLOPS more than", &d.checkLowPerformance);
    ImGui::SameLine();
    float below = static_cast<float>(d.maxBelowMedian * 100.0);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::SliderFloat("##below", &below, 5.0f, 95.0f, "%.0f %%")) {
        d.maxBelowMedian = below / 100.0;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("below the GPU-model median (min");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    changed |= ImGui::InputInt("##minsamples", &d.minSamplesForMedian, 0);
    d.minSamplesForMedian = std::clamp(d.minSamplesForMedian, 2, 1000);
    ImGui::SameLine();
    ImGui::TextUnformatted("offers)");
    changed |= ImGui::Checkbox("Flag blacklisted machines and hosts", &d.checkBlacklist);
    if (changed) {
        app.markSettingsDirty();
        app.searchView().onBlacklistChanged(app);
    }
    if (ui::SecondaryButton("Reset thresholds")) {
        d = DefectThresholds{};
        app.markSettingsDirty();
    }
    ImGui::Spacing();
}

void SettingsView::drawBlacklist(App& app) {
    Settings& s = app.settings();
    Blacklist& b = s.blacklist;
    bool changed = false;
    auto list = [&](const char* title, std::set<std::int64_t>& ids, std::int64_t& input, const char* id) {
        ImGui::PushID(id);
        ImGui::SeparatorText(title);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputScalar("##add", ImGuiDataType_S64, &input);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " Add") && input > 0) {
            ids.insert(input);
            input = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (!ids.empty() && ui::SecondaryButton("Clear all")) {
            ids.clear();
            changed = true;
        }
        std::optional<std::int64_t> remove;
        int n = 0;
        for (auto v : ids) {
            ImGui::PushID(static_cast<int>(v));
            if (n++ % 6 != 0) ImGui::SameLine();
            if (ui::SecondaryButton((std::to_string(v) + "  " ICON_FA_XMARK).c_str())) remove = v;
            ImGui::PopID();
        }
        if (remove) {
            ids.erase(*remove);
            changed = true;
        }
        if (ids.empty()) ui::TextDim("(empty)");
        ImGui::PopID();
    };
    ui::TextDim("Hidden machines/hosts are treated as defective in search results. Right-click an offer to add one.");
    list("Machines", b.machines, addMachine_, "m");
    list("Hosts", b.hosts, addHost_, "h");
    if (changed) {
        app.markSettingsDirty();
        app.searchView().onBlacklistChanged(app);
    }
}

void SettingsView::drawAbout(App& app) {
    const ConfigPaths& p = app.paths();
    ImGui::Text("VastGui %s  -  Dear ImGui %s, ImPlot %s", kAppVersion, IMGUI_VERSION, IMPLOT_VERSION);
    ImGui::Text("Config directory: %s%s", platform::pathToUtf8(p.dir).c_str(), p.portable ? " (portable mode)" : "");
    ImGui::Text("Settings: %s", platform::pathToUtf8(p.settingsFile).c_str());
    ImGui::Text("Layout:   %s", platform::pathToUtf8(p.iniFile).c_str());
    ImGui::Text("Log file: %s", platform::pathToUtf8(p.logFile).c_str());
    ImGui::Text("Key file: %s", platform::pathToUtf8(p.keyFile).c_str());
    if (ui::SecondaryButton(ICON_FA_FOLDER_OPEN " Open config folder")) platform::openPath(p.dir);
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ROTATE " Reset window layout")) {
        ImGui::ClearIniSettings();
        app.toasts().info("Layout reset", "Restart VastGui to rebuild the default docking layout.");
    }
    ui::TextDim("Portable mode: place an (even empty) VastGui.json next to VastGui.exe.");
}

}  // namespace vast
