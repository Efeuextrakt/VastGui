#include "App.hpp"

#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "embedded/Fonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "logic/Format.hpp"
#include "views/BillingView.hpp"
#include "views/ConsoleView.hpp"
#include "views/HostingView.hpp"
#include "views/InstancesView.hpp"
#include "views/KeysView.hpp"
#include "views/SearchView.hpp"
#include "views/ServerlessView.hpp"
#include "views/SettingsView.hpp"
#include "views/StorageView.hpp"
#include "views/TeamView.hpp"
#include "views/TemplatesView.hpp"
#include "widgets/Badge.hpp"
#include "widgets/Spinner.hpp"
#include "widgets/Widgets.hpp"

#include <algorithm>
#include <format>

namespace vast {

namespace {

struct NavItem {
    Page page;
    const char* icon;
    const char* label;
    bool separatorBefore;
};

constexpr NavItem kNav[] = {
    {Page::Search, ICON_FA_MAGNIFYING_GLASS, "Search", false},
    {Page::Templates, ICON_FA_LAYER_GROUP, "Templates", false},
    {Page::Instances, ICON_FA_SERVER, "Instances", false},
    {Page::Storage, ICON_FA_HARD_DRIVE, "Storage", false},
    {Page::Serverless, ICON_FA_BOLT, "Serverless", false},
    {Page::Billing, ICON_FA_CREDIT_CARD, "Billing", true},
    {Page::Earnings, ICON_FA_SACK_DOLLAR, "Earnings", false},
    {Page::Team, ICON_FA_USERS, "Members / Team", false},
    {Page::AuditLogs, ICON_FA_CLIPBOARD_LIST, "Audit Logs", false},
    {Page::Keys, ICON_FA_KEY, "Keys", false},
    {Page::Settings, ICON_FA_GEAR, "Settings", true},
    {Page::Hosting, ICON_FA_MICROCHIP, "Hosting (Machines)", false},
    {Page::Console, ICON_FA_LIST, "Activity log", false},
};

const NavItem& navItem(Page p) {
    for (const auto& n : kNav) {
        if (n.page == p) return n;
    }
    return kNav[0];
}

}  // namespace

App::App() : paths_(resolveConfigPaths()) {
    iniPathUtf8_ = platform::pathToUtf8(paths_.iniFile);
    std::string err;
    settings_ = loadSettings(paths_, err);
    log_.setFile(paths_.logFile, settings_.logToFile);
    if (!err.empty()) log_.warn(err);
    client_ = std::make_unique<VastClient>(log_, 12);
    client_->setCompletionHook([this](const ApiCall& c, const ApiResult& r) {
        if (!r.ok && !r.cancelled) toasts_.error(c.title + " failed", r.error);
    });

    search_ = std::make_unique<SearchView>();
    templates_ = std::make_unique<TemplatesView>();
    instancesView_ = std::make_unique<InstancesView>();
    storage_ = std::make_unique<StorageView>();
    serverless_ = std::make_unique<ServerlessView>();
    billing_ = std::make_unique<BillingView>();
    earnings_ = std::make_unique<EarningsView>();
    team_ = std::make_unique<TeamView>();
    audit_ = std::make_unique<AuditLogsView>();
    keys_ = std::make_unique<KeysView>();
    hosting_ = std::make_unique<HostingView>();
    console_ = std::make_unique<ConsoleView>();
    settingsView_ = std::make_unique<SettingsView>();
}

App::~App() = default;

void App::init(float dpiScale) {
    dpiScale_ = dpiScale > 0 ? dpiScale : 1.0f;
    loadFonts();
    applyAppearance();
    log_.info(std::format("VastGui {} - config dir: {}{}", kAppVersion, platform::pathToUtf8(paths_.dir),
                          paths_.portable ? " (portable)" : ""));
    reconfigureBackend();
    search_->init(*this);
    if (!apiKey_.key.empty()) {
        refreshUser();
        refreshInstances(true);
    }
}

void App::loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar kIconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

    ImFontConfig cfg;
    std::snprintf(cfg.Name, sizeof(cfg.Name), "Roboto");
    io.Fonts->AddFontFromMemoryCompressedTTF(Roboto_compressed_data, static_cast<int>(Roboto_compressed_size), 16.0f, &cfg);
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.PixelSnapH = true;
    icons.GlyphMinAdvanceX = 16.0f;
    icons.GlyphRanges = kIconRanges;
    std::snprintf(icons.Name, sizeof(icons.Name), "FontAwesome");
    io.Fonts->AddFontFromMemoryCompressedTTF(FontAwesome_compressed_data, static_cast<int>(FontAwesome_compressed_size),
                                             13.5f, &icons);

    ImFontConfig mcfg;
    std::snprintf(mcfg.Name, sizeof(mcfg.Name), "Cousine");
    ImFont* mono = io.Fonts->AddFontFromMemoryCompressedTTF(Mono_compressed_data, static_cast<int>(Mono_compressed_size),
                                                            15.0f, &mcfg);
    io.Fonts->AddFontFromMemoryCompressedTTF(FontAwesome_compressed_data, static_cast<int>(FontAwesome_compressed_size),
                                             12.5f, &icons);
    ui::setMonoFont(mono);
}

void App::applyAppearance() {
    ui::applyTheme(settings_.theme, settings_.accent, dpiScale_, settings_.uiScale, settings_.fontSize);
}

void App::setDpiScale(float scale) {
    if (scale <= 0 || std::abs(scale - dpiScale_) < 0.001f) return;
    dpiScale_ = scale;
    appearanceDirty_ = true;
}

void App::reconfigureBackend() {
    apiKey_ = resolveApiKey(settings_.apiKey);
    BackendConfig cfg;
    cfg.apiKey = apiKey_.key;
    cfg.serverUrl = settings_.serverUrl;
    client_->configure(cfg);
    log_.setFile(paths_.logFile, settings_.logToFile);
    if (apiKey_.key.empty()) {
        log_.warn("No API key found. Set VAST_API_KEY, or enter one in Settings.");
    } else {
        log_.info(std::format("API key {} loaded from {}", fmt::maskedKey(apiKey_.key), apiKeySourceLabel(apiKey_.source)));
    }
}

void App::markSettingsDirty() {
    if (!settingsDirty_) dirtySince_ = ImGui::GetTime();
    settingsDirty_ = true;
}

void App::saveIfDirty(bool force) {
    if (!settingsDirty_) return;
    if (!force && ImGui::GetTime() - dirtySince_ < 1.0) return;
    std::string err;
    if (!saveSettings(paths_, settings_, &err)) log_.error("Settings not saved: " + err);
    settingsDirty_ = false;
}

void App::shutdown() {
    saveIfDirty(true);
    if (ImGui::GetCurrentContext()) {
        ImGui::SaveIniSettingsToDisk(iniPathUtf8_.c_str());
        // io.IniFilename points into this App, which main() destroys before ImGui::DestroyContext(); without this
        // ImGui would save again from a dangling pointer during DestroyContext (use-after-free).
        ImGui::GetIO().IniFilename = nullptr;
    }
}

void App::navigate(Page p) { page_ = p; }

std::uint64_t App::run(ApiCall call, std::function<void(const ApiResult&)> onSuccess, std::string successMsg) {
    return client_->submit(std::move(call), [this, onSuccess = std::move(onSuccess), msg = std::move(successMsg)](
                                                const ApiResult& r) {
        if (!r.ok) return;
        if (!msg.empty()) toasts_.success(msg);
        if (onSuccess) onSuccess(r);
    });
}

void App::refreshInstances(bool quiet) {
    if (instancesLoading_) return;
    instancesLoading_ = true;
    instancesAttemptedAt_ = ImGui::GetTime();
    ApiCall c = cmd::showInstances();
    c.quiet = quiet;
    client_->submit(std::move(c), [this](const ApiResult& r) {
        instancesLoading_ = false;
        if (!r.ok) {
            instancesError_ = r.error;
            return;
        }
        instancesError_.clear();
        std::vector<Instance> list;
        for (const auto& j : jsonutil::extractArray(r.data, {"instances"})) {
            if (j.is_object()) list.push_back(j.get<Instance>());
        }
        instances_ = std::move(list);
        instancesFetchedAt_ = ImGui::GetTime();
        const double now = fmt::nowEpoch();
        for (const auto& inst : instances_) {
            if (!inst.costNowHr) continue;
            auto it = std::find_if(costHistory_.begin(), costHistory_.end(), [&](const auto& e) { return e.first == inst.id; });
            if (it == costHistory_.end()) {
                costHistory_.push_back({inst.id, {}});
                it = std::prev(costHistory_.end());
            }
            const double rate = *inst.costNowHr;
            it->second.emplace_back(now, rate);
            if (it->second.size() > 720) it->second.erase(it->second.begin());
        }
    });
}

const std::vector<std::pair<double, double>>* App::costHistory(std::int64_t id) const {
    for (const auto& e : costHistory_) {
        if (e.first == id) return &e.second;
    }
    return nullptr;
}

App::AccountCost App::accountCost() const {
    AccountCost c;
    for (const auto& i : instances_) {
        const double now = i.costNowHr.value_or(0.0);
        if (!i.prepaid()) {
            c.rate += now;
            continue;
        }
        c.prepaidRate += now;
        ++c.prepaidCount;
        // The prepaid credit lasts credit_balance / reserved rate hours; then the normal price is billed.
        if (now > 0.0) c.later.push_back({*i.creditBalance / now, i.costListHr.value_or(now)});
    }
    return c;
}

void App::refreshUser(std::function<void()> then) {
    ApiCall c = cmd::showUser();
    c.quiet = true;
    userFetchedAt_ = ImGui::GetTime();
    client_->submit(std::move(c), [this, then = std::move(then)](const ApiResult& r) {
        if (r.ok && r.data.is_object()) {
            user_ = r.data;
            user_.erase("api_key");
        }
        if (then) then();
    });
}

void App::setCurrentTemplate(const Template& t) {
    settings_.currentTemplate = t;
    search_->onTemplateChanged(*this);
    markSettingsDirty();
}

void App::autoRefresh() {
    if (apiKey_.key.empty() || !settings_.autoRefresh) return;
    const double now = ImGui::GetTime();
    const double interval = std::max(5, settings_.refreshIntervalSec);
    if (!instancesLoading_ && now - instancesAttemptedAt_ > interval) refreshInstances(true);
    if (now - userFetchedAt_ > std::max(120.0, interval * 4)) refreshUser();
}

// ---------------------------------------------------------------------------

void App::preFrame() {
    if (appearanceDirty_) {
        appearanceDirty_ = false;
        applyAppearance();
    }
}

void App::frame() {
    client_->poll();
    autoRefresh();

    const float s = dpiScale_ * settings_.uiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##Root", nullptr, rootFlags);
    ImGui::PopStyleVar(3);

    const float sidebarW = (settings_.sidebarCollapsed ? 56.0f : 222.0f) * s;
    const float statusH = ImGui::GetFrameHeight() + 6.0f * s;
    drawSidebar(sidebarW);

    ImGui::SetCursorPos(ImVec2(sidebarW, 0));
    const ImVec2 dockSize(std::max(1.0f, vp->WorkSize.x - sidebarW), std::max(1.0f, vp->WorkSize.y - statusH));
    dockspaceId_ = ImGui::GetID("MainDockSpace");
    if (firstFrame_ && ImGui::DockBuilderGetNode(dockspaceId_) == nullptr) {
        ImGui::DockBuilderAddNode(dockspaceId_, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId_, dockSize);
        ImGuiID center = dockspaceId_;
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###Page", center);
        ImGui::DockBuilderDockWindow("Activity###ActivityLog", bottom);
        ImGui::DockBuilderFinish(dockspaceId_);
    }
    ImGui::DockSpace(dockspaceId_, dockSize, ImGuiDockNodeFlags_AutoHideTabBar);

    ImGui::SetCursorPos(ImVec2(sidebarW, vp->WorkSize.y - statusH));
    drawStatusBar();
    ImGui::End();

    drawPage();
    if (settings_.showActivityLog) drawActivityLog();
    instancesView_->drawFloating(*this);
    serverless_->drawFloating(*this);
    search_->drawFloating(*this);
    toasts_.draw();

    saveIfDirty(false);
    firstFrame_ = false;
}

void App::drawSidebar(float width) {
    const ui::Palette& p = ui::palette();
    const float s = dpiScale_ * settings_.uiScale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.sidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 12 * s));
    ImGui::BeginChild("##Sidebar", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    const bool collapsed = settings_.sidebarCollapsed;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Logo
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFontSize() * 1.9f;
        const ImVec2 c(pos.x + h * 0.5f + 4 * s, pos.y + h * 0.5f);
        dl->AddCircleFilled(c, h * 0.5f, ui::col(p.accent), 32);
        const ImVec2 v1(c.x - h * 0.22f, c.y - h * 0.18f), v2(c.x, c.y + h * 0.22f), v3(c.x + h * 0.22f, c.y - h * 0.18f);
        dl->AddLine(v1, v2, IM_COL32_WHITE, h * 0.11f);
        dl->AddLine(v2, v3, IM_COL32_WHITE, h * 0.11f);
        if (!collapsed) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
            dl->AddText(ImVec2(pos.x + h + 12 * s, pos.y + (h - ImGui::GetFontSize()) * 0.5f), ui::col(p.text), "vast.ai");
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(width - 16 * s, h));
        if (!collapsed) {
            const double credit = jsonutil::num(user_, "credit", jsonutil::num(user_, "balance", 0.0));
            if (!user_.is_null()) {
                ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
                ImGui::Text("Credit: %s", fmt::money(credit, settings_.currency, 2).c_str());
                ImGui::PopStyleColor();
            } else {
                ui::TextDim(apiKey_.key.empty() ? "No API key" : " ");
            }
        }
        ImGui::Spacing();
    }

    int running = 0;
    for (const auto& i : instances_) {
        if (i.status() == "running") ++running;
    }

    const float itemH = ImGui::GetFrameHeight() * 1.18f;
    for (const auto& item : kNav) {
        if (item.separatorBefore) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ui::lerpColor(p.border, p.sidebar, 0.4f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        const bool active = page_ == item.page;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(static_cast<int>(item.page));
        if (ImGui::InvisibleButton("##nav", ImVec2(w, itemH))) navigate(item.page);
        const bool hovered = ImGui::IsItemHovered();
        if (collapsed && hovered) ImGui::SetTooltip("%s", item.label);
        ImGui::PopID();
        const ImVec2 max(pos.x + w, pos.y + itemH);
        if (active) {
            dl->AddRectFilled(pos, max, ui::col(ui::lerpColor(p.sidebar, p.accent, 0.22f)), 6.0f * s);
            dl->AddRectFilled(pos, ImVec2(pos.x + 3 * s, max.y), ui::col(p.accent), 2.0f);
        } else if (hovered) {
            dl->AddRectFilled(pos, max, ui::col(ui::lerpColor(p.sidebar, p.text, 0.06f)), 6.0f * s);
        }
        const ImVec4 tc = active ? p.text : (hovered ? p.text : p.textDim);
        const float ty = pos.y + (itemH - ImGui::GetFontSize()) * 0.5f;
        const float iconX = collapsed ? pos.x + (w - ImGui::CalcTextSize(item.icon).x) * 0.5f : pos.x + 12 * s;
        dl->AddText(ImVec2(iconX, ty), ui::col(active ? p.accent : tc), item.icon);
        if (!collapsed) dl->AddText(ImVec2(pos.x + 42 * s, ty), ui::col(tc), item.label);
        if (item.page == Page::Instances && running > 0) {
            const ImVec2 bc = collapsed ? ImVec2(max.x - 8 * s, pos.y + 8 * s) : ImVec2(max.x - 18 * s, pos.y + itemH * 0.5f);
            ui::CountBubble(dl, bc, running, p.success);
        }
    }

    // Bottom-pinned: Activity window toggle, then the collapse toggle.
    const float bottomY = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - itemH - 20 * s;
    if (ImGui::GetCursorPosY() < bottomY) ImGui::SetCursorPosY(bottomY);
    {
        const bool shown = settings_.showActivityLog;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##activitytoggle", ImVec2(w, itemH))) {
            settings_.showActivityLog = !shown;
            markSettingsDirty();
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetTooltip("%s", shown ? "Hide the Activity window" : "Show the Activity window");
        const ImVec2 max(pos.x + w, pos.y + itemH);
        if (hovered) dl->AddRectFilled(pos, max, ui::col(ui::lerpColor(p.sidebar, p.text, 0.06f)), 6.0f * s);
        const float ty = pos.y + (itemH - ImGui::GetFontSize()) * 0.5f;
        const char* icon = ICON_FA_TABLE_LIST;
        const float iconX = collapsed ? pos.x + (w - ImGui::CalcTextSize(icon).x) * 0.5f : pos.x + 12 * s;
        dl->AddText(ImVec2(iconX, ty), ui::col(shown ? p.accent : p.textDim), icon);
        if (!collapsed) {
            dl->AddText(ImVec2(pos.x + 42 * s, ty), ui::col(hovered ? p.text : p.textDim), "Activity log");
            // Small on/off switch on the right.
            const float sh = ImGui::GetFontSize() * 0.75f;
            const float sw = sh * 1.8f;
            const ImVec2 a(max.x - sw - 8 * s, pos.y + (itemH - sh) * 0.5f);
            const ImVec2 b(a.x + sw, a.y + sh);
            dl->AddRectFilled(a, b, ui::col(shown ? p.accent : p.border), sh * 0.5f);
            const float r = sh * 0.5f - 2.0f;
            dl->AddCircleFilled(ImVec2(shown ? b.x - sh * 0.5f : a.x + sh * 0.5f, a.y + sh * 0.5f), r, IM_COL32_WHITE, 16);
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (ui::IconButton(collapsed ? ICON_FA_CHEVRON_RIGHT : ICON_FA_CHEVRON_LEFT,
                       collapsed ? "Expand sidebar" : "Collapse sidebar")) {
        settings_.sidebarCollapsed = !settings_.sidebarCollapsed;
        markSettingsDirty();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::drawStatusBar() {
    const ui::Palette& p = ui::palette();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
    const float s = dpiScale_ * settings_.uiScale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.sidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 3 * s));
    ImGui::BeginChild("##StatusBar", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::AlignTextToFramePadding();
    ui::Badge("API", p.accent, false, ("vast.ai REST API: " + client_->serverUrl()).c_str());
    ImGui::SameLine();
    if (apiKey_.key.empty()) {
        if (ui::Badge(ICON_FA_KEY "  no API key", p.danger, false, "Click to open Settings")) navigate(Page::Settings);
    } else {
        const std::string tip = std::string("API key ") + fmt::maskedKey(apiKey_.key) + " from " + apiKeySourceLabel(apiKey_.source);
        ui::Badge(ICON_FA_KEY "  key", p.success, false, tip.c_str());
    }
    ImGui::SameLine();
    // Fixed-size activity slot: spinner while busy, idle icon otherwise - the text after it never moves.
    const int n = client_->inFlight();
    {
        const float slot = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(slot, slot));
        const ImVec2 c(pos.x + slot * 0.5f, pos.y + slot * 0.5f);
        if (n > 0) {
            const float r = ImGui::GetFontSize() * 0.36f;
            ui::DrawSpinner(ImGui::GetWindowDrawList(), c, r, std::max(1.5f, r * 0.3f), ui::col(p.accent));
        } else {
            const ImVec2 is = ImGui::CalcTextSize(ICON_FA_CIRCLE_CHECK);
            ImGui::GetWindowDrawList()->AddText(ImVec2(c.x - is.x * 0.5f, c.y - is.y * 0.5f), ui::col(p.textDim, 0.7f),
                                                ICON_FA_CIRCLE_CHECK);
        }
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (n > 0) {
        const auto titles = client_->inFlightTitles();
        std::string t = titles.empty() ? std::string{} : titles.front();
        if (titles.size() > 1) t += std::format(" (+{} more)", titles.size() - 1);
        ui::TextDim(t.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(titles, "\n").c_str());
    } else {
        ui::TextDim("Idle");
    }

    // Right-aligned section.
    std::string right;
    if (instancesFetchedAt_ > 0) {
        right = std::format("Instances updated {:.0f}s ago", ImGui::GetTime() - instancesFetchedAt_);
    }
    const float rw = ImGui::CalcTextSize(right.c_str()).x + 16.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowWidth() - rw));
    ImGui::AlignTextToFramePadding();
    ui::TextDim(right.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::drawActivityLog() {
    ImGui::SetNextWindowDockID(dockspaceId_, ImGuiCond_FirstUseEver);
    bool open = true;
    const bool visible = ImGui::Begin("Activity###ActivityLog", &open);
    if (!open) {
        settings_.showActivityLog = false;
        markSettingsDirty();
    }
    if (!visible) {
        ImGui::End();
        return;
    }
    activityVersion_ = log_.copySince(activityVersion_, activityCache_);
    if (activityCache_.size() > 2000) activityCache_.erase(activityCache_.begin(), activityCache_.end() - 1500);

    ui::SectionTitle(ICON_FA_TERMINAL, "Activity");
    ImGui::SameLine();
    ui::TextDim("- every action shows the API request it sent");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetFontSize() * 11.0f);
    if (ui::SecondaryButton(ICON_FA_LIST " Full log")) navigate(Page::Console);
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_TRASH " Clear")) {
        activityCache_.clear();
    }

    const ui::Palette& p = ui::palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.sidebar);
    if (ImGui::BeginChild("##activity", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
        ui::PushCompactLines();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(activityCache_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogEntry& e = activityCache_[static_cast<std::size_t>(i)];
                ImVec4 c = p.textDim;
                switch (e.level) {
                    case LogLevel::Command: c = p.accent; break;
                    case LogLevel::Error: c = p.danger; break;
                    case LogLevel::Warning: c = p.warning; break;
                    case LogLevel::Output: c = p.text; break;
                    case LogLevel::Info: break;
                }
                const auto nl = e.text.find('\n');
                const std::string first = nl == std::string::npos ? e.text : e.text.substr(0, nl) + "  ...";
                const std::string time = fmt::dateTimeFromEpoch(e.time).substr(11);
                ImGui::TextDisabled("%s", time.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextUnformatted(first.c_str());
                ImGui::PopStyleColor();
                if (nl != std::string::npos && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", e.text.substr(0, 3000).c_str());
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::SetClipboardText(e.text.c_str());
            }
        }
        ImGui::PopStyleVar();
        if (ui::monoFont()) ImGui::PopFont();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

void App::drawPage() {
    const NavItem& item = navItem(page_);
    const std::string title = std::string(item.label) + "###Page";
    ImGui::SetNextWindowDockID(dockspaceId_, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    switch (page_) {
        case Page::Search: search_->draw(*this); break;
        case Page::Templates: templates_->draw(*this); break;
        case Page::Instances: instancesView_->draw(*this); break;
        case Page::Storage: storage_->draw(*this); break;
        case Page::Serverless: serverless_->draw(*this); break;
        case Page::Billing: billing_->draw(*this); break;
        case Page::Earnings: earnings_->draw(*this); break;
        case Page::Team: team_->draw(*this); break;
        case Page::AuditLogs: audit_->draw(*this); break;
        case Page::Keys: keys_->draw(*this); break;
        case Page::Settings: settingsView_->draw(*this); break;
        case Page::Hosting: hosting_->draw(*this); break;
        case Page::Console: console_->draw(*this); break;
        case Page::Count: break;
    }
    ImGui::End();
}

// ---- Resource ---------------------------------------------------------------

void Resource::fetch(App& app, ApiCall call, After after) {
    if (loading) return;
    loading = true;
    attemptedAt = ImGui::GetTime();
    app.client().submit(std::move(call), [this, after = std::move(after)](const ApiResult& r) {
        loading = false;
        if (r.ok) {
            data = r.data.is_null() ? json(r.text) : r.data;
            loaded = true;
            error.clear();
            fetchedAt = ImGui::GetTime();
        } else {
            error = r.error;
        }
        if (after) after(*this, r);
    });
}

void Resource::autoFetch(App& app, const ApiCall& call, After after) {
    if (loading || app.apiKey().key.empty()) return;
    const double now = ImGui::GetTime();
    const Settings& s = app.settings();
    const bool due = !loaded ? (attemptedAt == 0 || (!error.empty() && now - attemptedAt > std::max(30, s.refreshIntervalSec)))
                             : (s.autoRefresh && now - attemptedAt > std::max(5, s.refreshIntervalSec));
    if (!due) return;
    ApiCall c = call;
    c.quiet = loaded;
    fetch(app, std::move(c), std::move(after));
}

double Resource::age() const { return fetchedAt > 0 ? ImGui::GetTime() - fetchedAt : -1.0; }

}  // namespace vast
