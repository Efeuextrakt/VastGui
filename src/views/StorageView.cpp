#include "views/StorageView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "logic/Format.hpp"
#include "views/SearchView.hpp"
#include "widgets/Spinner.hpp"

#include <format>

namespace vast {

void StorageView::draw(App& app) {
    ui::PageHeader(ICON_FA_HARD_DRIVE, "Storage", "volumes, volume offers, cloud connections and copies");
    if (ImGui::BeginTabBar("##storagetabs")) {
        if (ImGui::BeginTabItem(ICON_FA_DATABASE " Volumes")) {
            drawVolumes(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_MAGNIFYING_GLASS " Volume offers")) {
            drawOffers(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_CLOUD " Cloud connections")) {
            drawConnections(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_RIGHT_LEFT " Copy / cloud copy")) {
            drawCopy(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void StorageView::drawVolumes(App& app) {
    volumes_.autoFetch(app, cmd::showVolumes());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", volumes_.loading)) volumes_.fetch(app, cmd::showVolumes());
    ui::InlineError(volumes_.error);

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "name", "status", "disk_space", "size", "machine_id", "geolocation", "instances",
                             "dph_total", "start_date"};
    opts.rowMenu = [&](const json& row, int) {
        const auto id = jsonutil::optInt(row, "id").value_or(0);
        if (ImGui::MenuItem(ICON_FA_LINK "  Attach to a new rental (Search)")) {
            app.searchView().preselectVolume(app, id);
            app.navigate(Page::Search);
            app.toasts().info("Volume " + std::to_string(id) + " will be linked on the next rental",
                              "Pick an offer on the same machine.");
        }
        if (ImGui::MenuItem(ICON_FA_COPY "  Clone...")) {
            cloneFrom_ = row;
            cloneDest_ = 0;
            cloneSize_ = 0;
            openClone_ = true;
        }
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy volume path (V.id)")) {
            ImGui::SetClipboardText(std::format("V.{}:/", id).c_str());
        }
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingDelete_ = row;
            openDelete_ = true;
        }
    };
    ui::JsonTable("##volumes", jsonutil::extractArray(volumes_.data, {"volumes"}), volumesTable_, opts);

    if (openDelete_) {
        ImGui::OpenPopup("Delete volume?###voldel");
        openDelete_ = false;
    }
    const auto delId = jsonutil::optInt(pendingDelete_, "id").value_or(0);
    if (ui::ConfirmModal("Delete volume?###voldel",
                         std::format("Delete volume {}? All data on it is lost permanently.", delId), "Delete")) {
        app.run(cmd::deleteVolume(delId), [this, &app](const ApiResult&) { volumes_.fetch(app, cmd::showVolumes()); },
                "Volume deleted");
    }

    if (openClone_) {
        ImGui::OpenPopup("Clone volume###volclone");
        openClone_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Clone volume###volclone", ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto src = jsonutil::optInt(cloneFrom_, "id").value_or(0);
        ImGui::Text("Clone volume %lld", static_cast<long long>(src));
        ImGui::InputScalar("Destination volume offer ID", ImGuiDataType_S64, &cloneDest_);
        ImGui::InputDouble("Size GB (0 = same)", &cloneSize_, 1, 10, "%.0f");
        ui::TextDim("Find destination offers in the 'Volume offers' tab.");
        if (ImGui::Button("Clone")) {
            app.run(cmd::cloneVolume(src, cloneDest_, cloneSize_ > 0 ? std::optional(cloneSize_) : std::nullopt),
                    [this, &app](const ApiResult&) { volumes_.fetch(app, cmd::showVolumes()); }, "Clone started");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void StorageView::drawOffers(App& app) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    const bool enter = ImGui::InputTextWithHint("##vq", "query, e.g. disk_space>=100 geolocation=US", &offerQuery_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    ImGui::InputDouble("GB", &offerStorage_, 0, 0, "%.0f");
    ImGui::SameLine();
    if (ui::BusyButton(ICON_FA_MAGNIFYING_GLASS, "Search", offers_.loading) || enter) {
        offers_.fetch(app, cmd::searchVolumes(offerQuery_, offerStorage_));
    }
    ui::InlineError(offers_.error);

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "machine_id", "disk_space", "storage_cost", "disk_bw", "geolocation", "reliability",
                             "verification", "inet_up", "inet_down", "driver_version"};
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_PLUS "  Create volume...")) {
            createFrom_ = row;
            createSize_ = offerStorage_;
            createName_.clear();
            openCreate_ = true;
        }
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy offer ID")) {
            ImGui::SetClipboardText(std::to_string(jsonutil::optInt(row, "id").value_or(0)).c_str());
        }
    };
    if (auto dbl = ui::JsonTable("##voloffers", jsonutil::extractArray(offers_.data, {"offers"}), offersTable_, opts)) {
        createFrom_ = jsonutil::extractArray(offers_.data, {"offers"})[static_cast<std::size_t>(*dbl)];
        createSize_ = offerStorage_;
        openCreate_ = true;
    }

    if (openCreate_) {
        ImGui::OpenPopup("Create volume###volcreate");
        openCreate_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Create volume###volcreate", ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto id = jsonutil::optInt(createFrom_, "id").value_or(0);
        ImGui::Text("Volume offer %lld on machine %lld", static_cast<long long>(id),
                    static_cast<long long>(jsonutil::optInt(createFrom_, "machine_id").value_or(0)));
        ImGui::InputDouble("Size (GB)", &createSize_, 1, 10, "%.0f");
        ImGui::InputTextWithHint("Name", "optional", &createName_);
        const ApiCall call = cmd::createVolume(id, createSize_, createName_);
        if (ImGui::Button(ICON_FA_PLUS " Create")) {
            app.run(call, [this, &app](const ApiResult&) { volumes_.fetch(app, cmd::showVolumes()); }, "Volume created");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void StorageView::drawConnections(App& app) {
    connections_.autoFetch(app, cmd::showConnections());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", connections_.loading)) connections_.fetch(app, cmd::showConnections());
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Manage in vast.ai console")) {
        platform::openUrl("https://cloud.vast.ai/account/");
    }
    ui::InlineError(connections_.error);
    ui::TextDim("Cloud connections (S3, GCS, Backblaze, Dropbox...) are added in the vast.ai console and used by cloud copy.");
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "name", "cloud_type", "created_at"};
    ui::JsonTable("##connections", jsonutil::extractArray(connections_.data, {"results", "connections"}), connTable_, opts);
}

void StorageView::drawCopy(App& app) {
    const auto& inst = app.instances();
    ui::SectionTitle(ICON_FA_CLOUD, "Cloud copy");
    std::string preview = copyInstance_ >= 0 && static_cast<std::size_t>(copyInstance_) < inst.size()
                              ? std::format("{}  {}", inst[static_cast<std::size_t>(copyInstance_)].id,
                                            inst[static_cast<std::size_t>(copyInstance_)].label)
                              : std::string("select instance...");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    if (ImGui::BeginCombo("Instance", preview.c_str())) {
        for (std::size_t i = 0; i < inst.size(); ++i) {
            const std::string l = std::format("{}  {}  {}x {}  ({})", inst[i].id, inst[i].label, inst[i].numGpus,
                                              inst[i].gpuName, inst[i].status());
            if (ImGui::Selectable(l.c_str(), copyInstance_ == static_cast<int>(i))) copyInstance_ = static_cast<int>(i);
        }
        ImGui::EndCombo();
    }
    connections_.autoFetch(app, cmd::showConnections());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    if (ImGui::BeginCombo("Connection", copyConn_.empty() ? "select connection..." : copyConn_.c_str())) {
        for (const auto& c : jsonutil::extractArray(connections_.data, {"results", "connections"})) {
            const std::string id = jsonutil::str(c, "id");
            const std::string l = std::format("{}  {}  {}", id, jsonutil::str(c, "name"), jsonutil::str(c, "cloud_type"));
            if (ImGui::Selectable(l.c_str(), copyConn_ == id)) copyConn_ = id;
        }
        ImGui::EndCombo();
    }
    const char* dirs[] = {"Instance To Cloud", "Cloud To Instance"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    ImGui::Combo("Direction", &copyDirection_, dirs, 2);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    ImGui::InputText("Source path", &copySrc_);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    ImGui::InputText("Destination path", &copyDst_);
    ImGui::Checkbox("Dry run", &copyDryRun_);
    const bool ready = copyInstance_ >= 0 && static_cast<std::size_t>(copyInstance_) < inst.size() && !copyConn_.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(ICON_FA_CLOUD " Start cloud copy")) {
        cmd::CloudCopyParams p;
        p.instanceId = inst[static_cast<std::size_t>(copyInstance_)].id;
        p.connectionId = copyConn_;
        p.src = copySrc_;
        p.dst = copyDst_;
        p.transfer = dirs[copyDirection_];
        p.dryRun = copyDryRun_;
        app.run(cmd::cloudCopy(p), {}, "Cloud copy started");
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ui::SectionTitle(ICON_FA_RIGHT_LEFT, "Copy files");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    ImGui::InputTextWithHint("Source##plain", "C.123:/workspace/  or  V.45:/data/", &plainSrc_);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    ImGui::InputTextWithHint("Destination##plain", "C.456:/workspace/  or  s3.7:/bucket/", &plainDst_);
    ui::TextDim("Location formats: C.<instance>:path, V.<volume>:path, <cloud>.<connection>:path");
    ui::TextDim("Copies run on vast.ai between remote locations. To copy from/to this PC use an instance's SSH / SCP command.");
    ImGui::BeginDisabled(plainSrc_.empty() || plainDst_.empty());
    if (ImGui::Button(ICON_FA_RIGHT_LEFT " Copy")) {
        app.run(cmd::copyFiles(plainSrc_, plainDst_), {}, "Copy started - progress shows in the instance status");
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_BAN " Cancel copy to destination")) app.run(cmd::cancelCopy(plainDst_), {}, "Copy cancelled");
    ImGui::EndDisabled();
}

}  // namespace vast
