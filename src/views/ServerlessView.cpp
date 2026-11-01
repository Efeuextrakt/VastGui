#include "views/ServerlessView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/Format.hpp"
#include "widgets/Spinner.hpp"

#include <format>

namespace vast {

namespace {
const char* kEpStates[] = {"(unchanged)", "active", "suspended", "stopped"};

void optionalDouble(const char* label, std::optional<double>& v) {
    double d = v.value_or(0.0);
    if (ImGui::InputDouble(label, &d, 0, 0, "%.2f")) v = d > 0 ? std::optional(d) : std::nullopt;
}
}  // namespace

void ServerlessView::draw(App& app) {
    ui::PageHeader(ICON_FA_BOLT, "Serverless", "endpoints, worker groups and deployments");
    if (ImGui::BeginTabBar("##slt")) {
        if (ImGui::BeginTabItem(ICON_FA_ROUTE " Endpoints")) {
            drawEndpoints(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_CUBE " Worker groups")) {
            drawWorkergroups(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_ROCKET " Deployments")) {
            drawDeployments(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (openDelete_) {
        ImGui::OpenPopup("Delete?###sldel");
        openDelete_ = false;
    }
    const char* kinds[] = {"endpoint", "worker group", "deployment"};
    const auto id = jsonutil::optInt(pendingDelete_, "id").value_or(0);
    if (ui::ConfirmModal("Delete?###sldel", std::format("Delete {} {}?", kinds[pendingKind_], id), "Delete")) {
        if (pendingKind_ == 0) {
            app.run(cmd::deleteEndpoint(id), [this, &app](const ApiResult&) { endpoints_.fetch(app, cmd::showEndpoints()); }, "Endpoint deleted");
        } else if (pendingKind_ == 1) {
            app.run(cmd::deleteWorkergroup(id), [this, &app](const ApiResult&) { workergroups_.fetch(app, cmd::showWorkergroups()); },
                    "Worker group deleted");
        } else {
            app.run(cmd::deleteDeployment(id), [this, &app](const ApiResult&) { deployments_.fetch(app, cmd::showDeployments()); },
                    "Deployment deleted");
        }
    }
}

void ServerlessView::openLogs(App& app, bool endpoint, std::int64_t id) {
    for (auto& l : logs_) {
        if (l->endpoint == endpoint && l->id == id) {
            l->open = true;
            return;
        }
    }
    auto w = std::make_unique<LogWindow>();
    w->endpoint = endpoint;
    w->id = id;
    w->res.fetch(app, endpoint ? cmd::endpointLogs(id, w->tail) : cmd::workergroupLogs(id, w->tail));
    logs_.push_back(std::move(w));
}

void ServerlessView::drawEndpoints(App& app) {
    endpoints_.autoFetch(app, cmd::showEndpoints());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", endpoints_.loading)) endpoints_.fetch(app, cmd::showEndpoints());
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_PLUS " New endpoint")) {
        epForm_ = cmd::EndpointParams{};
        epEditId_.reset();
        epState_ = 0;
        openEpForm_ = true;
    }
    ui::InlineError(endpoints_.error);

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "endpoint_name", "endpoint_state", "min_load", "target_util", "cold_mult",
                             "cold_workers", "max_workers", "max_queue_time", "target_queue_time", "inactivity_timeout"};
    opts.rowMenu = [&](const json& row, int) {
        const auto id = jsonutil::optInt(row, "id").value_or(0);
        if (ImGui::MenuItem(ICON_FA_PEN "  Edit...")) {
            epEditId_ = id;
            epForm_.name = jsonutil::str(row, "endpoint_name");
            epForm_.minLoad = jsonutil::num(row, "min_load");
            epForm_.minColdLoad = jsonutil::num(row, "min_cold_load");
            epForm_.targetUtil = jsonutil::num(row, "target_util", 0.9);
            epForm_.coldMult = jsonutil::num(row, "cold_mult", 2.5);
            epForm_.coldWorkers = static_cast<int>(jsonutil::num(row, "cold_workers", 5));
            epForm_.maxWorkers = static_cast<int>(jsonutil::num(row, "max_workers", 20));
            epForm_.maxQueueTime = jsonutil::optDouble(row, "max_queue_time");
            epForm_.targetQueueTime = jsonutil::optDouble(row, "target_queue_time");
            epForm_.inactivityTimeout = jsonutil::optDouble(row, "inactivity_timeout");
            epState_ = 0;
            openEpForm_ = true;
        }
        if (ImGui::MenuItem(ICON_FA_FILE_LINES "  Logs")) openLogs(app, true, id);
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingDelete_ = row;
            pendingKind_ = 0;
            openDelete_ = true;
        }
    };
    ui::JsonTable("##endpoints", jsonutil::extractArray(endpoints_.data, {"results"}), epTable_, opts);

    if (openEpForm_) {
        ImGui::OpenPopup("Endpoint###epform");
        openEpForm_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Endpoint###epform", ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SectionTitle(ICON_FA_ROUTE, epEditId_ ? std::format("Update endpoint {}", *epEditId_).c_str() : "Create endpoint");
        ImGui::Separator();
        ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
        ImGui::InputText("Endpoint name", &epForm_.name);
        ImGui::InputDouble("Min load", &epForm_.minLoad, 0, 0, "%.2f");
        ImGui::InputDouble("Min cold load", &epForm_.minColdLoad, 0, 0, "%.2f");
        ImGui::InputDouble("Target utilization", &epForm_.targetUtil, 0.05, 0.1, "%.2f");
        ImGui::InputDouble("Cold multiplier", &epForm_.coldMult, 0.1, 0.5, "%.2f");
        ImGui::InputInt("Cold workers", &epForm_.coldWorkers);
        ImGui::InputInt("Max workers", &epForm_.maxWorkers);
        optionalDouble("Max queue time (s)", epForm_.maxQueueTime);
        optionalDouble("Target queue time (s)", epForm_.targetQueueTime);
        optionalDouble("Inactivity timeout (s)", epForm_.inactivityTimeout);
        if (epEditId_) ImGui::Combo("State", &epState_, kEpStates, 4);
        ImGui::PopItemWidth();
        epForm_.state = epState_ > 0 ? kEpStates[epState_] : "";
        const ApiCall call = epEditId_ ? cmd::updateEndpoint(*epEditId_, epForm_) : cmd::createEndpoint(epForm_);
        if (ImGui::Button(epEditId_ ? "Update" : "Create")) {
            app.run(call, [this, &app](const ApiResult&) { endpoints_.fetch(app, cmd::showEndpoints()); },
                    epEditId_ ? "Endpoint updated" : "Endpoint created");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ServerlessView::drawWorkergroups(App& app) {
    workergroups_.autoFetch(app, cmd::showWorkergroups());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", workergroups_.loading)) workergroups_.fetch(app, cmd::showWorkergroups());
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_PLUS " New worker group")) {
        wgForm_ = cmd::WorkergroupParams{};
        wgForm_.templateHash = app.settings().currentTemplate.hashId;
        wgEditId_.reset();
        wgEndpointId_ = 0;
        wgTemplateId_ = 0;
        wgGpuRam_ = 0;
        openWgForm_ = true;
    }
    ui::InlineError(workergroups_.error);

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "endpoint_name", "endpoint_id", "template_hash", "template_id", "gpu_ram",
                             "search_params", "launch_args", "min_load", "target_util"};
    opts.rowMenu = [&](const json& row, int) {
        const auto id = jsonutil::optInt(row, "id").value_or(0);
        if (ImGui::MenuItem(ICON_FA_PEN "  Edit...")) {
            wgEditId_ = id;
            wgForm_ = cmd::WorkergroupParams{};
            wgForm_.templateHash = jsonutil::str(row, "template_hash");
            wgForm_.searchParams = jsonutil::str(row, "search_params");
            wgForm_.launchArgs = jsonutil::str(row, "launch_args");
            wgForm_.endpointName = jsonutil::str(row, "endpoint_name");
            wgForm_.noDefault = true;
            wgEndpointId_ = jsonutil::optInt(row, "endpoint_id").value_or(0);
            wgTemplateId_ = jsonutil::optInt(row, "template_id").value_or(0);
            wgGpuRam_ = jsonutil::num(row, "gpu_ram");
            openWgForm_ = true;
        }
        if (ImGui::MenuItem(ICON_FA_FILE_LINES "  Logs")) openLogs(app, false, id);
        if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Rolling update of workers")) {
            app.run(cmd::updateWorkers(id), {}, "Worker update started");
        }
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingDelete_ = row;
            pendingKind_ = 1;
            openDelete_ = true;
        }
    };
    ui::JsonTable("##workergroups", jsonutil::extractArray(workergroups_.data, {"results"}), wgTable_, opts);

    if (openWgForm_) {
        ImGui::OpenPopup("Worker group###wgform");
        openWgForm_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Worker group###wgform", ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SectionTitle(ICON_FA_CUBE, wgEditId_ ? std::format("Update worker group {}", *wgEditId_).c_str()
                                                 : "Create worker group");
        ImGui::Separator();
        ImGui::PushItemWidth(ImGui::GetFontSize() * 18);
        ImGui::InputText("Template hash", &wgForm_.templateHash);
        ImGui::InputScalar("Template ID (optional)", ImGuiDataType_S64, &wgTemplateId_);
        ImGui::InputText("Endpoint name", &wgForm_.endpointName);
        ImGui::InputScalar("Endpoint ID (optional)", ImGuiDataType_S64, &wgEndpointId_);
        ImGui::InputTextWithHint("Search params", "gpu_ram>=24 num_gpus=1", &wgForm_.searchParams);
        ImGui::InputText("Launch args", &wgForm_.launchArgs);
        ImGui::InputDouble("GPU RAM estimate (GB)", &wgGpuRam_, 0, 0, "%.0f");
        ImGui::Checkbox("No default search filters (-n)", &wgForm_.noDefault);
        ImGui::PopItemWidth();
        wgForm_.templateId = wgTemplateId_ > 0 ? std::optional(wgTemplateId_) : std::nullopt;
        wgForm_.endpointId = wgEndpointId_ > 0 ? std::optional(wgEndpointId_) : std::nullopt;
        wgForm_.gpuRam = wgGpuRam_ > 0 ? std::optional(wgGpuRam_) : std::nullopt;
        const ApiCall call = wgEditId_ ? cmd::updateWorkergroup(*wgEditId_, wgForm_) : cmd::createWorkergroup(wgForm_);
        if (ImGui::Button(wgEditId_ ? "Update" : "Create")) {
            app.run(call, [this, &app](const ApiResult&) { workergroups_.fetch(app, cmd::showWorkergroups()); },
                    wgEditId_ ? "Worker group updated" : "Worker group created");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ServerlessView::drawDeployments(App& app) {
    deployments_.autoFetch(app, cmd::showDeployments());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", deployments_.loading)) deployments_.fetch(app, cmd::showDeployments());
    ui::InlineError(deployments_.error);
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "name", "tag", "status", "endpoint_id", "created_at"};
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingDelete_ = row;
            pendingKind_ = 2;
            openDelete_ = true;
        }
    };
    ui::JsonTable("##deployments", jsonutil::extractArray(deployments_.data, {"deployments", "results"}), depTable_, opts);
}

void ServerlessView::drawFloating(App& app) {
    for (auto& lp : logs_) {
        LogWindow& w = *lp;
        if (!w.open) continue;
        const std::string title = std::format("{} {} logs###sllog{}{}", w.endpoint ? "Endpoint" : "Worker group", w.id,
                                              w.endpoint ? "e" : "w", w.id);
        ImGui::SetNextWindowSize(ImVec2(760, 480), ImGuiCond_FirstUseEver);
        const bool logsVisible = ImGui::Begin(title.c_str(), &w.open);
        if (!ImGui::IsWindowDocked() && ui::ClickedOutsideCurrentWindow()) w.open = false;
        if (logsVisible) {
            if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", w.res.loading)) {
                w.res.fetch(app, w.endpoint ? cmd::endpointLogs(w.id, w.tail) : cmd::workergroupLogs(w.id, w.tail));
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
            ImGui::InputInt("Tail", &w.tail, 100, 1000);
            ui::InlineError(w.res.error, 60);
            std::string text;
            if (w.res.data.is_string()) {
                text = w.res.data.get<std::string>();
            } else if (w.res.data.is_object() && w.res.data.contains("logs") && w.res.data["logs"].is_string()) {
                text = w.res.data["logs"].get<std::string>();
            } else if (!w.res.data.is_null()) {
                text = w.res.data.dump(2);
            }
            ui::MonoText("##sltext", text, ImVec2(0, 0), true);
        }
        ImGui::End();
    }
    std::erase_if(logs_, [](const auto& w) { return !w->open && !w->res.loading; });
}

}  // namespace vast
