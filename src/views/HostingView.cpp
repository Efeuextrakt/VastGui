#include "views/HostingView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "widgets/Badge.hpp"
#include "widgets/Spinner.hpp"

#include <cstdio>
#include <format>

namespace vast {

namespace {
const char* kMaintCategories[] = {"power", "internet", "disk", "gpu", "software", "other"};
}

void HostingView::open(Dialog d, const Machine& m) {
    dialog_ = d;
    target_ = m;
    openPopup_ = true;
    priceGpu_ = m.listedGpuCost.value_or(0.3);
    priceDisk_ = m.listedStorageCost.value_or(0.1);
    priceUp_ = m.listedInetUpCost.value_or(0.0) * 1000.0;
    priceDown_ = m.listedInetDownCost.value_or(0.0) * 1000.0;
    minBid_ = m.minBidPrice.value_or(priceGpu_ * 0.5);
    discount_ = 0.4;
    minChunk_ = 1;
    endDate_ = m.endDate ? fmt::dateFromEpoch(*m.endDate) : fmt::dateFromEpoch(fmt::nowEpoch() + 90 * 86400.0);
    image_ = "vastai/base-image:@vastai-automatic-tag";
    args_.clear();
    maintDate_ = fmt::dateFromEpoch(fmt::nowEpoch() + 86400.0);
}

void HostingView::draw(App& app) {
    ui::PageHeader(ICON_FA_MICROCHIP, "Hosting", "your machines");
    machines_.autoFetch(app, cmd::showMachines(), [this](Resource& r, const ApiResult&) {
        parsed_.clear();
        for (const auto& j : jsonutil::extractArray(r.data, {"machines"})) {
            if (j.is_object()) parsed_.push_back(j.get<Machine>());
        }
    });
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", machines_.loading)) {
        machines_.fetch(app, cmd::showMachines(), [this](Resource& r, const ApiResult&) {
            parsed_.clear();
            for (const auto& j : jsonutil::extractArray(r.data, {"machines"})) {
                if (j.is_object()) parsed_.push_back(j.get<Machine>());
            }
        });
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_WRENCH " Show maintenance windows")) {
        std::vector<std::int64_t> ids;
        for (const auto& m : parsed_) ids.push_back(m.id);
        target_ = Machine{};
        dialog_ = Dialog::Maints;
        openPopup_ = true;
        reports_.fetch(app, cmd::showMaints(ids));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##mfilter", ICON_FA_MAGNIFYING_GLASS " Filter", &filter_);
    ui::InlineError(machines_.error);
    if (machines_.loaded && parsed_.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::TextDim("No machines registered on this account.");
    }

    const Settings& st = app.settings();
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##machines", 13, flags)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        for (const char* c : {"ID", "Hostname", "GPU", "Listed", "Verification", "Reliability", "Rentals", "$/GPU/hr",
                              "Min bid", "Storage $/GB/mo", "End date", "Earn/day", "Actions"}) {
            ImGui::TableSetupColumn(c);
        }
        ImGui::TableHeadersRow();
        for (const auto& m : parsed_) {
            if (!filter_.empty() && !fmt::containsCi(m.raw.dump(), filter_)) continue;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(m.id));
            ImGui::TableNextColumn();
            ui::TableRowSelectable(std::to_string(m.id).c_str(), false);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open(Dialog::Json, m);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.hostname.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%dx %s", m.numGpus, m.gpuName.c_str());
            ImGui::TableNextColumn();
            ui::Badge(m.listed ? "listed" : "unlisted", m.listed ? ui::palette().success : ui::palette().textDim);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.verification.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::percent(m.reliability, 2).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::fixed(m.rentalsRunning, 0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::money(m.listedGpuCost, st.currency).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::money(m.minBidPrice, st.currency).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::money(m.listedStorageCost, st.currency).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.endDate ? fmt::dateFromEpoch(*m.endDate).c_str() : kMissing);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::money(m.earnDay, st.currency, 2).c_str());
            ImGui::TableNextColumn();
            if (ui::IconButton(ICON_FA_TAG, "List / set prices")) open(Dialog::List, m);
            ImGui::SameLine(0, 2);
            if (ui::IconButton(ICON_FA_BARS, "More actions")) ImGui::OpenPopup("##mmenu");
            if (ImGui::BeginPopup("##mmenu")) {
                if (ImGui::MenuItem(ICON_FA_TAG "  List machine / set prices...")) open(Dialog::List, m);
                if (ImGui::MenuItem(ICON_FA_BAN "  Unlist machine...")) open(Dialog::Unlist, m);
                if (ImGui::MenuItem(ICON_FA_DOLLAR_SIGN "  Set min bid...")) open(Dialog::MinBid, m);
                if (ImGui::MenuItem(ICON_FA_CUBE "  Set default job...")) open(Dialog::Defjob, m);
                if (ImGui::MenuItem(ICON_FA_XMARK "  Remove default job")) app.run(cmd::removeDefjob(m.id), {}, "Default job removed");
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_CALENDAR "  Schedule maintenance...")) open(Dialog::Maint, m);
                if (ImGui::MenuItem(ICON_FA_XMARK "  Cancel maintenance")) app.run(cmd::cancelMaint(m.id), {}, "Maintenance cancelled");
                if (ImGui::MenuItem(ICON_FA_BROOM "  Cleanup machine...")) open(Dialog::Cleanup, m);
                if (ImGui::MenuItem(ICON_FA_FLAG "  Show reports")) {
                    open(Dialog::Reports, m);
                    reports_.fetch(app, cmd::machineReports(m.id));
                }
                if (ImGui::MenuItem(ICON_FA_CODE "  View raw JSON")) open(Dialog::Json, m);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    drawDialog(app);
}

void HostingView::drawDialog(App& app) {
    if (dialog_ == Dialog::None) return;
    const char* id = "Machine###machdlg";
    if (openPopup_) {
        ImGui::OpenPopup(id);
        openPopup_ = false;
    }
    const float s = app.dpiScale() * app.settings().uiScale;
    ImGui::SetNextWindowSize(ImVec2(dialog_ == Dialog::Reports || dialog_ == Dialog::Json || dialog_ == Dialog::Maints ? 720 * s : 480 * s, 0),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool closedNow = false;
    if (!ui::BeginDialog(id, 0, &closedNow)) {
        if (closedNow) dialog_ = Dialog::None;
        return;
    }
    const Machine& m = target_;
    auto reload = [this](const ApiResult&) {
        machines_.attemptedAt = 0;
        machines_.loaded = false;
    };
    bool close = false;
    ImGui::PushItemWidth(ImGui::GetFontSize() * 12);
    switch (dialog_) {
        case Dialog::List: {
            ui::SectionTitle(ICON_FA_TAG, std::format("List machine {}", m.id).c_str());
            ImGui::Separator();
            ImGui::InputDouble("GPU price ($/GPU/hr)", &priceGpu_, 0.01, 0.1, "%.4f");
            ImGui::InputDouble("Min bid ($/GPU/hr)", &minBid_, 0.01, 0.1, "%.4f");
            ImGui::InputDouble("Storage ($/GB/month)", &priceDisk_, 0.01, 0.1, "%.3f");
            ImGui::InputDouble("Upload ($/TB)", &priceUp_, 0.5, 5, "%.2f");
            ImGui::InputDouble("Download ($/TB)", &priceDown_, 0.5, 5, "%.2f");
            ImGui::InputDouble("Max reserved discount (0-1)", &discount_, 0.05, 0.1, "%.2f");
            ImGui::InputInt("Min GPU chunk", &minChunk_);
            ImGui::InputTextWithHint("Offer end date", "YYYY-MM-DD", &endDate_);
            cmd::ListMachineParams p;
            p.id = m.id;
            p.priceGpu = priceGpu_;
            p.priceMinBid = minBid_;
            p.priceDisk = priceDisk_;
            p.priceInetUp = priceUp_ / 1000.0;
            p.priceInetDown = priceDown_ / 1000.0;
            p.discountRate = discount_;
            p.minChunk = minChunk_;
            p.endDate = endDate_;
            const ApiCall call = cmd::listMachine(p);
            if (ImGui::Button(ICON_FA_TAG " List")) {
                app.run(call, reload, "Machine listed");
                close = true;
            }
            break;
        }
        case Dialog::Unlist:
            ImGui::TextWrapped("Unlist machine %lld? Existing rentals continue, no new rentals are accepted.",
                               static_cast<long long>(m.id));
            if (ui::DangerButton(ICON_FA_BAN " Unlist")) {
                app.run(cmd::unlistMachine(m.id), reload, "Machine unlisted");
                close = true;
            }
            break;
        case Dialog::MinBid:
            ui::SectionTitle(ICON_FA_DOLLAR_SIGN, std::format("Min bid for machine {}", m.id).c_str());
            ImGui::InputDouble("$/GPU/hr", &minBid_, 0.01, 0.1, "%.4f");
            if (ImGui::Button("Set min bid")) {
                app.run(cmd::setMinBid(m.id, minBid_), reload, "Min bid updated");
                close = true;
            }
            break;
        case Dialog::Defjob:
            ui::SectionTitle(ICON_FA_CUBE, std::format("Default (background) job for machine {}", m.id).c_str());
            ImGui::InputDouble("GPU price ($/hr)", &priceGpu_, 0.01, 0.1, "%.4f");
            ImGui::InputDouble("Upload ($/TB)", &priceUp_, 0.5, 5, "%.2f");
            ImGui::InputDouble("Download ($/TB)", &priceDown_, 0.5, 5, "%.2f");
            ImGui::InputText("Image", &image_);
            ImGui::InputText("Args", &args_);
            if (ImGui::Button("Set default job")) {
                app.run(cmd::setDefjob(m.id, priceGpu_, priceUp_ / 1000.0, priceDown_ / 1000.0, image_, args_), reload,
                        "Default job set");
                close = true;
            }
            break;
        case Dialog::Maint: {
            ui::SectionTitle(ICON_FA_CALENDAR, std::format("Schedule maintenance for machine {}", m.id).c_str());
            ImGui::InputTextWithHint("Date (UTC)", "YYYY-MM-DD", &maintDate_);
            ImGui::InputTextWithHint("Time (UTC)", "HH:MM", &maintTime_);
            ImGui::InputDouble("Duration (hours)", &maintHours_, 0.5, 1, "%.1f");
            ImGui::Combo("Category", &maintCategory_, kMaintCategories, IM_ARRAYSIZE(kMaintCategories));
            int hh = 0, mm = 0;
            std::sscanf(maintTime_.c_str(), "%d:%d", &hh, &mm);
            const auto day = fmt::epochFromDate(maintDate_);
            ImGui::BeginDisabled(!day);
            if (ImGui::Button(ICON_FA_CALENDAR " Schedule") && day) {
                const double start = *day + hh * 3600.0 + mm * 60.0;
                app.run(cmd::scheduleMaint(m.id, start, maintHours_, kMaintCategories[maintCategory_]), reload,
                        "Maintenance scheduled");
                close = true;
            }
            ImGui::EndDisabled();
            break;
        }
        case Dialog::Cleanup:
            ImGui::TextWrapped("Remove expired/stopped containers and their data from machine %lld?", static_cast<long long>(m.id));
            if (ui::DangerButton(ICON_FA_BROOM " Cleanup")) {
                app.run(cmd::cleanupMachine(m.id), reload, "Cleanup requested");
                close = true;
            }
            break;
        case Dialog::Reports:
        case Dialog::Maints:
            ui::SectionTitle(ICON_FA_FLAG, dialog_ == Dialog::Reports ? std::format("Reports for machine {}", m.id).c_str()
                                                                       : "Maintenance windows");
            ui::InlineError(reports_.error, 60);
            if (reports_.loading) {
                ui::MonoText("##reptext", "loading...", ImVec2(-FLT_MIN, 380 * s));
            } else if (reports_.data.is_string()) {
                ui::MonoText("##reptext", reports_.data.get<std::string>(), ImVec2(-FLT_MIN, 380 * s));
            } else {
                ui::JsonText("##repjson", reports_.data, ImVec2(-FLT_MIN, 380 * s));
            }
            break;
        case Dialog::Json:
            ui::JsonText("##mjson", m.raw, ImVec2(-FLT_MIN, 420 * s));
            break;
        case Dialog::None: break;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ui::SecondaryButton("Close") || close) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

}  // namespace vast
