#include "views/TemplatesView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/DockerOptions.hpp"
#include "logic/Format.hpp"
#include "model/Template.hpp"
#include "widgets/Spinner.hpp"

#include <format>
#include <memory>
#include <set>

namespace vast {

namespace {

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

    const json rows = jsonutil::extractArray(res.data, {"templates"});
    const float formW = formOpen_ ? ImGui::GetContentRegionAvail().x * 0.42f : 0.0f;
    ImGui::BeginChild("##tpllist", ImVec2(formOpen_ ? ImGui::GetContentRegionAvail().x - formW - 8 : 0, 0));
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"name", "image", "tag", "hash_id", "id", "recommended", "private", "count_created",
                             "runtype", "recommended_disk_space", "creator_id"};
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_ROCKET "  Use for search / rent")) {
            app.setCurrentTemplate(row.get<Template>());
            app.navigate(Page::Search);
        }
        if (ImGui::MenuItem(ICON_FA_PEN "  Edit")) editFrom(row, false);
        if (ImGui::MenuItem(ICON_FA_COPY "  Duplicate as new")) editFrom(row, true);
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy hash ID")) ImGui::SetClipboardText(jsonutil::str(row, "hash_id").c_str());
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingDelete_ = row;
            openDelete_ = true;
        }
    };
    ImGui::PushID(scope_);
    if (auto dbl = ui::JsonTable("##templates", rows, tables_[static_cast<std::size_t>(scope_)], opts)) {
        editFrom(rows[static_cast<std::size_t>(*dbl)], false);
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
