#include "views/TemplatesView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/DockerOptions.hpp"
#include "logic/Format.hpp"
#include "model/Template.hpp"
#include "widgets/Badge.hpp"
#include "widgets/Spinner.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <memory>
#include <set>

namespace vast {

namespace {

// Collapses whitespace and cuts `s` (with "...") so it wraps to at most `lines` lines at `wrapW`.
std::string fitLines(const std::string& in, float wrapW, int lines) {
    std::string s;
    s.reserve(in.size());
    for (char ch : in) {
        const bool ws = ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ';
        if (ws) {
            if (!s.empty() && s.back() != ' ') s.push_back(' ');
        } else {
            s.push_back(ch);
        }
    }
    const float maxH = ImGui::GetTextLineHeight() * static_cast<float>(lines) + 0.5f;
    auto fits = [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str(), nullptr, false, wrapW).y <= maxH; };
    if (fits(s)) return s;
    std::size_t lo = 0;
    std::size_t hi = s.size();
    while (lo < hi) {  // longest prefix that still fits with the ellipsis
        const std::size_t mid = (lo + hi + 1) / 2;
        if (fits(s.substr(0, mid) + "...")) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    while (lo > 0 && (static_cast<unsigned char>(s[lo]) & 0xC0) == 0x80) --lo;
    while (lo > 0 && s[lo - 1] == ' ') --lo;
    return s.substr(0, lo) + "...";
}

void fetchMyTemplatesImpl(App& app, Resource& res, bool allowUserRefresh) {
    if (res.loading) return;
    const auto uid = jsonutil::optInt(app.user(), "id");
    if (!uid && allowUserRefresh && !app.apiKey().key.empty()) {
        // The user id is needed for the creator filter; load it first.
        res.loading = true;
        app.refreshUser([&app, &res] {
            res.loading = false;
            fetchMyTemplatesImpl(app, res, false);
        });
        return;
    }
    struct Merge {
        json rows = json::array();
        std::set<std::string> seen;
        int pending = 0;
        bool anyOk = false;
        std::string error;
    };
    auto merge = std::make_shared<Merge>();
    std::vector<ApiCall> calls;
    if (uid) calls.push_back(cmd::searchTemplates("creator_id=" + std::to_string(*uid)));
    calls.push_back(cmd::searchTemplates("private=true"));
    merge->pending = static_cast<int>(calls.size());
    res.loading = true;
    res.attemptedAt = ImGui::GetTime();
    for (auto& c : calls) {
        app.client().submit(std::move(c), [&res, merge](const ApiResult& r) {
            if (r.ok) {
                merge->anyOk = true;
                for (const auto& row : jsonutil::extractArray(r.data, {"templates"})) {
                    const std::string key = jsonutil::str(row, "id") + "/" + jsonutil::str(row, "hash_id");
                    if (merge->seen.insert(key).second) merge->rows.push_back(row);
                }
            } else {
                merge->error = r.error;
            }
            if (--merge->pending > 0) return;
            res.loading = false;
            if (merge->anyOk) {
                res.data = std::move(merge->rows);
                res.loaded = true;
                res.error.clear();
                res.fetchedAt = ImGui::GetTime();
            } else {
                res.error = merge->error;
            }
        });
    }
}

}  // namespace

void fetchMyTemplates(App& app, Resource& res) { fetchMyTemplatesImpl(app, res, true); }

void TemplatesView::load(App& app, int scope, bool force) {
    Resource& r = res_[static_cast<std::size_t>(scope)];
    if (r.loading || (r.loaded && !force)) return;
    if (scope == kMine) {
        fetchMyTemplates(app, r);
    } else if (scope == kRecommended) {
        r.fetch(app, cmd::searchTemplates("recommended=true"));
    } else {
        r.fetch(app, cmd::searchTemplates(fmt::trim(query_)));
    }
}

void TemplatesView::reloadAfterChange(App& app) {
    // Other tabs may show the changed template too; reload them when they are next shown.
    for (int t = 0; t < 3; ++t) {
        if (t != scope_) res_[static_cast<std::size_t>(t)].loaded = false;
    }
    load(app, scope_, true);
}

void TemplatesView::editFrom(const json& row, bool asNew) {
    const Template t = row.get<Template>();
    form_ = cmd::TemplateParams{};
    form_.id = asNew ? std::nullopt : t.id;
    form_.name = asNew ? t.name + " (copy)" : t.name;
    form_.image = t.image;
    form_.tag = t.tag;
    form_.onstart = t.onstart;
    form_.readme = t.readme;
    form_.desc = t.desc;
    form_.href = t.href;
    form_.repo = t.repo;
    form_.ssh = t.usesSsh();
    form_.jupyter = t.usesJupyter();
    form_.direct = t.isDirect();
    form_.isPublic = !t.isPrivate;
    form_.diskSpaceGb = t.recommendedDiskGb;
    const DockerOptions o = parseDockerOptions(t.env);
    env_ = o.env;
    ports_ = o.ports;
    extra_ = o.extra;
    launchMode_ = t.usesJupyter() ? 1 : (t.runtype == "args" ? 2 : 0);
    editing_ = !asNew && !t.hashId.empty();
    editHash_ = t.hashId;
    formOpen_ = true;
}

void TemplatesView::useTemplate(App& app, const json& row) {
    app.setCurrentTemplate(row.get<Template>());
    app.navigate(Page::Search);
}

void TemplatesView::rowMenu(App& app, const json& row) {
    if (ImGui::MenuItem(ICON_FA_ROCKET "  Use for search / rent")) useTemplate(app, row);
    if (ImGui::MenuItem(ICON_FA_PEN "  Edit")) editFrom(row, false);
    if (ImGui::MenuItem(ICON_FA_COPY "  Duplicate as new")) editFrom(row, true);
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy hash ID")) ImGui::SetClipboardText(jsonutil::str(row, "hash_id").c_str());
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy image")) ImGui::SetClipboardText(row.get<Template>().imageWithTag().c_str());
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
        pendingDelete_ = row;
        openDelete_ = true;
    }
}

void TemplatesView::drawCards(App& app, const json& rows) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const int n = static_cast<int>(rows.size());
    if (n == 0) {
        const Resource& res = res_[static_cast<std::size_t>(scope_)];
        ui::TextDim(res.loading ? "Loading templates..." : "No templates.");
        return;
    }
    // Responsive grid like the console: as many columns as fit at a minimum card width.
    const float availW = ImGui::GetContentRegionAvail().x - style.ScrollbarSize;
    const float minW = fs * 24.0f;
    const int cols = std::max(1, static_cast<int>((availW + style.ItemSpacing.x) / (minW + style.ItemSpacing.x)));
    const float cardW = std::floor((availW - style.ItemSpacing.x * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    const float cardH = style.WindowPadding.y * 2.0f + fs * 2.4f + ui::BadgeSize("X").y + ImGui::GetTextLineHeight() * 2.0f +
                        ImGui::GetFrameHeight() + style.ItemSpacing.y * 3.0f;
    const int rowCount = (n + cols - 1) / cols;

    // Sorted order, rebuilt only when the data or the sort changes.
    const Settings& st = app.settings();
    const Resource& res = res_[static_cast<std::size_t>(scope_)];
    const auto key = std::make_tuple(scope_, res.fetchedAt, rows.size(), st.templateSort, st.templateSortDesc);
    if (key != cardOrderKey_) {
        cardOrderKey_ = key;
        struct K { std::string name; std::optional<double> date; std::int64_t id; };
        std::vector<K> keys;
        keys.reserve(rows.size());
        for (const auto& r : rows) {
            keys.push_back({fmt::toLower(jsonutil::str(r, "name")), jsonutil::optDouble(r, "created_at"),
                            jsonutil::optInt(r, "id").value_or(0)});
        }
        cardOrder_.resize(rows.size());
        for (std::size_t i = 0; i < cardOrder_.size(); ++i) cardOrder_[i] = i;
        const bool byDate = st.templateSort == "date";
        const bool desc = st.templateSortDesc;
        std::stable_sort(cardOrder_.begin(), cardOrder_.end(), [&](std::size_t a, std::size_t b) {
            const K& x = keys[a];
            const K& y = keys[b];
            if (byDate && x.date != y.date) {
                if (!x.date || !y.date) return x.date.has_value();  // undated templates last
                return desc ? *x.date > *y.date : *x.date < *y.date;
            }
            if (x.name != y.name) return (desc && !byDate) ? x.name > y.name : x.name < y.name;
            return x.id < y.id;
        });
    }

    ImGui::BeginChild("##tplcards", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGuiListClipper clipper;
    clipper.Begin(rowCount, cardH + style.ItemSpacing.y);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int pos = r * cols + c;
                if (pos >= n) break;
                if (c > 0) ImGui::SameLine();
                const std::size_t idx = cardOrder_[static_cast<std::size_t>(pos)];
                drawCard(app, rows[idx], static_cast<int>(idx), ImVec2(cardW, cardH));
            }
        }
    }
    ImGui::EndChild();
}

void TemplatesView::drawCard(App& app, const json& row, int index, const ImVec2& size) {
    const ui::Palette& p = ui::palette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const Template t = row.get<Template>();
    const Template& current = app.settings().currentTemplate;
    const bool selected = !t.hashId.empty() ? t.hashId == current.hashId
                                            : (t.name == current.name && t.imageWithTag() == current.imageWithTag());

    ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.card);
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? p.info : p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, selected ? std::max(1.5f, style.ChildBorderSize * 1.5f) : style.ChildBorderSize);
    ImGui::BeginChild("##tcard", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ui::col(p.accent, 0.05f), 8.0f);
    }
    const float innerW = ImGui::GetContentRegionAvail().x;

    // Header: initial avatar, name and image.
    const float av = fs * 2.4f;
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(a0, ImVec2(a0.x + av, a0.y + av), ui::col(p.accent, 0.18f), av * 0.25f);
    char initial[2] = {'?', 0};
    for (char ch : t.name) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            break;
        }
    }
    const float initSize = fs * 1.3f;
    ImGui::PushFont(nullptr, initSize);
    const ImVec2 is = ImGui::CalcTextSize(initial);
    ImGui::PopFont();
    dl->AddText(nullptr, initSize, ImVec2(a0.x + (av - is.x) * 0.5f, a0.y + (av - is.y) * 0.5f), ui::col(p.accent), initial);
    ImGui::Dummy(ImVec2(av, av));
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float textW = innerW - av - style.ItemSpacing.x;
    ImGui::PushFont(nullptr, style.FontSizeBase * 1.15f);
    ImGui::TextUnformatted(ui::Ellipsize(t.name.empty() ? std::string("(unnamed)") : t.name, textW).c_str());
    ImGui::PopFont();
    ui::TextDim(ui::Ellipsize(t.imageWithTag(), textW).c_str());
    ImGui::EndGroup();
    if (ImGui::IsItemHovered() && !t.hashId.empty()) ImGui::SetTooltip("%s\nhash %s", t.imageWithTag().c_str(), t.hashId.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a0.y + av + style.ItemSpacing.y));

    // Badges (only as many as fit on one line).
    struct B { std::string text; ImVec4 color; const char* tip; };
    std::vector<B> badges;
    if (t.recommended) badges.push_back({"recommended", p.caution, "Recommended by vast.ai"});
    badges.push_back({t.isPrivate ? "private" : "public", p.textDim, nullptr});
    const char* mode = t.usesJupyter() ? "Jupyter" : (t.runtype == "args" ? "Entrypoint" : "SSH");
    badges.push_back({t.isDirect() ? std::string(mode) + " direct" : std::string(mode), p.info, "Launch mode"});
    if (t.recommendedDiskGb) badges.push_back({std::format("{:.0f} GB disk", *t.recommendedDiskGb), p.textDim, "Recommended disk space"});
    if (t.countCreated && *t.countCreated > 0) {
        badges.push_back({std::format("used {}x", fmt::compactNumber(*t.countCreated)), p.textDim, "Instances created from this template"});
    }
    float x = 0.0f;
    bool first = true;
    for (const auto& b : badges) {
        const float bw = ui::BadgeSize(b.text.c_str()).x;
        if (x + bw > innerW) break;
        if (!first) ImGui::SameLine();
        ui::Badge(b.text.c_str(), b.color, false, b.tip);
        x += bw + style.ItemSpacing.x;
        first = false;
    }

    // Description: two lines at most.
    const std::string desc = !t.desc.empty() ? t.desc : t.readme;
    const std::string shown = fitLines(desc.empty() ? std::string("No description.") : desc, innerW, 2);
    const ImVec2 d0 = ImGui::GetCursorScreenPos();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerW);
    ui::TextDim(shown.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered() && shown != desc && !desc.empty()) {
        ImGui::SetNextWindowSize(ImVec2(fs * 30.0f, 0));
        ImGui::SetTooltip("%s", desc.substr(0, 1500).c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(d0.x, d0.y + ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y));

    // Footer: select + edit / duplicate / more.
    const float iconsW = ImGui::CalcTextSize(ICON_FA_PEN).x + ImGui::CalcTextSize(ICON_FA_COPY).x +
                         ImGui::CalcTextSize(ICON_FA_ELLIPSIS_VERTICAL).x + style.FramePadding.x * 6.0f + style.ItemSpacing.x * 3.0f;
    const ImVec2 selSize(std::max(fs * 6.0f, innerW - iconsW), 0);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.info.x, p.info.y, p.info.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_Text, p.info);
        ImGui::BeginDisabled();
        ImGui::Button(ICON_FA_CHECK "  SELECTED", selSize);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This template is used for search and rent.");
    } else if (ImGui::Button(ICON_FA_ROCKET "  Select", selSize)) {
        useTemplate(app, row);
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use this template for search and rent");
    }
    ImGui::SameLine();
    if (ui::IconButton(ICON_FA_PEN, "Edit")) editFrom(row, false);
    ImGui::SameLine();
    if (ui::IconButton(ICON_FA_COPY, "Duplicate as new")) editFrom(row, true);
    ImGui::SameLine();
    if (ui::IconButton(ICON_FA_ELLIPSIS_VERTICAL, "More")) ImGui::OpenPopup("##tplmore");
    if (ImGui::BeginPopup("##tplmore")) {
        rowMenu(app, row);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupContextWindow("##tplctx")) {
        rowMenu(app, row);
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        editFrom(row, false);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void TemplatesView::draw(App& app) {
    ui::PageHeader(ICON_FA_LAYER_GROUP, "Templates", "search, create, edit and delete templates");
    if (scope_ < 0) {
        scope_ = app.apiKey().key.empty() ? kRecommended : kMine;
        scopeRequest_ = scope_;
    }

    // Tab order: My templates first.
    struct TabDef { int scope; const char* label; };
    constexpr TabDef kTabs[] = {{kMine, ICON_FA_USER " My templates"},
                                {kRecommended, ICON_FA_STAR " Recommended"},
                                {kSearch, ICON_FA_MAGNIFYING_GLASS " Search"}};
    if (ImGui::BeginTabBar("##tplscope")) {
        for (const auto& t : kTabs) {
            const ImGuiTabItemFlags tf = scopeRequest_ == t.scope ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(t.label, nullptr, tf)) {
                scope_ = t.scope;
                ImGui::EndTabItem();
            }
        }
        scopeRequest_ = -1;
        ImGui::EndTabBar();
    }
    Resource& res = res_[static_cast<std::size_t>(scope_)];
    const bool needsKey = scope_ == kMine && app.apiKey().key.empty();
    // Load each tab once when first shown; after an error only the Refresh button retries.
    if (!res.loaded && !res.loading && res.error.empty() && !needsKey) load(app, scope_, false);

    // Same toolbar on every tab so switching never shifts the layout; the query only applies to Search.
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, scope_ == kSearch ? "Search" : "Refresh", res.loading)) load(app, scope_, true);
    ImGui::SameLine();
    ImGui::BeginDisabled(scope_ != kSearch);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    if (ImGui::InputTextWithHint("##tq", "query, e.g. name=PyTorch or image=vastai/comfy", &query_,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        load(app, kSearch, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_PLUS " New template")) {
        form_ = cmd::TemplateParams{};
        form_.image = "vastai/base-image";
        form_.tag = "@vastai-automatic-tag";
        env_.clear();
        ports_ = {"8080:8080"};
        extra_.clear();
        launchMode_ = 0;
        editing_ = false;
        editHash_.clear();
        formOpen_ = true;
    }
    if (needsKey) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::TextWithColor(ui::palette().warning, ICON_FA_KEY " Set an API key in Settings to list your templates.");
    } else {
        ui::InlineError(res.error);
    }
    {
        // Sort (card view) + card / table toggle, right-aligned on the toolbar line.
        Settings& st = app.settings();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec4 on = ui::palette().accent;
        const char* sortIcon = st.templateSortDesc ? ICON_FA_ARROW_DOWN_WIDE_SHORT : ICON_FA_ARROW_UP_WIDE_SHORT;
        const float comboW = ImGui::GetFontSize() * 7.5f;
        const float toggleW = ImGui::CalcTextSize(ICON_FA_GRIP).x + ImGui::CalcTextSize(ICON_FA_TABLE_LIST).x +
                              style.FramePadding.x * 4.0f;
        const float sortW = st.templateCardView
                                ? comboW + ImGui::CalcTextSize(sortIcon).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x * 3.0f
                                : 0.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - toggleW - sortW));
        if (st.templateCardView) {
            constexpr std::pair<const char*, const char*> kSorts[] = {{"name", "Name"}, {"date", "Date"}};
            const char* cur = st.templateSort == "date" ? "Date" : "Name";
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##tplsort", (std::string(ICON_FA_ARROW_DOWN_WIDE_SHORT "  ") + cur).c_str())) {
                for (const auto& [id, label] : kSorts) {
                    if (ImGui::Selectable(label, st.templateSort == id)) {
                        if (st.templateSort != id) st.templateSortDesc = std::string_view(id) == "date";  // newest first
                        st.templateSort = id;
                        app.markSettingsDirty();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sort templates by name or creation date");
            ImGui::SameLine(0, style.ItemSpacing.x * 0.5f);
            if (ui::IconButton(sortIcon, st.templateSortDesc ? "Descending (click for ascending)" : "Ascending (click for descending)")) {
                st.templateSortDesc = !st.templateSortDesc;
                app.markSettingsDirty();
            }
            ImGui::SameLine();
        }
        if (ui::IconButton(ICON_FA_GRIP, "Card view", st.templateCardView ? &on : nullptr)) {
            st.templateCardView = true;
            app.markSettingsDirty();
        }
        ImGui::SameLine(0, 0);
        if (ui::IconButton(ICON_FA_TABLE_LIST, "Table view", !st.templateCardView ? &on : nullptr)) {
            st.templateCardView = false;
            app.markSettingsDirty();
        }
    }

    const json rows = jsonutil::extractArray(res.data, {"templates"});
    const float formW = formOpen_ ? ImGui::GetContentRegionAvail().x * 0.42f : 0.0f;
    ImGui::BeginChild("##tpllist", ImVec2(formOpen_ ? ImGui::GetContentRegionAvail().x - formW - 8 : 0, 0));
    ImGui::PushID(scope_);
    if (app.settings().templateCardView) {
        drawCards(app, rows);
    } else {
        ui::JsonTableOptions opts;
        opts.preferredColumns = {"name", "image", "tag", "hash_id", "id", "recommended", "private", "count_created",
                                 "runtype", "recommended_disk_space", "creator_id"};
        opts.rowMenu = [&](const json& row, int) { rowMenu(app, row); };
        if (auto dbl = ui::JsonTable("##templates", rows, tables_[static_cast<std::size_t>(scope_)], opts)) {
            editFrom(rows[static_cast<std::size_t>(*dbl)], false);
        }
    }
    ImGui::PopID();
    ImGui::EndChild();
    if (formOpen_) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette().panel);
        ImGui::BeginChild("##tplform", ImVec2(0, 0), ImGuiChildFlags_Borders);
        drawForm(app);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    if (openDelete_) {
        ImGui::OpenPopup("Delete template?###tpldel");
        openDelete_ = false;
    }
    const std::string msg = std::format("Delete template \"{}\" ({})?", jsonutil::str(pendingDelete_, "name"),
                                        jsonutil::str(pendingDelete_, "hash_id"));
    if (ui::ConfirmModal("Delete template?###tpldel", msg, "Delete")) {
        app.run(cmd::deleteTemplate(jsonutil::optInt(pendingDelete_, "id"), jsonutil::str(pendingDelete_, "hash_id")),
                [this, &app](const ApiResult&) { reloadAfterChange(app); }, "Template deleted");
    }
}

void TemplatesView::drawForm(App& app) {
    const float s = app.dpiScale() * app.settings().uiScale;
    ui::SectionTitle(editing_ ? ICON_FA_PEN : ICON_FA_PLUS, editing_ ? "Edit template" : "Create template");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());
    if (ui::IconButton(ICON_FA_XMARK, "Close form")) formOpen_ = false;
    ImGui::Separator();
    const float labelW = 120 * s;
    auto label = [&](const char* t) {
        ImGui::AlignTextToFramePadding();
        ui::TextDim(t);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    label("Name");
    ImGui::InputText("##fname", &form_.name);
    label("Image");
    ImGui::InputText("##fimage", &form_.image);
    label("Tag");
    ImGui::InputText("##ftag", &form_.tag);
    label("Docker login");
    ImGui::InputTextWithHint("##flogin", "-u user -p token registry (private images)", &form_.login,
                             ImGuiInputTextFlags_Password);
    label("Description");
    ImGui::InputText("##fdesc", &form_.desc);
    label("Disk (GB)");
    double disk = form_.diskSpaceGb.value_or(0.0);
    if (ImGui::InputDouble("##fdisk", &disk, 1, 10, "%.0f")) form_.diskSpaceGb = disk > 0 ? std::optional(disk) : std::nullopt;

    label("Launch mode");
    ImGui::RadioButton("SSH", &launchMode_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Jupyter", &launchMode_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Args", &launchMode_, 2);
    if (launchMode_ != 2) {
        ImGui::SameLine();
        ImGui::Checkbox("Direct", &form_.direct);
    }
    if (launchMode_ == 1) {
        ImGui::SetCursorPosX(labelW);
        ImGui::Checkbox("JupyterLab", &form_.jupyterLab);
    }
    label("Visibility");
    ImGui::Checkbox("Public", &form_.isPublic);
    ImGui::SameLine();
    ImGui::Checkbox("Hide readme", &form_.hideReadme);

    ImGui::SeparatorText("Environment variables");
    int rm = -1;
    for (std::size_t i = 0; i < env_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::InputTextWithHint("##k", "NAME", &env_[i].first);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.4f);
        ImGui::InputTextWithHint("##v", "value", &env_[i].second);
        ImGui::SameLine();
        if (ui::IconButton(ICON_FA_XMARK, "Remove")) rm = static_cast<int>(i);
        ImGui::PopID();
    }
    if (rm >= 0) env_.erase(env_.begin() + rm);
    if (ui::SecondaryButton(ICON_FA_PLUS " Variable")) env_.emplace_back();

    ImGui::SeparatorText("Ports");
    rm = -1;
    for (std::size_t i = 0; i < ports_.size(); ++i) {
        ImGui::PushID(500 + static_cast<int>(i));
        ImGui::SetNextItemWidth(110 * s);
        ImGui::InputText("##p", &ports_[i]);
        ImGui::SameLine();
        if (ui::IconButton(ICON_FA_XMARK, "Remove")) rm = static_cast<int>(i);
        ImGui::PopID();
        if ((i + 1) % 3 != 0 && i + 1 < ports_.size()) ImGui::SameLine();
    }
    if (rm >= 0) ports_.erase(ports_.begin() + rm);
    if (ui::SecondaryButton(ICON_FA_PLUS " Port")) ports_.emplace_back();
    label("Docker options");
    ImGui::InputTextWithHint("##fextra", "--shm-size=8g ...", &extra_);
    label("Search params");
    ImGui::InputTextWithHint("##fsearch", "e.g. gpu_ram>=24 cuda_vers>=12.1", &form_.searchParams);

    ImGui::SeparatorText("On-start script");
    if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
    ImGui::InputTextMultiline("##fonstart", &form_.onstart, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5));
    if (ui::monoFont()) ImGui::PopFont();
    ImGui::SeparatorText("Readme");
    ImGui::InputTextMultiline("##freadme", &form_.readme, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5));

    form_.ssh = launchMode_ != 2;
    form_.jupyter = launchMode_ == 1;
    form_.env = buildDockerOptions(DockerOptions{env_, ports_, extra_});
    const ApiCall call = editing_ ? cmd::updateTemplate(editHash_, form_) : cmd::createTemplate(form_);
    ImGui::Separator();
    ImGui::BeginDisabled(form_.name.empty() || form_.image.empty());
    if (ImGui::Button(editing_ ? ICON_FA_FLOPPY_DISK " Save changes" : ICON_FA_PLUS " Create template")) {
        app.run(call, [this, &app](const ApiResult&) { reloadAfterChange(app); }, editing_ ? "Template updated" : "Template created");
    }
    ImGui::EndDisabled();
    if (editing_) {
        ImGui::SameLine();
        ui::TextDim("Edits create a new template version (new hash).");
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ROCKET " Use for rent")) {
        Template t;
        t.name = form_.name;
        t.image = form_.image;
        t.tag = form_.tag;
        t.env = form_.env;
        t.onstart = form_.onstart;
        t.runtype = launchMode_ == 1 ? "jupyter" : (launchMode_ == 2 ? "args" : "ssh");
        t.sshDirect = form_.direct && launchMode_ != 2;
        t.jupyterDirect = form_.direct && launchMode_ == 1;
        t.useSsh = launchMode_ != 2;
        t.hashId = editing_ ? editHash_ : std::string{};
        t.recommendedDiskGb = form_.diskSpaceGb;
        app.setCurrentTemplate(t);
        app.navigate(Page::Search);
    }
}

}  // namespace vast
