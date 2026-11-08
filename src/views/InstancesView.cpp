#include "views/InstancesView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "implot.h"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "views/SearchView.hpp"
#include "widgets/Badge.hpp"
#include "widgets/Spinner.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace vast {

namespace {

enum InstCol { ICSel, ICId, ICLabel, ICStatus, ICGpu, ICImage, ICPrice, ICTflopsPerDollar, ICDlpPerDollar, ICReserve,
               ICUptime, ICSsh, ICPorts, ICGpuUtil, ICCpuUtil, ICVram, ICDisk, ICSpend, ICActions, ICCount };
// Saved column visibility of older versions: without the per-dollar columns (16) / without Reserve (18).
constexpr int kInstColCountV1 = ICCount - 3;
constexpr int kInstColCountV2 = ICCount - 1;

// Reserved discount plans offered in the dialog (months), like the web console.
constexpr double kReservePlanMonths[] = {1.0, 3.0, 6.0};

struct InstColumn {
    const char* name;
    float width;
    bool hidden;
};

constexpr InstColumn kInstColumns[ICCount] = {
    {"##sel", 24, false},  {"ID", 70, false},      {"Label", 110, false}, {"Status", 112, false},  {"GPU", 120, false},
    {"Image", 170, false}, {"$/hr", 60, false},    {"TFLOPS/$/hr", 90, false}, {"DLP/$/hr", 80, false},
    {"Reserve", 115, false},
    {"Uptime", 70, false}, {"SSH", 170, false},    {"Ports", 140, true},
    {"GPU util", 80, false}, {"CPU util", 80, false}, {"VRAM", 90, false}, {"Disk", 90, false},
    {"$/hr trend", 90, false}, {"Actions", 205, false},
};

const char* kStatusFilters[] = {"All", "running", "starting", "loading", "scheduling", "stopping", "inactive", "outbid",
                                "offline", "error", "other"};

// vast.ai reports cpu_util as a 0..1 fraction of the allocated cores; tolerate per-core or percent scales too.
std::optional<double> cpuPercent(const Instance& i) {
    if (!i.cpuUtil) return std::nullopt;
    const double v = *i.cpuUtil;
    if (v <= 1.0) return v * 100.0;
    if (const auto cores = jsonutil::optDouble(i.raw, "cpu_cores_effective"); cores && *cores > 1.0 && v <= *cores) {
        return v / *cores * 100.0;
    }
    return std::min(v, 100.0);
}

// Machine details (CPU, RAM, board, PCIe, disk, network, CUDA/driver, ...) of the machine an instance runs on.
void drawMachineTooltip(const Instance& i) {
    using namespace jsonutil;
    const json& r = i.raw;
    if (!ImGui::BeginTooltip()) return;
    ImGui::Text("%dx %s", i.numGpus, i.gpuName.c_str());
    ui::TextDim(std::format("instance #{}  machine {}  host {}", i.id, i.machineId.value_or(0), i.hostId.value_or(0)).c_str());
    ImGui::Separator();
    auto row = [](const char* k, const std::string& v) {
        if (v.empty()) return;
        ui::TextDim(k);
        ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
        ImGui::TextUnformatted(v.c_str());
    };
    const auto gpuRam = optDouble(r, "gpu_ram");
    row("GPU", std::format("{}{}", i.gpuName, gpuRam ? std::format("  {:.0f} GB", *gpuRam / 1024.0) : std::string{}));
    {
        std::string perf;
        if (auto t = optDouble(r, "total_flops")) perf += formatMetric(t, 1) + " TFLOPS";
        if (auto d = optDouble(r, "dlperf")) perf += (perf.empty() ? "" : "   DLPerf ") + formatMetric(d, 1);
        row("Performance", perf);
    }
    {
        std::string cpu = str(r, "cpu_name");
        const auto eff = optDouble(r, "cpu_cores_effective");
        const auto cores = optDouble(r, "cpu_cores");
        if (eff || cores) cpu += std::format("  {}/{} cores", formatMetric(eff, 1), formatMetric(cores, 0));
        row("CPU", cpu);
    }
    if (auto ram = optDouble(r, "cpu_ram")) row("RAM", std::format("{:.0f} GB", *ram / 1024.0));
    row("Board", str(r, "mobo_name"));
    {
        std::string pcie;
        if (auto g = optDouble(r, "pci_gen")) pcie = "PCIe " + formatMetric(g, 1);
        if (auto l = optDouble(r, "gpu_lanes")) pcie += " x" + formatMetric(l, 0);
        if (auto b = optDouble(r, "pcie_bw")) pcie += "  " + formatMetric(b, 1) + " GB/s";
        row("PCIe", pcie);
    }
    {
        std::string disk = str(r, "disk_name");
        if (auto bw = optDouble(r, "disk_bw")) disk += std::format("{}{} MB/s", disk.empty() ? "" : "  ", formatMetric(bw, 0));
        if (i.diskSpace) disk += std::format("  {:.0f} GB", *i.diskSpace);
        row("Disk", disk);
    }
    if (i.inetUp || i.inetDown) {
        row("Network", std::format("up {}  down {} Mbps", formatMetric(i.inetUp, 0), formatMetric(i.inetDown, 0)));
    }
    {
        std::string sw;
        if (auto c = optDouble(r, "cuda_max_good")) sw = "CUDA " + formatMetric(c, 1);
        if (const std::string d = str(r, "driver_version"); !d.empty()) sw += (sw.empty() ? "" : "  ") + ("driver " + d);
        if (const std::string os = str(r, "os_version"); !os.empty()) sw += (sw.empty() ? "" : "  ") + ("Ubuntu " + os);
        row("Software", sw);
    }
    {
        auto rel = optDouble(r, "reliability2");
        if (!rel) rel = optDouble(r, "reliability");
        row("Reliability", rel ? fmt::percent(rel, 2) : std::string{});
    }
    row("Location", i.geolocation);
    row("Verification", str(r, "verification"));
    ImGui::EndTooltip();
}

// Breakdown of an instance's current cost (what it is billed right now, not its list price).
void drawCostTooltip(const Instance& i, const CurrencyFormat& c) {
    if (!ImGui::BeginTooltip()) return;
    ImGui::TextUnformatted("Current cost");
    ImGui::Separator();
    auto row = [&](const char* k, const std::string& v) {
        ui::TextDim(k);
        ImGui::SameLine(ImGui::GetFontSize() * 10.0f);
        ImGui::TextUnformatted(v.c_str());
    };
    const bool running = i.status() == "running";
    if (i.costGpuHr) row(running ? "GPU" : "GPU (not billed)", fmt::money(*i.costGpuHr, c, 4) + "/hr");
    if (i.costDiskHr) row("Storage", fmt::money(*i.costDiskHr, c, 4) + "/hr");
    if (i.costListHr && i.costNowHr && *i.costListHr - *i.costNowHr > 1e-6) {
        row("Reserved discount", "-" + fmt::money(*i.costListHr - *i.costNowHr, c, 4) + "/hr");
    }
    ImGui::Separator();
    row("Total", fmt::money(i.costNowHr, c, 4) + "/hr");
    if (!running) ui::TextDim("Not running: only the storage is billed.");
    if (i.prepaid()) {
        const double hours = i.costNowHr && *i.costNowHr > 0 ? *i.creditBalance / *i.costNowHr : 0.0;
        ui::TextWithColor(ui::palette().success,
                          std::format("Prepaid: {} credit left{} - not billed to your account", fmt::money(*i.creditBalance, c, 2),
                                      hours > 0 ? std::format(" (~{})", fmt::humanDuration(hours * 3600.0)) : std::string{})
                              .c_str());
    }
    if (i.dphTotal && (!i.costNowHr || std::abs(*i.dphTotal - *i.costNowHr) > 1e-6)) {
        ui::TextDim(std::format("Normal price while running: {}/hr", fmt::money(i.dphTotal, c, 4)).c_str());
    }
    ui::TextDim("Bandwidth is billed separately.");
    ImGui::EndTooltip();
}

// Reserve plans (kReservePlanMonths) the instance can buy: the host must offer a discount and the plan must end
// before the offer's end date (unknown end date: all plans).
std::vector<ReservedPlan> reservePlans(const Instance& i, double nowEpoch) {
    std::vector<ReservedPlan> out;
    const double maxD = i.creditDiscountMax.value_or(0.0);
    if (maxD <= 0.0 || !i.dphTotal || *i.dphTotal <= 0.0) return out;
    for (const double months : kReservePlanMonths) {
        if (i.endDate && *i.endDate > 0 && nowEpoch + months * kHoursPerReserveMonth * 3600.0 > *i.endDate) continue;
        out.push_back(reservedPlanForMonths(*i.dphTotal, maxD, months));
    }
    return out;
}

struct ReserveBadge {
    std::string text;  // empty: no badge
    std::string tooltip;
    bool active = false;     // a reserved discount is in effect
    bool clickable = false;  // a plan can be bought / extended
    double sortValue = 0;
};

ReserveBadge reserveBadge(const Instance& i, double nowEpoch) {
    ReserveBadge b;
    const auto plans = reservePlans(i, nowEpoch);
    const double active = i.creditDiscount.value_or(0.0);
    if (active > 0.0005) {
        b.active = true;
        b.text = std::format("Saved {:.0f}%", active * 100.0);
        b.tooltip = std::format("Reserved: {:.1f}% discount, ${:.2f} prepaid credit left.", active * 100.0,
                                i.creditBalance.value_or(0.0));
        b.clickable = !plans.empty();
        if (b.clickable) b.tooltip += "\nClick to extend the reservation.";
        b.sortValue = active;
    } else if (!plans.empty()) {
        const ReservedPlan& best = plans.back();
        b.text = std::format("Save {:.0f}%", best.discount * 100.0);
        b.tooltip = std::format("Reserve this instance for up to {:.0f} months and save up to {:.0f}% on the hourly rate.\n"
                                "Click to choose a plan.",
                                best.months, best.discount * 100.0);
        b.clickable = true;
        b.sortValue = best.discount;
    } else {
        b.tooltip = i.creditDiscountMax.value_or(0.0) > 0.0 ? "No reserve plan ends before the machine's offer end date."
                                                            : "The host offers no reserved discount for this machine.";
    }
    return b;
}

std::string reservePlanLabel(const ReservedPlan& p) {
    return std::format("{:.0f} Month{} Reserve Plan - ${:.2f} (Save {:.0f}%)", p.months, p.months == 1.0 ? "" : "s", p.amount,
                       p.discount * 100.0);
}

void toggleMachineBlacklist(App& app, const Instance& inst) {
    if (!inst.machineId) {
        app.toasts().warning("This instance reports no machine ID");
        return;
    }
    auto& machines = app.settings().blacklist.machines;
    const auto id = *inst.machineId;
    if (machines.erase(id) > 0) {
        app.toasts().info("Machine " + std::to_string(id) + " removed from the blacklist");
    } else {
        machines.insert(id);
        app.toasts().info("Machine " + std::to_string(id) + " blacklisted",
                          "Its offers are hidden in Search (or marked blacklisted). Manage the list in Settings.");
    }
    app.markSettingsDirty();
    app.searchView().onBlacklistChanged(app);
}

void usageBar(std::optional<double> used, std::optional<double> total, const char* unit) {
    if (!used || !total || *total <= 0) {
        ImGui::TextDisabled("%s", kMissing);
        return;
    }
    const float frac = static_cast<float>(std::clamp(*used / *total, 0.0, 1.0));
    const std::string overlay = std::format("{:.1f}/{:.0f}{}", *used, *total, unit);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, frac > 0.9f ? ui::palette().danger : ui::palette().accent);
    ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay.c_str());
    ImGui::PopStyleColor();
}

std::pair<double, std::string> instanceSortKey(const Instance& i, int col, double now, const json& user) {
    switch (col) {
        case ICId: return {static_cast<double>(i.id), {}};
        case ICLabel: return {0, i.label};
        case ICStatus: return {0, i.consoleStatus(now, user).label};
        case ICGpu: return {0, i.gpuName};
        case ICImage: return {0, i.image};
        case ICPrice: return {i.costNowHr.value_or(0), {}};
        case ICTflopsPerDollar: return {safeRatio(i.totalFlops, i.dphTotal).value_or(-1), {}};
        case ICDlpPerDollar: return {safeRatio(i.dlperf, i.dphTotal).value_or(-1), {}};
        case ICReserve: return {reserveBadge(i, now).sortValue, {}};
        case ICUptime: return {i.startDate.value_or(0) > 0 ? now - *i.startDate : 0, {}};
        case ICSsh: return {0, i.sshUrl()};
        case ICPorts: return {0, i.portsSummary()};
        case ICGpuUtil: return {i.gpuUtil.value_or(0), {}};
        case ICCpuUtil: return {cpuPercent(i).value_or(0), {}};
        case ICVram: return {i.vmemUsage.value_or(0), {}};
        case ICDisk: return {i.diskUsage.value_or(0), {}};
        default: return {0, {}};
    }
}

// Card view sort options (id saved in VastGui.json -> table column used for the sort key).
struct CardSort {
    const char* id;
    const char* label;
    int column;
};
constexpr CardSort kCardSorts[] = {{"label", "Name", ICLabel}, {"id", "ID", ICId},          {"status", "Status", ICStatus},
                                   {"gpu", "GPU", ICGpu},       {"price", "$/hr", ICPrice}, {"uptime", "Uptime", ICUptime}};

const CardSort& cardSort(const std::string& id) {
    for (const auto& c : kCardSorts) {
        if (id == c.id) return c;
    }
    return kCardSorts[0];
}

// ImPlot $/hr sparkline of the samples collected since app start.
void spendSparkline(App& app, const Instance& i, const ImVec2& size) {
    const auto* hist = app.costHistory(i.id);
    if (!hist || hist->size() < 2) {
        ImGui::TextDisabled("collecting");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("$/hr trend: samples are collected on every refresh");
        return;
    }
    std::vector<double> xs, ys;
    double spent = 0;
    for (std::size_t k = 0; k < hist->size(); ++k) {
        xs.push_back((*hist)[k].first);
        ys.push_back((*hist)[k].second);
        if (k > 0) spent += (*hist)[k - 1].second * ((*hist)[k].first - (*hist)[k - 1].first) / 3600.0;
    }
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    if (ImPlot::BeginPlot("##spark", size, ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit);
        ImPlotSpec spec;
        spec.LineColor = ui::palette().accent;
        spec.LineWeight = 1.5f;
        ImPlot::PlotLine("$/hr", xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("$/hr samples since app start\nEstimated spend in this session: %s",
                          fmt::money(spent, app.settings().currency, 4).c_str());
    }
}

std::string envFromInstance(const json& raw) {
    std::string out;
    if (const json* e = jsonutil::find(raw, "extra_env")) {
        if (e->is_array()) {
            for (const auto& kv : *e) {
                if (kv.is_array() && kv.size() >= 2 && kv[0].is_string()) {
                    out += std::format("-e {}={} ", kv[0].get<std::string>(), jsonutil::scalarToString(kv[1]));
                }
            }
        } else if (e->is_object()) {
            for (const auto& [k, v] : e->items()) out += std::format("-e {}={} ", k, jsonutil::scalarToString(v));
        }
    }
    if (const json* p = jsonutil::find(raw, "ports"); p && p->is_object()) {
        for (const auto& [k, v] : p->items()) {
            const auto slash = k.find('/');
            const std::string port = k.substr(0, slash);
            if (port == "22") continue;
            out += std::format("-p {}:{} ", port, port);
        }
    }
    return fmt::trim(out);
}

}  // namespace

std::vector<std::int64_t> InstancesView::selectedIds() const { return {selected_.begin(), selected_.end()}; }

void InstancesView::openDialog(Dialog d, const Instance& inst) {
    dialog_ = d;
    target_ = inst;
    openPopup_ = true;
    text1_.clear();
    text2_.clear();
    text3_.clear();
    text4_.clear();
    number_ = 0;
    choice_ = 0;
    flag_ = false;
    execOutput_.clear();
    busy_ = false;
    switch (d) {
        case Dialog::Label: text1_ = inst.label; break;
        case Dialog::ChangeBid: number_ = inst.minBid.value_or(inst.dphTotal.value_or(0.1)); break;
        case Dialog::Prepay: {
            const auto plans = reservePlans(inst, fmt::nowEpoch());
            choice_ = 0;  // first plan, or "Custom amount" when no plan fits
            number_ = plans.empty() ? 10.0 : std::round(plans.front().amount * 100.0) / 100.0;
            break;
        }
        case Dialog::Execute: text1_ = "nvidia-smi"; break;
        case Dialog::Copy:
            text1_ = std::format("C.{}:/workspace/", inst.id);
            text2_.clear();  // e.g. another instance C.<id>:/workspace/ or V.<volume>:/data/
            break;
        case Dialog::CloudCopy:
            text1_ = "/workspace/";
            text2_ = "/";
            break;
        case Dialog::CreateTemplate:
            text1_ = inst.label.empty() ? std::format("From instance {}", inst.id) : inst.label;
            text2_ = inst.image;
            text3_ = envFromInstance(inst.raw);
            text4_ = jsonutil::str(inst.raw, "onstart");
            break;
        case Dialog::Update:
            text1_ = inst.image;
            text2_ = envFromInstance(inst.raw);
            text3_ = jsonutil::str(inst.raw, "onstart");
            break;
        case Dialog::Snapshot: text4_ = "docker.io"; break;
        default: break;
    }
}

void InstancesView::openLogs(App& app, std::int64_t id) {
    for (auto& w : logWindows_) {
        if (w->id == id) {
            w->open = true;
            ImGui::SetWindowFocus(std::format("Logs: instance {}###logs{}", id, id).c_str());
            return;
        }
    }
    auto w = std::make_unique<LogWindow>();
    w->id = id;
    fetchLogs(app, *w, 0);
    logWindows_.push_back(std::move(w));
}

void InstancesView::fetchLogs(App& app, LogWindow& w, int tab) {
    if (tab < 0 || tab >= static_cast<int>(std::size(w.tabs))) return;
    LogTab& t = w.tabs[tab];
    t.tail = std::clamp(t.tail, 100, 20000);
    ApiCall c = cmd::instanceLogs(w.id, t.tail, t.filter, tab == 1);
    c.quiet = t.res.loaded;  // background refreshes don't flood the console
    t.res.fetch(app, std::move(c));
}

void InstancesView::draw(App& app) {
    drawToolbar(app);
    if (app.settings().instanceCardView) {
        drawCards(app);
    } else {
        drawTable(app);
    }
    drawDialogs(app);
}

void InstancesView::drawToolbar(App& app) {
    const auto& list = app.instances();
    int running = 0;
    int outbid = 0;
    const double now = fmt::nowEpoch();
    for (const auto& i : list) {
        if (i.consoleStatus(now, app.user()).outbid) ++outbid;
        if (i.status() == "running") ++running;
    }
    // Prepaid (reserved) instances are paid from their own credit, so they are not part of the hourly total.
    const App::AccountCost cost = app.accountCost();
    const CurrencyFormat& cur = app.settings().currency;
    const std::string outbidText = outbid > 0 ? std::format(", {} outbid", outbid) : std::string{};
    const std::string prepaidText =
        cost.prepaidCount > 0 ? std::format(" (+ {} prepaid: {}/hr)", cost.prepaidCount, fmt::money(cost.prepaidRate, cur)) : std::string{};
    ui::PageHeader(ICON_FA_SERVER, "Instances",
                   std::format("{} total, {} running{}, {}/hr{}", list.size(), running, outbidText, fmt::money(cost.rate, cur),
                               prepaidText)
                       .c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s/hr is billed to your account now (storage of stopped instances included).%s",
                          fmt::money(cost.rate, cur).c_str(),
                          cost.prepaidCount > 0 ? "\nPrepaid (reserved) instances are paid from their own prepaid credit and are "
                                                  "not included; when it runs out they are billed at the normal price."
                                                : "");
    }
    if (app.apiKey().key.empty()) {
        ui::TextWithColor(ui::palette().warning, ICON_FA_KEY "  Set an API key in Settings to manage instances.");
    }

    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", app.instancesLoading())) app.refreshInstances();
    ImGui::SameLine();
    const bool any = !selected_.empty();
    ImGui::BeginDisabled(!any);
    if (ui::SecondaryButton(ICON_FA_PLAY " Start")) app.run(cmd::startInstances(selectedIds()), [&app](const ApiResult&) { app.refreshInstances(); }, "Start requested");
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_STOP " Stop")) app.run(cmd::stopInstances(selectedIds()), [&app](const ApiResult&) { app.refreshInstances(); }, "Stop requested");
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ROTATE " Reboot")) {
        for (auto id : selectedIds()) app.run(cmd::rebootInstance(id), [&app](const ApiResult&) { app.refreshInstances(); }, "Reboot requested");
    }
    ImGui::SameLine();
    if (ui::DangerButton(ICON_FA_TRASH " Destroy")) {
        openDialog(Dialog::Destroy, Instance{});
        bulkIds_ = selectedIds();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ui::TextDim(std::format("{} selected", selected_.size()).c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##instfilter", ICON_FA_MAGNIFYING_GLASS " Filter", &filter_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::Combo("##statusfilter", &statusFilter_, kStatusFilters, IM_ARRAYSIZE(kStatusFilters));
    // Right-aligned: sort (card view) + card / table toggle.
    Settings& st = app.settings();
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* sortIcon = st.instanceSortDesc ? ICON_FA_ARROW_DOWN_WIDE_SHORT : ICON_FA_ARROW_UP_WIDE_SHORT;
    const float comboW = ImGui::GetFontSize() * 7.5f;
    const float toggleW = ImGui::CalcTextSize(ICON_FA_GRIP).x + ImGui::CalcTextSize(ICON_FA_TABLE_LIST).x + style.FramePadding.x * 4.0f;
    const float sortW = st.instanceCardView
                            ? comboW + ImGui::CalcTextSize(sortIcon).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x * 2.5f
                            : 0.0f;
    const float rightX = ImGui::GetContentRegionMax().x - toggleW - sortW;
    if (!app.instancesError().empty()) {
        // Same line as the toolbar so a failing refresh never pushes the list down.
        ImGui::SameLine();
        const float maxW = rightX - ImGui::GetCursorPosX() - style.ItemSpacing.x;
        std::string e = ICON_FA_CIRCLE_XMARK " " + app.instancesError();
        if (const auto nl = e.find('\n'); nl != std::string::npos) e.resize(nl);
        e = ui::Ellipsize(e, maxW);
        ImGui::AlignTextToFramePadding();
        ui::TextWithColor(ui::palette().danger, e.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.instancesError().c_str());
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightX));
    if (st.instanceCardView) {
        const CardSort& cur = cardSort(st.instanceSort);
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##instsort", (std::string(ICON_FA_ARROW_DOWN_WIDE_SHORT "  ") + cur.label).c_str())) {
            for (const auto& c : kCardSorts) {
                if (ImGui::Selectable(c.label, &c == &cur)) {
                    st.instanceSort = c.id;
                    app.markSettingsDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sort the instance cards");
        ImGui::SameLine(0, style.ItemSpacing.x * 0.5f);
        if (ui::IconButton(sortIcon, st.instanceSortDesc ? "Descending (click for ascending)" : "Ascending (click for descending)")) {
            st.instanceSortDesc = !st.instanceSortDesc;
            app.markSettingsDirty();
        }
        ImGui::SameLine();
    }
    const ImVec4 on = ui::palette().accent;
    if (ui::IconButton(ICON_FA_GRIP, "Card view", st.instanceCardView ? &on : nullptr)) {
        st.instanceCardView = true;
        app.markSettingsDirty();
    }
    ImGui::SameLine(0, 0);
    if (ui::IconButton(ICON_FA_TABLE_LIST, "Table view", !st.instanceCardView ? &on : nullptr)) {
        st.instanceCardView = false;
        app.markSettingsDirty();
    }
}

void InstancesView::drawRowMenu(App& app, const Instance& inst) {
    auto refresh = [&app](const ApiResult&) { app.refreshInstances(); };
    const std::vector<std::int64_t> one{inst.id};
    if (ImGui::MenuItem(ICON_FA_PLAY "  Start")) app.run(cmd::startInstances(one), refresh, "Start requested");
    if (ImGui::MenuItem(ICON_FA_STOP "  Stop")) app.run(cmd::stopInstances(one), refresh, "Stop requested");
    if (ImGui::MenuItem(ICON_FA_ROTATE "  Reboot")) app.run(cmd::rebootInstance(inst.id), refresh, "Reboot requested");
    if (ImGui::MenuItem(ICON_FA_RECYCLE "  Recycle (destroy + recreate)...")) openDialog(Dialog::Recycle, inst);
    if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Update from template...")) openDialog(Dialog::Update, inst);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_TAG "  Label...")) openDialog(Dialog::Label, inst);
    if (ImGui::MenuItem(ICON_FA_FILE_LINES "  Show logs")) openLogs(app, inst.id);
    if (ImGui::MenuItem(ICON_FA_TERMINAL "  Execute command...")) openDialog(Dialog::Execute, inst);
    ImGui::Separator();
    const std::string ssh = inst.sshCommand();
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy SSH command", nullptr, false, !ssh.empty())) {
        ImGui::SetClipboardText(ssh.c_str());
        app.toasts().info("SSH command copied", ssh);
    }
    const std::string scp = inst.scpUrl();
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy SCP URL", nullptr, false, !scp.empty())) {
        ImGui::SetClipboardText(scp.c_str());
        app.toasts().info("SCP URL copied", scp);
    }
    const auto jup = inst.jupyterUrl();
    if (ImGui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Open Jupyter in browser", nullptr, false, jup.has_value())) {
        platform::openUrl(*jup);
    }
    if (ImGui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Open in vast.ai console")) {
        platform::openUrl("https://cloud.vast.ai/instances/");
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_RIGHT_LEFT "  Copy files...")) openDialog(Dialog::Copy, inst);
    if (ImGui::MenuItem(ICON_FA_CLOUD "  Cloud copy...")) openDialog(Dialog::CloudCopy, inst);
    if (ImGui::MenuItem(ICON_FA_CAMERA "  Take snapshot...")) openDialog(Dialog::Snapshot, inst);
    if (ImGui::MenuItem(ICON_FA_KEY "  Attach SSH key...")) openDialog(Dialog::AttachSsh, inst);
    if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Create template from instance...")) openDialog(Dialog::CreateTemplate, inst);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_DOLLAR_SIGN "  Change bid...", nullptr, false, true)) openDialog(Dialog::ChangeBid, inst);
    if (ImGui::MenuItem(ICON_FA_TAG "  Add a Reserved Discount...")) openDialog(Dialog::Prepay, inst);
    if (ImGui::MenuItem(ICON_FA_WALLET "  Show deposit")) {
        app.run(cmd::showDeposit(inst.id), [&app](const ApiResult& r) {
            app.toasts().info("Deposit", r.data.is_null() ? r.text : r.data.dump(1));
        });
    }
    if (ImGui::MenuItem(ICON_FA_CODE "  View raw JSON")) openDialog(Dialog::Json, inst);
    ImGui::Separator();
    const bool listed = inst.machineId && app.settings().blacklist.machines.contains(*inst.machineId);
    const std::string blLabel = listed ? std::format(ICON_FA_BAN "  Remove machine {} from blacklist", *inst.machineId)
                                       : std::format(ICON_FA_BAN "  Blacklist machine {}", inst.machineId.value_or(0));
    if (ImGui::MenuItem(blLabel.c_str(), nullptr, false, inst.machineId.has_value())) toggleMachineBlacklist(app, inst);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_TRASH "  Destroy...")) {
        openDialog(Dialog::Destroy, inst);
        bulkIds_ = {inst.id};
    }
}

std::vector<const Instance*> InstancesView::visibleInstances(App& app) {
    const auto& list = app.instances();
    const double now = fmt::nowEpoch();
    std::vector<const Instance*> rows;
    for (const auto& i : list) {
        const std::string status = i.consoleStatus(now, app.user()).label;
        if (statusFilter_ > 0) {
            const std::string want = kStatusFilters[statusFilter_];
            const bool known = std::any_of(std::begin(kStatusFilters) + 1, std::end(kStatusFilters) - 1,
                                           [&](const char* f) { return status == f; });
            if (want == "other" ? known : status != want) continue;
        }
        if (!filter_.empty() && !fmt::containsCi(i.raw.dump(), filter_)) continue;
        rows.push_back(&i);
    }
    // Drop selections that no longer exist.
    std::erase_if(selected_, [&](std::int64_t id) {
        return std::none_of(list.begin(), list.end(), [&](const Instance& i) { return i.id == id; });
    });
    return rows;
}

bool InstancesView::drawEmpty(App& app) {
    if (!app.instances().empty()) return false;
    ImGui::Spacing();
    ui::TextDim(app.instancesLoading() ? "Loading instances..." : "No instances. Rent one from the Search page.");
    if (!app.instancesLoading() && ImGui::Button(ICON_FA_MAGNIFYING_GLASS "  Go to Search")) app.navigate(Page::Search);
    return true;
}

void InstancesView::drawStatusBadge(App& app, const Instance& i, double now) {
    const InstanceStatus cs = i.consoleStatus(now, app.user());
    std::string tip = cs.help;
    if (!i.statusMsg.empty()) tip += (tip.empty() ? "" : "\n\n") + i.statusMsg;
    if (cs.outbid) {
        const CurrencyFormat& cur = app.settings().currency;
        tip += std::format("\n\nYour bid: {}/hr, current minimum bid: {}/hr.\nClick to change the bid.",
                           fmt::money(i.dphTotal, cur), fmt::money(i.minBid, cur));
        if (ui::Badge("outbid", ui::palette().danger, true, tip.c_str())) openDialog(Dialog::ChangeBid, i);
    } else {
        ui::Badge(cs.label.c_str(), ui::statusColor(cs.label), false, tip.empty() ? nullptr : tip.c_str());
    }
}

void InstancesView::drawReserveBadge(const Instance& i, double now) {
    const ui::Palette& p = ui::palette();
    const ReserveBadge b = reserveBadge(i, now);
    if (!b.text.empty()) {
        if (ui::Badge(b.text.c_str(), b.active ? p.success : p.accent, false, b.tooltip.c_str()) && b.clickable) {
            openDialog(Dialog::Prepay, i);
        }
    } else {
        ImGui::TextDisabled("%s", kMissing);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.tooltip.c_str());
    }
}

void InstancesView::drawActions(App& app, const Instance& i) {
    const bool isRunning = i.status() == "running" || i.status() == "loading";
    if (isRunning) {
        if (ui::IconButton(ICON_FA_STOP, "Stop")) {
            app.run(cmd::stopInstances({i.id}), [&app](const ApiResult&) { app.refreshInstances(); }, "Stop requested");
        }
    } else if (ui::IconButton(ICON_FA_PLAY, "Start")) {
        app.run(cmd::startInstances({i.id}), [&app](const ApiResult&) { app.refreshInstances(); }, "Start requested");
    }
    ImGui::SameLine(0, 2);
    if (ui::IconButton(ICON_FA_FILE_LINES, "Logs")) openLogs(app, i.id);
    ImGui::SameLine(0, 2);
    if (ui::IconButton(ICON_FA_TERMINAL, "Copy SSH command")) {
        const std::string ssh = i.sshCommand();
        if (!ssh.empty()) {
            ImGui::SetClipboardText(ssh.c_str());
            app.toasts().info("SSH command copied", ssh);
        }
    }
    ImGui::SameLine(0, 2);
    {
        // Always rendered (disabled when unavailable) so the action icons line up across rows.
        const auto jup = i.jupyterUrl();
        ImGui::BeginDisabled(!jup.has_value());
        if (ui::IconButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE, "Open Jupyter") && jup) platform::openUrl(*jup);
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, 2);
    {
        const bool listed = i.machineId && app.settings().blacklist.machines.contains(*i.machineId);
        const ImVec4 danger = ui::palette().danger;
        const std::string tip = !i.machineId ? std::string("No machine ID")
                                : listed ? std::format("Machine {} is blacklisted - click to remove", *i.machineId)
                                         : std::format("Blacklist machine {} (hide its offers in Search)", *i.machineId);
        ImGui::BeginDisabled(!i.machineId.has_value());
        if (ui::IconButton(ICON_FA_BAN, tip.c_str(), listed ? &danger : nullptr)) toggleMachineBlacklist(app, i);
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, 2);
    if (ui::IconButton(ICON_FA_BARS, "More actions")) ImGui::OpenPopup("##instmenu");
    if (ImGui::BeginPopup("##instmenu")) {
        drawRowMenu(app, i);
        ImGui::EndPopup();
    }
}

void InstancesView::drawCards(App& app) {
    const Settings& st = app.settings();
    const double now = fmt::nowEpoch();
    std::vector<const Instance*> rows = visibleInstances(app);
    if (drawEmpty(app)) return;

    const CardSort& sort = cardSort(st.instanceSort);
    const bool desc = st.instanceSortDesc;
    const json& user = app.user();
    std::stable_sort(rows.begin(), rows.end(), [&](const Instance* a, const Instance* b) {
        if (sort.column == ICLabel && a->label.empty() != b->label.empty()) return b->label.empty();  // unnamed last
        auto ka = instanceSortKey(*a, sort.column, now, user);
        auto kb = instanceSortKey(*b, sort.column, now, user);
        ka.second = fmt::toLower(ka.second);
        kb.second = fmt::toLower(kb.second);
        if (ka != kb) return desc ? ka > kb : ka < kb;
        return a->id < b->id;
    });

    // Select-all for the card view (the table has it in its header).
    {
        bool all = !rows.empty() && std::all_of(rows.begin(), rows.end(), [&](const Instance* i) { return selected_.contains(i->id); });
        if (ImGui::Checkbox("Select all", &all)) {
            for (const Instance* i : rows) {
                if (all) {
                    selected_.insert(i->id);
                } else {
                    selected_.erase(i->id);
                }
            }
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::TextDim(std::format("{} shown", rows.size()).c_str());
    }

    ImGui::BeginChild("##instcards", ImVec2(0, 0), ImGuiChildFlags_None);
    float measured = 0.0f;
    const float height = cardHeight_ > 0.0f
                             ? cardHeight_
                             : ImGui::GetTextLineHeightWithSpacing() * 6.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), height + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            drawCard(app, *rows[static_cast<std::size_t>(k)], now, height, &measured);
        }
    }
    ImGui::EndChild();
    // Cards share one height (for clipping): the measured content height of last frame's cards.
    if (measured > 0.0f) cardHeight_ = std::ceil(measured);
}

void InstancesView::drawCard(App& app, const Instance& i, double now, float height, float* measured) {
    const ui::Palette& p = ui::palette();
    const Settings& st = app.settings();
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool selected = selected_.contains(i.id);
    const bool outbid = i.consoleStatus(now, app.user()).outbid;

    ImGui::PushID(static_cast<int>(i.id));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.card);
    const ImVec4 border = selected ? p.info : outbid ? ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.6f) : p.border;
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,
                        selected ? std::max(1.5f, style.ChildBorderSize * 1.5f) : style.ChildBorderSize);
    ImGui::BeginChild("##icard", ImVec2(0, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ui::col(p.accent, 0.05f), 8.0f);
    }

    const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##cols", 4, tf)) {
        ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("img", ImGuiTableColumnFlags_WidthStretch, 1.45f);
        ImGui::TableSetupColumn("use", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_WidthStretch, 1.05f);
        ImGui::TableNextRow();

        // Left: selection, status, name, GPU, machine.
        ImGui::TableSetColumnIndex(0);
        bool sel = selected;
        if (ImGui::Checkbox("##sel", &sel)) {
            if (sel) {
                selected_.insert(i.id);
            } else {
                selected_.erase(i.id);
            }
        }
        ImGui::SameLine();
        drawStatusBadge(app, i, now);
        ImGui::SameLine();
        ui::TextDim(std::format("#{}", i.id).c_str());
        if (ImGui::IsItemHovered()) drawMachineTooltip(i);
        const float colW = ImGui::GetContentRegionAvail().x;
        if (i.label.empty()) {
            ui::TextDim("(no name)");
        } else {
            ImGui::TextUnformatted(ui::Ellipsize(i.label, colW).c_str());
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Name (label) - change it from the menu");
        ImGui::PushFont(nullptr, style.FontSizeBase * 1.45f);
        ImGui::Text("%dx %s", i.numGpus, i.gpuName.c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) drawMachineTooltip(i);
        ui::TextDim(std::format("m:{}  host:{}", i.machineId.value_or(0), i.hostId.value_or(0)).c_str());
        if (ImGui::IsItemHovered()) drawMachineTooltip(i);
        ui::TextDim(ui::Ellipsize(i.geolocation.empty() ? std::string(kMissing) : i.geolocation, colW).c_str());

        // Middle: image, template, uptime, SSH, ports.
        ImGui::TableSetColumnIndex(1);
        const float midW = ImGui::GetContentRegionAvail().x;
        const float textX = ImGui::GetCursorPosX() + ImGui::GetFontSize() * 1.6f;
        auto iconRow = [&](const char* icon, const std::string& text, const char* tip) {
            ui::TextDim(icon);
            ImGui::SameLine();
            ImGui::SetCursorPosX(textX);
            ImGui::TextUnformatted(ui::Ellipsize(text, midW - ImGui::GetFontSize() * 1.6f).c_str());
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };
        iconRow(ICON_FA_BOX, i.image.empty() ? std::string(kMissing) : i.image, i.image.c_str());
        iconRow(ICON_FA_LAYER_GROUP, i.templateName.empty() ? std::string("no template") : i.templateName, "Template");
        const auto up = i.uptimeHours(now);
        iconRow(ICON_FA_CLOCK, up ? "up " + fmt::humanDuration(*up * 3600.0) : std::string(kMissing), "Uptime");
        {
            const std::string ssh = i.sshUrl();
            ui::TextDim(ICON_FA_TERMINAL);
            ImGui::SameLine();
            ImGui::SetCursorPosX(textX);
            if (ssh.size() <= 6) {
                ImGui::TextDisabled("%s", kMissing);
            } else {
                ui::CopyIcon("##sshcopy", i.sshCommand());
                ImGui::SameLine();
                ImGui::TextUnformatted(ui::Ellipsize(ssh.substr(6), midW - ImGui::GetFontSize() * 3.4f).c_str());
            }
        }
        const std::string ports = i.portsSummary();
        iconRow(ICON_FA_NETWORK_WIRED, ports.empty() ? std::string("no ports") : ports, ports.empty() ? nullptr : ports.c_str());

        // Usage bars.
        ImGui::TableSetColumnIndex(2);
        const float barX = ImGui::GetCursorPosX() + ImGui::CalcTextSize("VRAM").x + style.ItemSpacing.x;
        auto bar = [&](const char* label, std::optional<double> used, std::optional<double> total, const char* unit) {
            ImGui::AlignTextToFramePadding();
            ui::TextDim(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(barX);
            usageBar(used, total, unit);
        };
        bar("GPU", i.gpuUtil, 100.0, "%");
        bar("CPU", cpuPercent(i), 100.0, "%");
        bar("VRAM", i.vmemUsage, i.gpuRamMb ? std::optional<double>(*i.gpuRamMb / 1024.0) : std::nullopt, "G");
        bar("Disk", i.diskUsage, i.diskSpace, "G");

        // Right: cost, reserve, per-dollar metrics, trend, actions.
        ImGui::TableSetColumnIndex(3);
        ImGui::PushFont(nullptr, style.FontSizeBase * 1.5f);
        ImGui::TextUnformatted((fmt::money(i.costNowHr, st.currency) + "/hr").c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) drawCostTooltip(i, st.currency);
        ImGui::SameLine();
        drawReserveBadge(i, now);
        ui::TextDim(std::format("TFLOPS/$/hr {}   DLP/$/hr {}", formatMetric(safeRatio(i.totalFlops, i.dphTotal), 1),
                                formatMetric(safeRatio(i.dlperf, i.dphTotal), 1))
                        .c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Per $/hr of the normal running price - higher is better");
        spendSparkline(app, i, ImVec2(-FLT_MIN, ImGui::GetFrameHeight()));
        drawActions(app, i);
        ImGui::EndTable();
    }
    if (measured) {
        // Content height in this child (cursor started at WindowPadding.y) plus the bottom padding.
        const float h = ImGui::GetCursorPosY() - style.ItemSpacing.y + style.WindowPadding.y;
        *measured = std::max(*measured, h);
    }
    if (ImGui::BeginPopupContextWindow("##instcardctx")) {
        drawRowMenu(app, i);
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        openDialog(Dialog::Json, i);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void InstancesView::drawTable(App& app) {
    Settings& st = app.settings();
    const double now = fmt::nowEpoch();
    const float s = app.dpiScale() * st.uiScale;

    std::vector<const Instance*> rows = visibleInstances(app);
    if (drawEmpty(app)) return;

    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    // New table id whenever saved widths/order must not apply: imgui.ini stores them per column index (V4: wider
    // Status column for the console status labels such as "scheduling").
    if (!ImGui::BeginTable("##instancesV4", ICCount, flags)) return;
    ImGui::TableSetupScrollFreeze(2, 1);
    for (int c = 0; c < ICCount; ++c) {
        ImGuiTableColumnFlags cf = ImGuiTableColumnFlags_WidthFixed;
        if (kInstColumns[c].hidden) cf |= ImGuiTableColumnFlags_DefaultHide;
        if (c == ICSel || c == ICActions || c == ICSpend) cf |= ImGuiTableColumnFlags_NoSort;
        if (c == ICSel || c == ICActions) cf |= ImGuiTableColumnFlags_NoHide;
        ImGui::TableSetupColumn(kInstColumns[c].name, cf, kInstColumns[c].width * s);
    }
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < ICCount; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        if (c == ICSel) {
            bool all = !rows.empty() && std::all_of(rows.begin(), rows.end(), [&](const Instance* i) { return selected_.contains(i->id); });
            if (ImGui::Checkbox("##all", &all)) {
                for (const Instance* i : rows) {
                    if (all) {
                        selected_.insert(i->id);
                    } else {
                        selected_.erase(i->id);
                    }
                }
            }
        } else {
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
        }
    }

    if (st.instanceColumns.size() == kInstColCountV1) {
        // Keep the user's choices: insert the newer columns (visible) at their positions.
        st.instanceColumns.insert(st.instanceColumns.begin() + ICTflopsPerDollar, {true, true});
        app.markSettingsDirty();
    }
    if (st.instanceColumns.size() == kInstColCountV2) {
        st.instanceColumns.insert(st.instanceColumns.begin() + ICReserve, true);
        app.markSettingsDirty();
    }
    if (st.instanceColumns.size() != ICCount) {
        st.instanceColumns.assign(ICCount, true);
        for (int c = 0; c < ICCount; ++c) st.instanceColumns[static_cast<std::size_t>(c)] = !kInstColumns[c].hidden;
    }
    if (!tableColumnsApplied_) {
        for (int c = 0; c < ICCount; ++c) ImGui::TableSetColumnEnabled(c, st.instanceColumns[static_cast<std::size_t>(c)]);
        tableColumnsApplied_ = true;
    } else {
        for (int c = 0; c < ICCount; ++c) {
            const bool en = (ImGui::TableGetColumnFlags(c) & ImGuiTableColumnFlags_IsEnabled) != 0;
            if (st.instanceColumns[static_cast<std::size_t>(c)] != en) {
                st.instanceColumns[static_cast<std::size_t>(c)] = en;
                app.markSettingsDirty();
            }
        }
    }

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        const int c = specs->Specs[0].ColumnIndex;
        const bool desc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        std::stable_sort(rows.begin(), rows.end(), [&](const Instance* a, const Instance* b) {
            const auto ka = instanceSortKey(*a, c, now, app.user());
            const auto kb = instanceSortKey(*b, c, now, app.user());
            return desc ? ka > kb : ka < kb;
        });
    }

    for (const Instance* ip : rows) {
        const Instance& i = *ip;
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i.id));
        for (int c = 0; c < ICCount; ++c) {
            if (!ImGui::TableSetColumnIndex(c)) continue;
            switch (c) {
                case ICSel: {
                    bool sel = selected_.contains(i.id);
                    if (ImGui::Checkbox("##sel", &sel)) {
                        if (sel) {
                            selected_.insert(i.id);
                        } else {
                            selected_.erase(i.id);
                        }
                    }
                    break;
                }
                case ICId: {
                    const std::string idText = std::to_string(i.id);
                    const ImVec2 idPos = ImGui::GetCursorScreenPos();
                    ui::TableRowSelectable(idText.c_str(), selected_.contains(i.id));
                    if (ImGui::BeginPopupContextItem("##instctx")) {
                        drawRowMenu(app, i);
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDialog(Dialog::Json, i);
                    // The selectable spans the whole row: only the ID text itself shows the machine tooltip.
                    const ImVec2 idSize = ImGui::CalcTextSize(idText.c_str());
                    if (ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(idPos, ImVec2(idPos.x + idSize.x, idPos.y + idSize.y))) {
                        drawMachineTooltip(i);
                    }
                    break;
                }
                case ICLabel: ImGui::TextUnformatted(i.label.empty() ? "-" : i.label.c_str()); break;
                case ICStatus: drawStatusBadge(app, i, now); break;
                case ICGpu:
                    ImGui::Text("%dx %s", i.numGpus, i.gpuName.c_str());
                    if (ImGui::IsItemHovered()) drawMachineTooltip(i);
                    break;
                case ICImage:
                    ImGui::TextUnformatted(i.image.c_str());
                    if (ImGui::IsItemHovered() && !i.templateName.empty()) ImGui::SetTooltip("Template: %s", i.templateName.c_str());
                    break;
                case ICPrice:
                    ImGui::TextUnformatted(fmt::money(i.costNowHr, st.currency).c_str());
                    if (ImGui::IsItemHovered()) drawCostTooltip(i, st.currency);
                    break;
                case ICTflopsPerDollar:
                case ICDlpPerDollar: {
                    const bool tf = c == ICTflopsPerDollar;
                    ImGui::TextUnformatted(formatMetric(safeRatio(tf ? i.totalFlops : i.dlperf, i.dphTotal), 1).c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(tf ? "total_flops / dph_total - higher is better" : "dlperf / dph_total - higher is better");
                    }
                    break;
                }
                case ICReserve: drawReserveBadge(i, now); break;
                case ICUptime: {
                    const auto h = i.uptimeHours(now);
                    ImGui::TextUnformatted(h ? fmt::humanDuration(*h * 3600.0).c_str() : kMissing);
                    break;
                }
                case ICSsh: {
                    const std::string ssh = i.sshUrl();
                    if (ssh.empty()) {
                        ImGui::TextDisabled("%s", kMissing);
                    } else {
                        ui::CopyIcon("##sshcopy", i.sshCommand());
                        ImGui::SameLine();
                        ImGui::TextUnformatted(ssh.c_str() + 6);
                    }
                    break;
                }
                case ICPorts: ImGui::TextUnformatted(i.portsSummary().c_str()); break;
                case ICGpuUtil: usageBar(i.gpuUtil, 100.0, "%"); break;
                case ICCpuUtil: usageBar(cpuPercent(i), 100.0, "%"); break;
                case ICVram: usageBar(i.vmemUsage, i.gpuRamMb ? std::optional<double>(*i.gpuRamMb / 1024.0) : std::nullopt, "G"); break;
                case ICDisk: usageBar(i.diskUsage, i.diskSpace, "G"); break;
                case ICSpend: spendSparkline(app, i, ImVec2(-FLT_MIN, ImGui::GetFrameHeight())); break;
                case ICActions: drawActions(app, i); break;
                default: break;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void InstancesView::drawDialogs(App& app) {
    if (dialog_ == Dialog::None) return;
    const char* kId = "Instance action###instdlg";
    if (openPopup_) {
        ImGui::OpenPopup(kId);
        openPopup_ = false;
        if (dialog_ == Dialog::CloudCopy) connections_.fetch(app, cmd::showConnections());
        if (dialog_ == Dialog::AttachSsh) sshKeys_.fetch(app, cmd::showSshKeys());
    }
    const float s = app.dpiScale() * app.settings().uiScale;
    const float dlgW = dialog_ == Dialog::Json || dialog_ == Dialog::Execute ? 760 * s : dialog_ == Dialog::Prepay ? 680 * s : 560 * s;
    if (dialog_ != Dialog::Prepay) ImGui::SetNextWindowSize(ImVec2(dlgW, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // The reserve dialog changes height (custom amount, credit warning): keep its width, fit the height to the content.
    ImGuiWindowFlags dlgFlags = ImGuiWindowFlags_NoSavedSettings;
    if (dialog_ == Dialog::Prepay) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(dlgW, 0), ImVec2(dlgW, FLT_MAX));
        dlgFlags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    bool closedNow = false;
    if (!ui::BeginDialog(kId, dlgFlags, &closedNow)) {
        if (closedNow) dialog_ = Dialog::None;
        return;
    }
    const Instance& i = target_;
    auto refresh = [&app](const ApiResult&) { app.refreshInstances(); };
    bool close = false;
    const float bw = 130 * s;
    auto field = [&](const char* label, std::string* v, const char* hint = "", ImGuiInputTextFlags flags = 0) {
        ImGui::AlignTextToFramePadding();
        ui::TextDim(label);
        ImGui::SameLine(130 * s);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint((std::string("##") + label).c_str(), hint, v, flags);
    };
    auto title = [&](const char* icon, const std::string& t) {
        ui::SectionTitle(icon, t.c_str());
        ImGui::Separator();
    };

    switch (dialog_) {
        case Dialog::Label:
            title(ICON_FA_TAG, std::format("Label instance {}", i.id));
            field("Label", &text1_);
            if (ImGui::Button("Save", ImVec2(bw, 0))) {
                app.run(cmd::labelInstance(i.id, text1_), refresh, "Label updated");
                close = true;
            }
            break;
        case Dialog::ChangeBid:
            title(ICON_FA_DOLLAR_SIGN, std::format("Change bid for instance {}", i.id));
            ImGui::InputDouble("$/hr", &number_, 0.01, 0.1, "%.4f");
            if (i.isOutbid()) {
                ui::TextWithColor(ui::palette().danger, ICON_FA_GAVEL " This instance is outbid and not running.");
            }
            ui::TextDim("Applies to interruptible instances; raise it to resume an outbid instance.");
            if (ImGui::Button("Change bid", ImVec2(bw, 0))) {
                app.run(cmd::changeBid(i.id, number_), refresh, "Bid changed");
                close = true;
            }
            break;
        case Dialog::Prepay: {
            title(ICON_FA_TAG, std::format("Add a Reserved Discount - instance {}", i.id));
            const auto plans = reservePlans(i, fmt::nowEpoch());
            const double dph = i.dphTotal.value_or(0.0);
            const double maxD = i.creditDiscountMax.value_or(0.0);
            const int customIdx = static_cast<int>(plans.size());
            choice_ = std::clamp(choice_, 0, customIdx);
            const ui::Palette& pal = ui::palette();

            ImGui::TextUnformatted("Select a discount option to save money on your hourly rate.");
            ImGui::Text("Current rate: %s/hr", fmt::money(i.dphTotal, app.settings().currency).c_str());
            if (i.creditBalance.value_or(0.0) > 0.005) {
                ImGui::PushTextWrapPos(0.0f);
                ui::TextWithColor(pal.warning,
                                  std::format(ICON_FA_TRIANGLE_EXCLAMATION " This instance already has ${:.2f} prepaid ({:.0f}% "
                                              "discount). Adding credit recalculates the discount over the whole prepaid "
                                              "period; small additions late in a term can lower it.",
                                              *i.creditBalance, i.creditDiscount.value_or(0.0) * 100.0)
                                      .c_str());
                ImGui::PopTextWrapPos();
            }
            if (plans.empty()) {
                ui::TextDim(maxD > 0.0 ? "No 1/3/6 month plan ends before this machine's offer end date; use a custom amount."
                                       : "The host offers no reserved discount for this machine (prepaying gives no discount).");
            }

            // Plan dropdown: label left, discounted rate right (like the web console).
            const std::string preview = choice_ < customIdx ? reservePlanLabel(plans[static_cast<std::size_t>(choice_)])
                                                            : std::string("Custom amount...");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##plan", preview.c_str())) {
                for (int k = 0; k <= customIdx; ++k) {
                    const bool isCustom = k == customIdx;
                    const std::string label = isCustom ? std::string("Custom amount...") : reservePlanLabel(plans[static_cast<std::size_t>(k)]);
                    if (ImGui::Selectable((label + "##plan" + std::to_string(k)).c_str(), choice_ == k)) {
                        choice_ = k;
                        flag_ = false;
                        if (!isCustom) number_ = std::round(plans[static_cast<std::size_t>(k)].amount * 100.0) / 100.0;
                    }
                    if (!isCustom) {
                        const std::string rate = std::format("${:.3f}/hr", plans[static_cast<std::size_t>(k)].ratePerHour);
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(rate.c_str()).x);
                        ui::TextDim(rate.c_str());
                    }
                }
                ImGui::EndCombo();
            }

            std::optional<ReservedPlan> plan;
            if (choice_ < customIdx) {
                plan = plans[static_cast<std::size_t>(choice_)];
            } else {
                ImGui::AlignTextToFramePadding();
                ui::TextDim("Amount ($)");
                ImGui::SameLine(130 * s);
                ImGui::SetNextItemWidth(160 * s);
                if (ImGui::InputDouble("##amount", &number_, 10, 100, "%.2f")) flag_ = false;
                number_ = std::max(0.0, number_);
                plan = reservedPlanForAmount(dph, maxD, number_);
                ImGui::SameLine();
                if (plan) {
                    ui::TextDim(std::format("= {:.2f} months, save {:.1f}%, ${:.3f}/hr", plan->months, plan->discount * 100.0,
                                            plan->ratePerHour)
                                    .c_str());
                }
            }
            const double amount = plan ? std::round(plan->amount * 100.0) / 100.0 : 0.0;

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, pal.textDim);
            ImGui::TextUnformatted(std::format("Choosing this option will immediately deduct ${:.2f} from your current credits "
                                               "and \"deposit\" them for use by the instance. Usual billing applies to those "
                                               "credits until they run out.",
                                               amount)
                                       .c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("You can destroy a reserved instance to refund part of the remaining deposit: the remaining "
                                   "deposit minus the discount received so far and a 5% fee. Prepaid credit stays with this "
                                   "instance and cannot move to another machine.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            {
                // Text link, like the web console.
                const char* kLink = ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Learn about reserved instance discounts";
                ui::TextWithColor(pal.accent, kLink);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, ui::col(pal.accent), 1.0f);
                }
                if (ImGui::IsItemClicked()) platform::openUrl("https://docs.vast.ai/guides/instances/choosing/reserved-instances");
            }

            const ApiCall call = cmd::prepayInstance(i.id, amount);
            ImGui::Spacing();

            const double credit = jsonutil::num(app.user(), "credit", -1.0);
            const bool tooLittle = credit >= 0.0 && amount > credit + 1e-9;
            if (tooLittle) {
                ui::TextWithColor(pal.danger, std::format(ICON_FA_CIRCLE_XMARK " Your credit (${:.2f}) is lower than ${:.2f}.",
                                                          credit, amount)
                                                  .c_str());
            }
            ImGui::Separator();
            // Two steps: Purchase -> Confirm, because credits are deducted immediately.
            ImGui::BeginDisabled(amount <= 0.0 || tooLittle);
            if (!flag_) {
                if (ImGui::Button(ICON_FA_CART_SHOPPING " Purchase", ImVec2(bw, 0))) flag_ = true;
            } else {
                if (ui::DangerButton(std::format("Deduct ${:.2f} now", amount).c_str(), ImVec2(bw * 1.4f, 0))) {
                    app.run(call, refresh, std::format("Reserved instance {}: ${:.2f} prepaid", i.id, amount));
                    close = true;
                }
                ImGui::SameLine();
                if (ui::SecondaryButton("Back", ImVec2(bw * 0.6f, 0))) flag_ = false;
            }
            ImGui::EndDisabled();
            break;
        }
        case Dialog::Execute:
            title(ICON_FA_TERMINAL, std::format("Execute command on instance {}", i.id));
            field("Command", &text1_, "ls -la /workspace");
            ui::TextDim("Limited to simple commands (ls, rm, du, nvidia-smi ...) by vast.ai.");
            if (ui::BusyButton(ICON_FA_PLAY, "Run", busy_, true, ImVec2(bw, 0)) && !busy_) {
                busy_ = true;
                execOutput_.clear();
                app.client().submit(cmd::executeCommand(i.id, text1_), [this](const ApiResult& r) {
                    busy_ = false;
                    execOutput_ = r.ok ? (r.data.is_null() ? r.text : r.data.dump(2)) : "ERROR: " + r.error;
                });
            }
            ui::MonoText("##execout", busy_ && execOutput_.empty() ? "running..." : execOutput_, ImVec2(-FLT_MIN, 260 * s));
            break;
        case Dialog::Copy:
            title(ICON_FA_RIGHT_LEFT, "Copy files");
            field("Source", &text1_);
            field("Destination", &text2_);
            ui::TextDim("Formats: C.<instance>:/path, V.<volume>:/path, <cloud>.<connection>:/path");
            ui::TextDim("Copies run on vast.ai between remote locations. To copy from/to this PC use the SSH / SCP command.");
            ui::TextWithColor(ui::palette().warning, "Never copy to /root or / on the instance.");
            ImGui::BeginDisabled(fmt::trim(text1_).empty() || fmt::trim(text2_).empty());
            if (ImGui::Button(ICON_FA_RIGHT_LEFT " Copy", ImVec2(bw, 0))) {
                ApiCall call = cmd::copyFiles(fmt::trim(text1_), fmt::trim(text2_));
                // Invalid / local locations fail without a request: keep the dialog open so the input can be fixed.
                close = call.error.empty();
                app.run(std::move(call), {}, "Copy started - progress shows in the instance status");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::SecondaryButton("Cancel copy to destination")) app.run(cmd::cancelCopy(text2_), {}, "Copy cancelled");
            break;
        case Dialog::CloudCopy: {
            title(ICON_FA_CLOUD, std::format("Cloud copy for instance {}", i.id));
            const char* dirs[] = {"Instance To Cloud", "Cloud To Instance"};
            ImGui::Combo("Direction", &choice_, dirs, 2);
            field("Source path", &text1_);
            field("Destination path", &text2_);
            ImGui::AlignTextToFramePadding();
            ui::TextDim("Connection");
            ImGui::SameLine(130 * s);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##conn", text3_.empty() ? "select connection..." : text3_.c_str())) {
                if (connections_.loading) ui::SpinnerText("##cl", "loading...");
                for (const auto& c : jsonutil::extractArray(connections_.data, {"results", "connections"})) {
                    const std::string id = jsonutil::str(c, "id");
                    const std::string label = std::format("{}  {}  {}", id, jsonutil::str(c, "name"), jsonutil::str(c, "cloud_type"));
                    if (ImGui::Selectable(label.c_str(), text3_ == id)) text3_ = id;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Dry run", &flag_);
            ui::TextDim("Add cloud connections in the vast.ai console (Settings > Cloud connections).");
            ImGui::BeginDisabled(text3_.empty());
            if (ImGui::Button(ICON_FA_CLOUD " Start copy", ImVec2(bw, 0))) {
                cmd::CloudCopyParams p;
                p.src = text1_;
                p.dst = text2_;
                p.instanceId = i.id;
                p.connectionId = text3_;
                p.transfer = dirs[choice_];
                p.dryRun = flag_;
                app.run(cmd::cloudCopy(p), {}, "Cloud copy started");
                close = true;
            }
            ImGui::EndDisabled();
            break;
        }
        case Dialog::CreateTemplate:
            title(ICON_FA_LAYER_GROUP, std::format("Create template from instance {}", i.id));
            field("Name", &text1_);
            field("Image", &text2_);
            field("Docker options", &text3_);
            ImGui::TextUnformatted("On-start");
            ImGui::InputTextMultiline("##onstart", &text4_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
            ImGui::Checkbox("Public", &flag_);
            if (ImGui::Button(ICON_FA_FLOPPY_DISK " Create", ImVec2(bw, 0))) {
                cmd::TemplateParams p;
                p.name = text1_;
                const auto colon = text2_.rfind(':');
                if (colon != std::string::npos && text2_.find('/', colon) == std::string::npos) {
                    p.image = text2_.substr(0, colon);
                    p.tag = text2_.substr(colon + 1);
                } else {
                    p.image = text2_;
                }
                p.env = text3_;
                p.onstart = text4_;
                p.jupyter = i.imageRuntype.find("jupyter") != std::string::npos;
                p.ssh = true;
                p.direct = i.imageRuntype.find("direc") != std::string::npos;
                p.isPublic = flag_;
                p.diskSpaceGb = i.diskSpace;
                app.run(cmd::createTemplate(p), {}, "Template created");
                close = true;
            }
            break;
        case Dialog::Update:
            title(ICON_FA_ARROWS_ROTATE, std::format("Update instance {} (recreates the container)", i.id));
            field("Template ID", &text4_, "optional numeric template id");
            field("Image", &text1_);
            field("Docker options", &text2_);
            ImGui::TextUnformatted("On-start");
            ImGui::InputTextMultiline("##onstartu", &text3_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
            if (ImGui::Button("Update", ImVec2(bw, 0))) {
                cmd::UpdateInstanceParams p;
                if (auto id = jsonutil::parseDouble(text4_)) p.templateId = static_cast<std::int64_t>(*id);
                if (text1_ != i.image) p.image = text1_;
                p.env = text2_;
                p.onstart = text3_;
                app.run(cmd::updateInstance(i.id, p), refresh, "Update requested");
                close = true;
            }
            break;
        case Dialog::Destroy: {
            std::vector<std::string> ids;
            for (auto id : bulkIds_) ids.push_back(std::to_string(id));
            title(ICON_FA_TRASH, "Destroy instances");
            ImGui::TextWrapped("Permanently destroy instance(s) %s? All data on the instance disk is lost. This cannot be undone.",
                               fmt::join(ids, ", ").c_str());
            if (ui::DangerButton(ICON_FA_TRASH " Destroy", ImVec2(bw, 0))) {
                app.run(cmd::destroyInstances(bulkIds_), refresh, "Destroy requested");
                for (auto id : bulkIds_) selected_.erase(id);
                close = true;
            }
            break;
        }
        case Dialog::Recycle:
            title(ICON_FA_RECYCLE, std::format("Recycle instance {}", i.id));
            ImGui::TextWrapped("Destroys and recreates the container from the same image (pulls the image again). "
                               "Data in the container is lost.");
            if (ui::DangerButton(ICON_FA_RECYCLE " Recycle", ImVec2(bw, 0))) {
                app.run(cmd::recycleInstance(i.id), refresh, "Recycle requested");
                close = true;
            }
            break;
        case Dialog::Snapshot:
            title(ICON_FA_CAMERA, std::format("Snapshot instance {} to a container registry", i.id));
            field("Registry", &text4_);
            field("Repository", &text1_, "user/repo:tag");
            field("Username", &text2_);
            field("Password", &text3_, "", ImGuiInputTextFlags_Password);
            ImGui::Checkbox("Pause container during snapshot", &flag_);
            if (ImGui::Button(ICON_FA_CAMERA " Snapshot", ImVec2(bw, 0))) {
                cmd::SnapshotParams p;
                p.repo = text1_;
                p.user = text2_;
                p.password = text3_;
                p.registry = text4_;
                p.pause = flag_;
                app.run(cmd::takeSnapshot(i.id, p), {}, "Snapshot started");
                close = true;
            }
            break;
        case Dialog::AttachSsh: {
            title(ICON_FA_KEY, std::format("Attach SSH key to instance {}", i.id));
            if (sshKeys_.loading && sshKeys_.data.is_null()) ui::TextDim("loading keys...");
            for (const auto& k : jsonutil::extractArray(sshKeys_.data, {"ssh_keys", "keys"})) {
                const std::string key = jsonutil::str(k, "public_key", jsonutil::str(k, "ssh_key"));
                if (key.empty()) continue;
                const std::string label = key.size() > 70 ? key.substr(0, 40) + "..." + key.substr(key.size() - 24) : key;
                if (ImGui::Selectable(label.c_str(), text1_ == key)) text1_ = key;
            }
            ImGui::TextUnformatted("Public key");
            ImGui::InputTextMultiline("##pubkey", &text1_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3));
            if (ImGui::Button(ICON_FA_LINK " Attach", ImVec2(bw, 0))) {
                app.run(cmd::attachSsh(i.id, fmt::trim(text1_)), {}, "SSH key attached");
                close = true;
            }
            break;
        }
        case Dialog::Json:
            title(ICON_FA_CODE, std::format("Instance {}", i.id));
            if (ImGui::Button(ICON_FA_COPY " Copy JSON")) ImGui::SetClipboardText(i.raw.dump(2).c_str());
            ui::JsonText("##instjson", i.raw, ImVec2(-FLT_MIN, 480 * s));
            break;
        case Dialog::None: break;
    }
    ImGui::SameLine();
    if (ui::SecondaryButton("Close", ImVec2(bw, 0)) || close) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void InstancesView::drawFloating(App& app) {
    const double now = ImGui::GetTime();
    constexpr const char* kTabNames[2] = {ICON_FA_FILE_LINES " Instance logs", ICON_FA_SERVER " Daemon logs"};
    for (auto& wp : logWindows_) {
        LogWindow& w = *wp;
        if (!w.open) continue;
        const std::string title = std::format("Logs: instance {}###logs{}", w.id, w.id);
        const float em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(em * 62.0f, em * 36.0f), ImGuiCond_FirstUseEver);
        const bool logsVisible = ImGui::Begin(title.c_str(), &w.open);
        if (!ImGui::IsWindowDocked() && ui::ClickedOutsideCurrentWindow()) w.open = false;
        if (logsVisible) {
            if (ImGui::BeginTabBar("##logtabs")) {
                for (int t = 0; t < 2; ++t) {
                    if (ImGui::BeginTabItem(kTabNames[t])) {
                        if (w.active != t) {
                            w.active = t;
                            if (!w.tabs[t].res.loaded && !w.tabs[t].res.loading) fetchLogs(app, w, t);
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            LogTab& tab = w.tabs[w.active];
            const std::string text = tab.res.data.is_string() ? tab.res.data.get<std::string>()
                                     : tab.res.data.is_null() ? std::string{}
                                                              : tab.res.data.dump(2);

            // Toolbar: identical widgets on both tabs so switching never shifts the layout.
            if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", tab.res.loading)) fetchLogs(app, w, w.active);
            ImGui::SameLine();
            ImGui::Checkbox("Auto", &w.autoRefresh);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(em * 7.5f);
            ImGui::SliderInt("##int", &w.intervalSec, 3, 120, "every %d s");
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ui::TextDim("Tail");
            ImGui::SameLine();
            {
                constexpr int kTailStep = 1000, kTailMin = 100, kTailMax = 20000;
                const float btn = ImGui::GetFrameHeight();
                const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
                ImGui::SetNextItemWidth(em * 4.5f);
                ImGui::InputInt("##tail", &tab.tail, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit()) tab.tail = std::clamp(tab.tail, kTailMin, kTailMax);
                ImGui::SameLine(0, sp);
                if (ImGui::Button("-##tailm", ImVec2(btn, 0)))
                    tab.tail = std::max(kTailMin, ((tab.tail - 1) / kTailStep) * kTailStep);
                ImGui::SameLine(0, sp);
                if (ImGui::Button("+##tailp", ImVec2(btn, 0)))
                    tab.tail = std::min(kTailMax, (tab.tail / kTailStep + 1) * kTailStep);
                ImGui::SameLine(0, sp);
                if (ImGui::Button("Max##tailx")) tab.tail = kTailMax;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Request the last %d lines", kTailMax);
            }
            // Wrap the rest of the toolbar onto a second line in narrow windows instead of clipping it.
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < em * 14.0f) ImGui::NewLine();
            ImGui::SetNextItemWidth(em * 10);
            if (ImGui::InputTextWithHint("##lf", ICON_FA_FILTER " grep filter", &tab.filter,
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                fetchLogs(app, w, w.active);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(text.empty());
            if (ui::IconButton(ICON_FA_COPY, "Copy logs (without line numbers)")) ImGui::SetClipboardText(text.c_str());
            ImGui::SameLine(0, 2);
            if (ui::IconButton(ICON_FA_DOWNLOAD, "Save these logs to a file (without line numbers)")) {
                const std::string name = std::format("instance_{}_{}logs.txt", w.id, w.active == 1 ? "daemon_" : "");
                if (const auto path = platform::saveFileDialog("Save logs", name.c_str())) {
                    if (platform::writeTextFileAtomic(*path, text)) {
                        app.toasts().success("Logs saved", platform::pathToUtf8(*path));
                    } else {
                        app.toasts().error("Could not save logs", platform::pathToUtf8(*path));
                    }
                }
            }
            ImGui::EndDisabled();
            ui::InlineError(tab.res.error, 60);

            ui::LogText("##logtext", text.empty() && tab.res.loading ? "requesting logs..." : text, ImVec2(0, 0), true);
            if (w.autoRefresh && !tab.res.loading && now - tab.res.attemptedAt > w.intervalSec) fetchLogs(app, w, w.active);
        }
        ImGui::End();
    }
    std::erase_if(logWindows_, [](const auto& w) {
        return !w->open && !w->tabs[0].res.loading && !w->tabs[1].res.loading;
    });
}

}  // namespace vast
