#include "views/SearchView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "logic/DockerOptions.hpp"
#include "logic/QueryBuilder.hpp"
#include "logic/Regions.hpp"
#include "views/TemplatesView.hpp"
#include "widgets/Badge.hpp"
#include "widgets/RangeSlider.hpp"
#include "widgets/Spinner.hpp"

#include <algorithm>
#include <any>
#include <cmath>
#include <format>
#include <memory>
#include <set>

namespace vast {

namespace {

float uiScale(App& app) { return app.dpiScale() * app.settings().uiScale; }

// Starts a new row when the next widget group would not fit.
void sameLineIfFits(float width) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
}

std::string rangeLabel(const RangeMeta& m, const Range& r) {
    if (!r.active()) return "Any";
    auto f = [&](double v) {
        if (m.field == RangeField::DphTotal) return std::format("${:.{}f}", v, m.decimals);
        return std::format("{:.{}f}", v, m.decimals);
    };
    const std::string unit = m.unit[0] ? std::string(" ") + m.unit : std::string{};
    if (r.min && r.max) return f(*r.min) + " - " + f(*r.max) + unit;
    if (r.min) return ">= " + f(*r.min) + unit;
    return "<= " + f(*r.max) + unit;
}

enum OfferCol {
    ColId, ColMachine, ColHost, ColGpu, ColNum, ColVram, ColTotalVram, ColTflops, ColDlperf, ColPrice,
    ColTflopsPerDollar, ColDlpPerDollar, ColReliability, ColDuration, ColInetUp, ColInetDown, ColDiskBw,
    ColDisk, ColCpu, ColCores, ColRam, ColPcie, ColCuda, ColLocation, ColVerification, ColPorts, ColRent, ColCount
};

struct OfferColumn {
    const char* name;
    float width;
    bool defaultHidden;
};

constexpr OfferColumn kOfferColumns[ColCount] = {
    {"Offer ID", 80, false}, {"Machine", 65, false},   {"Host", 65, true},          {"GPU", 120, false},
    {"#", 28, false},        {"VRAM/GPU", 70, false},  {"Total VRAM", 75, true},    {"TFLOPS", 60, false},
    {"DLPerf", 60, false},   {"$/hr", 65, false},      {"TFLOPS/$/hr", 85, false},  {"DLP/$/hr", 75, false},
    {"Reliability", 75, false}, {"Max Dur", 70, false},
    {"Inet Up", 65, false},  {"Inet Down", 70, false}, {"Disk BW", 65, false},      {"Disk GB", 60, true},
    {"CPU", 160, true},      {"Cores", 55, true},      {"RAM GB", 60, true},        {"PCIe", 70, true},
    {"CUDA", 45, false},     {"Location", 110, false}, {"Status", 80, false},       {"Ports", 50, true},
    {"", 60, false},
};

constexpr std::pair<int, SortKey> kColumnSortKeys[] = {
    {ColNum, SortKey::NumGpus},          {ColVram, SortKey::Vram},
    {ColTflops, SortKey::Tflops},        {ColDlperf, SortKey::Dlperf},
    {ColPrice, SortKey::Price},          {ColDlpPerDollar, SortKey::DlpPerDollar},
    {ColTflopsPerDollar, SortKey::TflopsPerDollar}, {ColReliability, SortKey::Reliability},
    {ColDuration, SortKey::Duration},    {ColInetUp, SortKey::InetUp},
    {ColInetDown, SortKey::InetDown},    {ColDiskBw, SortKey::DiskBw},
    {ColCuda, SortKey::Cuda},
};

std::optional<SortKey> sortKeyForColumn(int c) {
    for (const auto& [col, key] : kColumnSortKeys) {
        if (col == c) return key;
    }
    return std::nullopt;
}

std::optional<int> sortColumnFor(SortKey k) {
    for (const auto& [col, key] : kColumnSortKeys) {
        if (key == k) return col;
    }
    return std::nullopt;
}

std::optional<double> numericCell(int c, const Offer& o, const OfferMetrics& m) {
    switch (c) {
        case ColId: return static_cast<double>(o.id);
        case ColMachine: return o.machineId ? std::optional<double>(static_cast<double>(*o.machineId)) : std::nullopt;
        case ColHost: return o.hostId ? std::optional<double>(static_cast<double>(*o.hostId)) : std::nullopt;
        case ColNum: return static_cast<double>(o.numGpus);
        case ColVram: return o.gpuRamGb();
        case ColTotalVram: return o.totalVramGb();
        case ColTflops: return o.totalFlops;
        case ColDlperf: return o.dlperf;
        case ColPrice: return o.priceHr;
        case ColDlpPerDollar: return m.dlpPerDollar;
        case ColTflopsPerDollar: return m.tflopsPerDollar;
        case ColReliability: return o.reliability;
        case ColDuration: return o.durationSec;
        case ColInetUp: return o.inetUp;
        case ColInetDown: return o.inetDown;
        case ColDiskBw: return o.diskBw;
        case ColDisk: return o.diskSpace;
        case ColCores: return o.cpuCoresEffective;
        case ColRam: return o.cpuRamGb();
        case ColPcie: return o.pcieBw;
        case ColCuda: return o.cudaMaxGood;
        case ColPorts: return o.directPortCount;
        default: return std::nullopt;
    }
}

std::string textCell(int c, const Offer& o, const OfferMetrics& m, const CurrencyFormat& cur) {
    switch (c) {
        case ColId: return std::to_string(o.id);
        case ColMachine: return o.machineId ? std::to_string(*o.machineId) : kMissing;
        case ColHost: return o.hostId ? std::to_string(*o.hostId) : kMissing;
        case ColGpu: return o.gpuName;
        case ColNum: return std::to_string(o.numGpus);
        case ColVram: return formatMetric(o.gpuRamGb(), 0);
        case ColTotalVram: return formatMetric(o.totalVramGb(), 0);
        case ColTflops: return formatMetric(o.totalFlops, 1);
        case ColDlperf: return formatMetric(o.dlperf, 1);
        case ColPrice: return fmt::money(o.priceHr, cur);
        case ColDlpPerDollar: return formatMetric(m.dlpPerDollar, 1);
        case ColTflopsPerDollar: return formatMetric(m.tflopsPerDollar, 1);
        case ColReliability: return fmt::percent(o.reliability, 2);
        case ColDuration: return o.durationSec ? fmt::humanDuration(*o.durationSec) : std::string(kMissing);
        case ColInetUp: return formatMetric(o.inetUp, 0);
        case ColInetDown: return formatMetric(o.inetDown, 0);
        case ColDiskBw: return formatMetric(o.diskBw, 0);
        case ColDisk: return formatMetric(o.diskSpace, 0);
        case ColCpu: return o.cpuName;
        case ColCores: return formatMetric(o.cpuCoresEffective, 1);
        case ColRam: return formatMetric(o.cpuRamGb(), 0);
        case ColPcie: return std::format("{} x{}", formatMetric(o.pciGen, 1), formatMetric(o.gpuLanes, 0));
        case ColCuda: return formatMetric(o.cudaMaxGood, 1);
        case ColLocation: return o.geolocation;
        case ColVerification: return o.verification;
        case ColPorts: return formatMetric(o.directPortCount, 0);
        default: return {};
    }
}

}  // namespace

void SearchView::init(App& app) {
    Settings& s = app.settings();
    if (s.offerColumns.size() != ColCount) {
        s.offerColumns.assign(ColCount, true);
        for (int c = 0; c < ColCount; ++c) s.offerColumns[static_cast<std::size_t>(c)] = !kOfferColumns[c].defaultHidden;
    }
    search(app);
}

void SearchView::onTemplateChanged(App& app) {
    updateTemplateRequirements(app);
    recompute(app);
    filtersChanged(app);
}

void SearchView::preselectVolume(App& app, std::int64_t volumeId) {
    rent_.volumeMode = 1;
    rent_.volumeId = volumeId;
    if (!volumesRes_.loaded) volumesRes_.fetch(app, cmd::showVolumes());
}

void SearchView::filtersChanged(App& app, bool immediate) {
    app.markSettingsDirty();
    syncTableSort_ = true;
    if (immediate) {
        search(app);
    } else {
        changedAt_ = ImGui::GetTime();
    }
}

void SearchView::search(App& app) {
    changedAt_ = -1.0;
    if (loading_) {
        pendingSearch_ = true;
        return;
    }
    pendingSearch_ = false;
    SearchFilters& f = app.settings().filters;
    f.limit = app.settings().searchLimit;
    updateTemplateRequirements(app);

    const auto parsed = QueryBuilder::parseQuery(QueryBuilder::buildQueryString(f));
    warnings_ = parsed.warnings;
    if (!parsed.ok()) {
        error_ = parsed.error;
        return;
    }
    error_.clear();
    loading_ = true;
    lastSearchAt_ = ImGui::GetTime();

    // vast.ai returns a random sample per request; several identical requests merged give a far more
    // complete and stable result (measured: limit 1000 x 3 finds the true best 10 and ~87% of the best 200).
    struct Batch {
        std::vector<std::vector<Offer>> samples;
        int pending = 0;
        std::string error;
    };
    const int n = std::max(1, app.settings().searchRequests);
    auto batch = std::make_shared<Batch>();
    batch->pending = n;
    for (int i = 0; i < n; ++i) {
        ApiCall c = cmd::searchOffers(f);
        c.quiet = i > 0;  // the console shows the command once
        c.prepare = [bid = f.rentalType == RentalType::Interruptible](ApiResult& r) -> std::any {
            auto list = std::make_shared<std::vector<Offer>>();
            for (const auto& j : jsonutil::extractArray(r.data, {"offers"})) {
                if (!j.is_object()) continue;
                list->push_back(j.get<Offer>());
                if (bid) applyBidMarkup(list->back());
            }
            return list;
        };
        app.client().submit(std::move(c), [this, &app, batch, n](const ApiResult& r) {
            if (r.ok) {
                using OfferList = std::shared_ptr<std::vector<Offer>>;
                if (const auto* list = std::any_cast<OfferList>(&r.prepared); list && *list) {
                    batch->samples.push_back(std::move(**list));
                } else {
                    batch->samples.emplace_back();
                }
            } else if (batch->error.empty()) {
                batch->error = r.error;
            }
            if (--batch->pending > 0) return;
            loading_ = false;
            if (!batch->samples.empty()) {
                const int okCount = static_cast<int>(batch->samples.size());
                receivedOffers_ = 0;
                for (const auto& s : batch->samples) receivedOffers_ += s.size();
                mergedRequests_ = okCount;
                offers_ = mergeOfferSamples(std::move(batch->samples));
                rentedOffers_.clear();  // fresh results; own machines are marked via the instance list
                recompute(app);
                // A partial failure still yields results; mention it without hiding them.
                error_ = okCount < n ? std::format("{} of {} requests failed: {}", n - okCount, n, batch->error)
                                     : std::string{};
            } else {
                error_ = batch->error;
            }
            if (pendingSearch_) search(app);
        });
    }
}

void SearchView::updateTemplateRequirements(App& app) {
    SearchFilters& f = app.settings().filters;
    const Template& t = app.settings().currentTemplate;
    f.templateRequirements = templateRequirements(t.searchParams, t.imageWithTag());
    f.templateMinCuda = requiredMinCuda(f.templateRequirements);
    f.templateDiskGb = t.recommendedDiskGb;
}

void SearchView::recompute(App& app) {
    const Settings& s = app.settings();
    const SearchFilters& f = s.filters;
    const double newVolumeGb = rent_.volumeMode == 2 ? rent_.volumeSizeGb : 0.0;
    for (Offer& o : offers_) setNewVolumeCost(o, newVolumeGb);
    metrics_ = computeMetrics(offers_);
    defects_ = evaluateDefects(offers_, s.defects, s.blacklist);
    const auto order = sortedOrder(offers_, metrics_, f.sortKey, f.sortDesc);
    visible_.clear();
    defectiveCount_ = 0;
    blacklistedCount_ = 0;
    hiddenDeverified_ = 0;
    hiddenIncompatible_ = 0;
    hiddenRented_ = 0;
    hiddenNonFavorite_ = 0;
    tableRowsDirty_ = true;
    for (std::size_t idx : order) {
        if (!passesVerificationFilter(offers_[idx], f)) {
            ++hiddenDeverified_;
            continue;
        }
        if (f.favoritesOnly && !(offers_[idx].machineId && s.favoriteMachines.contains(*offers_[idx].machineId))) {
            ++hiddenNonFavorite_;
            continue;
        }
        if (!f.showRentedByMe && offers_[idx].machineId && rentedMachines_.contains(*offers_[idx].machineId)) {
            ++hiddenRented_;
            continue;
        }
        // The server only filters the template's CUDA requirement; the rest is checked here.
        if (!f.showIncompatible && isIncompatible(offers_[idx], f)) {
            ++hiddenIncompatible_;
            continue;
        }
        if (defects_[idx].blacklisted()) {
            ++blacklistedCount_;  // counts all blacklisted offers; they are only removed when not shown
            if (!f.showBlacklisted) continue;
        }
        if (defects_[idx].defective()) {
            ++defectiveCount_;  // counts all defective offers; they are only removed when not shown
            if (!f.showDefective) continue;
        }
        visible_.push_back(idx);
    }
}

void SearchView::handleAction(App& app, ui::OfferAction a, const Offer& o) {
    Settings& s = app.settings();
    switch (a) {
        case ui::OfferAction::Rent: openRent(app, o); break;
        case ui::OfferAction::RentNow: rentNow(app, o); break;
        case ui::OfferAction::CopyOfferId:
            ImGui::SetClipboardText(std::to_string(o.id).c_str());
            app.toasts().info("Copied offer ID " + std::to_string(o.id));
            break;
        case ui::OfferAction::CopyMachineId:
            if (o.machineId) {
                ImGui::SetClipboardText(std::to_string(*o.machineId).c_str());
                app.toasts().info("Copied machine ID " + std::to_string(*o.machineId));
            }
            break;
        case ui::OfferAction::CopyHostId:
            if (o.hostId) {
                ImGui::SetClipboardText(std::to_string(*o.hostId).c_str());
                app.toasts().info("Copied host ID " + std::to_string(*o.hostId));
            }
            break;
        case ui::OfferAction::HideHost:
            if (o.hostId) {
                s.blacklist.hosts.insert(*o.hostId);
                app.markSettingsDirty();
                recompute(app);
                app.toasts().info("Host " + std::to_string(*o.hostId) + " hidden", "Manage the blacklist in Settings.");
            }
            break;
        case ui::OfferAction::HideMachine:
            if (o.machineId) {
                const bool wasFavorite = s.favoriteMachines.contains(*o.machineId);
                s.setBlacklistedMachine(*o.machineId, true);
                app.markSettingsDirty();
                recompute(app);
                app.toasts().info("Machine " + std::to_string(*o.machineId) + " hidden",
                                  wasFavorite ? "Removed from your favorites. Manage the blacklist in Settings."
                                              : "Manage the blacklist in Settings.");
            }
            break;
        case ui::OfferAction::ToggleFavorite:
            if (o.machineId) {
                const bool add = !s.favoriteMachines.contains(*o.machineId);
                const bool wasBlacklisted = s.blacklist.machines.contains(*o.machineId);
                s.setFavoriteMachine(*o.machineId, add);
                app.markSettingsDirty();
                recompute(app);
                const std::string m = "Machine " + std::to_string(*o.machineId);
                if (add) {
                    app.toasts().info(m + " added to favorites",
                                      wasBlacklisted ? "Removed from the blacklist. Manage favorites in Settings."
                                                     : "Manage favorites in Settings.");
                } else {
                    app.toasts().info(m + " removed from favorites");
                }
            }
            break;
        case ui::OfferAction::ViewJson:
            if (std::none_of(jsonWindows_.begin(), jsonWindows_.end(), [&](const auto& w) { return w.first == o.id; })) {
                jsonWindows_.emplace_back(o.id, o.raw);
            }
            break;
        case ui::OfferAction::None: break;
    }
}

void SearchView::prepareTemplatePicker(App& app) {
    if (tplTab_ < 0) tplTab_ = app.apiKey().key.empty() ? 0 : 1;
    tplTabRequest_ = tplTab_;
    loadTemplateTab(app, tplTab_, false);
}

void SearchView::applyTemplateToRent(App& app, RentState& r, const Template& t) {
    r.templateName = t.name;
    r.templateFilters = t.searchParams;
    r.image = t.imageWithTag();
    r.templateHash = t.hashId;
    r.useTemplateHash = !t.hashId.empty();
    r.diskGb = std::max(app.settings().filters.diskGb, t.recommendedDiskGb.value_or(0.0));
    const DockerOptions opts = parseDockerOptions(t.env);
    r.env = opts.env;
    r.ports = opts.ports;
    r.extraDocker = opts.extra;
    r.onstart = t.onstart;
    r.launchMode = t.usesJupyter() ? 1 : (t.runtype == "args" ? 2 : 0);
    r.direct = t.isDirect() || t.runtype.find("direc") != std::string::npos;
    r.args.clear();
}

SearchView::RentState SearchView::newRentState(App& app, const Offer& o) {
    RentState r;
    r.offer = o;
    r.bidPrice = o.minBid ? *o.minBid * 1.05 : o.dphTotal.value_or(0.0);
    r.volumeMode = rent_.volumeMode;
    r.volumeId = rent_.volumeId;
    r.volumeSizeGb = rent_.volumeSizeGb;
    r.mountPath = rent_.mountPath;
    applyTemplateToRent(app, r, app.settings().currentTemplate);
    return r;
}

void SearchView::openRent(App& app, const Offer& o) {
    rent_ = newRentState(app, o);
    openRentPopup_ = true;
    if (rent_.volumeMode == 1 && !volumesRes_.loaded) volumesRes_.fetch(app, cmd::showVolumes());
}

ApiCall SearchView::buildRentCall(const RentState& r, bool interruptible) {
    const Offer& o = r.offer;
    cmd::CreateInstanceParams params;
    params.offerId = o.id;
    params.image = r.image;
    params.diskGb = r.diskGb;
    params.label = r.label;
    params.env = buildDockerOptions(DockerOptions{r.env, r.ports, r.extraDocker});
    params.onstartCmd = r.onstart;
    if (r.useTemplateHash) params.templateHash = r.templateHash;
    params.ssh = true;
    params.jupyter = r.launchMode == 1;
    params.argsMode = r.launchMode == 2;
    params.args = r.args;
    params.direct = r.direct;
    if (interruptible) params.bidPrice = r.bidPrice;
    if (r.volumeMode == 1 && r.volumeId) params.linkVolume = r.volumeId;
    if (r.volumeMode == 2) {
        if (auto ask = jsonutil::optInt(o.raw, "avail_vol_ask_id")) params.createVolume = *ask;
        params.volumeSizeGb = r.volumeSizeGb;
    }
    if (r.volumeMode != 0) params.mountPath = r.mountPath;
    params.cancelUnavail = r.cancelUnavail;
    return cmd::createInstance(params);
}

// On-demand RENT works like the vast.ai console: one click rents with the current template and the panel's disk /
// volume settings (right-click > Rent... opens the dialog). Anything that needs a decision opens the dialog instead.
void SearchView::rentNow(App& app, const Offer& o) {
    if (rentingOffers_.contains(o.id) || rentedOffers_.contains(o.id)) return;
    RentState r = newRentState(app, o);
    std::string why;
    if (r.image.empty() && !r.useTemplateHash) {
        why = "The template has no image.";
    } else if (!unmetRequirements(o, templateRequirements(r.templateFilters, r.image)).empty()) {
        why = "This machine does not meet the template's requirements.";
    } else if (r.volumeMode == 1 && !r.volumeId) {
        why = "Select the volume to link.";
    } else if (r.volumeMode == 2 && !jsonutil::optInt(o.raw, "avail_vol_ask_id")) {
        why = "This offer has no local volume offer available.";
    }
    if (!why.empty()) {
        app.toasts().info("Check the rental settings", why);
        openRent(app, o);
        return;
    }
    const std::int64_t offerId = o.id;
    rentingOffers_.insert(offerId);
    app.client().submit(buildRentCall(r, false), [this, &app, offerId](const ApiResult& res) {
        rentingOffers_.erase(offerId);
        if (!res.ok) return;
        rentedOffers_.insert(offerId);
        const auto id = jsonutil::optInt(res.data, "new_contract");
        app.toasts().success("Instance created", id ? "Instance ID " + std::to_string(*id) : res.text);
        app.refreshInstances();
    });
}

// ---------------------------------------------------------------------------

void SearchView::requestGpuTypes(App& app) {
    gpuTypesRequested_ = true;
    ApiCall c = cmd::showGpuTypes();
    c.quiet = true;  // background refresh; failures still reach the activity log
    app.client().submit(std::move(c), [this, &app](const ApiResult& r) {
        if (!r.ok) return;  // keep the cached / built-in list
        auto names = gpuNamesFromTypes(r.data);
        if (names.empty()) return;
        std::sort(names.begin(), names.end());
        if (names != app.settings().gpuModels) {
            app.settings().gpuModels = std::move(names);
            app.markSettingsDirty();
        }
        gpuCatalogDirty_ = true;
    });
}

void SearchView::rebuildGpuCatalog(App& app) {
    const Settings& st = app.settings();
    std::vector<std::string> names = st.gpuModels;
    if (names.empty()) names.assign(knownGpuModels().begin(), knownGpuModels().end());
    // Models that are in the results or selected but missing from the list (new or renamed) are shown too.
    for (const auto& o : offers_) {
        if (!o.gpuName.empty()) names.push_back(o.gpuName);
    }
    names.insert(names.end(), st.filters.gpuModels.begin(), st.filters.gpuModels.end());
    gpuCatalog_ = buildGpuCatalog(names);
    gpuSearchNames_.clear();
    for (const auto& g : gpuCatalog_) {
        for (const auto& m : g.models) gpuSearchNames_[m] = normaliseGpuName(m);
    }
    gpuCatalogDirty_ = false;
}

void SearchView::draw(App& app) {
    if (!gpuTypesRequested_) requestGpuTypes(app);
    // The limit is a setting (not part of saved filters/presets); keep the filters in sync so searches use it.
    app.settings().filters.limit = app.settings().searchLimit;
    // Favorites are a setting too; the filters carry a copy for the query. With "Favorites only" a changed list
    // needs a new search, otherwise only the marks / client-side filter change.
    {
        SearchFilters& f = app.settings().filters;
        const auto& fav = app.settings().favoriteMachines;
        std::vector<long long> ids(fav.begin(), fav.end());
        if (ids != f.favoriteMachines) {
            f.favoriteMachines = std::move(ids);
            if (f.favoritesOnly) changedAt_ = ImGui::GetTime();
            if (!offers_.empty()) recompute(app);
        }
    }
    // Re-filter when the set of machines with own instances changes (instances refresh in the background).
    {
        std::set<std::int64_t> rented;
        for (const auto& inst : app.instances()) {
            if (inst.machineId) rented.insert(*inst.machineId);
        }
        if (rented != rentedMachines_) {
            rentedMachines_ = std::move(rented);
            if (!offers_.empty()) recompute(app);
        }
    }
    if (changedAt_ >= 0 && ImGui::GetTime() - changedAt_ > 0.6) search(app);
    const int autoSec = app.settings().searchRefreshSec;
    if (autoSec > 0 && !loading_ && lastSearchAt_ >= 0 && ImGui::GetTime() - lastSearchAt_ > autoSec) search(app);

    drawTopBar(app);
    ImGui::Spacing();
    const float s = uiScale(app);
    if (filterPanelOpen_) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette().panel);
        ImGui::BeginChild("##filters", ImVec2(305 * s, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        drawFilterPanel(app);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::BeginChild("##results", ImVec2(0, 0), ImGuiChildFlags_None);
    drawResults(app);
    ImGui::EndChild();

    if (openRentPopup_) {
        ImGui::OpenPopup("Rent instance###rent");
        openRentPopup_ = false;
    }
    if (openTemplatePicker_) {
        ImGui::OpenPopup("Select template###tplpick");
        openTemplatePicker_ = false;
        pickerForRent_ = false;
        prepareTemplatePicker(app);
    }
    if (openTemplateEditor_) {
        ImGui::OpenPopup("Edit template###tpledit");
        openTemplateEditor_ = false;
    }
    if (openSavePreset_) {
        ImGui::OpenPopup("Save filter preset###savepreset");
        openSavePreset_ = false;
    }
    drawRentModal(app);
    drawTemplatePicker(app);
    drawTemplateEditor(app);
    drawPresetPopups(app);
}

void SearchView::drawTopBar(App& app) {
    SearchFilters& f = app.settings().filters;
    const float s = uiScale(app);
    const ui::Palette& p = ui::palette();

    if (ui::IconButton(filterPanelOpen_ ? ICON_FA_CHEVRON_LEFT : ICON_FA_SLIDERS,
                       filterPanelOpen_ ? "Hide filter panel" : "Show filter panel")) {
        filterPanelOpen_ = !filterPanelOpen_;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::TextDim("#GPUs");
    ImGui::SameLine();
    constexpr std::pair<int, const char*> kCounts[] = {{0, "ANY"}, {1, "1X"}, {2, "2X"}, {4, "4X"}, {8, "8X"}, {9, "9X+"}};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * s, 4 * s));
    for (const auto& [v, label] : kCounts) {
        if (ui::ToggleButton(label, f.gpuCount == v, ImVec2(40 * s, 0))) {
            f.gpuCount = v;
            filtersChanged(app, true);
        }
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::NewLine();
    ImGui::SameLine(0, 0);

    sameLineIfFits(140 * s);
    ImGui::SetNextItemWidth(140 * s);
    if (ImGui::BeginCombo("##rental", rentalTypeLabel(f.rentalType))) {
        for (RentalType t : {RentalType::OnDemand, RentalType::Interruptible, RentalType::Reserved}) {
            if (ImGui::Selectable(rentalTypeLabel(t), f.rentalType == t)) {
                f.rentalType = t;
                filtersChanged(app, true);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rental type");

    sameLineIfFits(190 * s);
    std::string gpuPreview = f.gpuModels.empty() ? "All GPUs"
                             : f.gpuModels.size() == 1 ? f.gpuModels.front()
                                                       : std::format("{} GPU models", f.gpuModels.size());
    ImGui::SetNextItemWidth(190 * s);
    if (ImGui::BeginCombo("##gpus", (ICON_FA_MICROCHIP "  " + gpuPreview).c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##gpufilter", ICON_FA_MAGNIFYING_GLASS " Search GPU models", &gpuFilterText_);
        // Always present (disabled when empty) so selecting the first model does not shift the list.
        ImGui::BeginDisabled(f.gpuModels.empty());
        if (ui::SecondaryButton(ICON_FA_XMARK " Clear selection")) {
            f.gpuModels.clear();
            filtersChanged(app);
        }
        ImGui::EndDisabled();
        if (ImGui::IsWindowAppearing() || gpuCatalogDirty_) rebuildGpuCatalog(app);
        // Groups start collapsed; while searching, every group with a match is shown open (without changing the
        // user's own open/closed state).
        const std::string query = fmt::trim(gpuFilterText_);
        const bool searching = !query.empty();
        int shownGroups = 0;
        for (const auto& g : gpuCatalog_) {
            std::vector<const std::string*> visible;
            int selectedCount = 0;
            std::size_t visibleSelected = 0;
            for (const auto& m : g.models) {
                const bool sel = std::find(f.gpuModels.begin(), f.gpuModels.end(), m) != f.gpuModels.end();
                if (sel) ++selectedCount;
                if (!searching || fmt::containsCi(m, query) || fmt::containsCi(gpuSearchNames_[m], query)) {
                    visible.push_back(&m);
                    if (sel) ++visibleSelected;
                }
            }
            if (visible.empty()) continue;
            ++shownGroups;
            const std::string header = selectedCount > 0 ? std::format("{}  ({} selected)", g.name, selectedCount) : g.name;
            // The group's box reflects (and toggles) the models listed under it, i.e. only the matches while searching.
            const ui::CheckState groupState = visibleSelected == 0                ? ui::CheckState::Off
                                              : visibleSelected == visible.size() ? ui::CheckState::On
                                                                                  : ui::CheckState::Mixed;
            bool groupClicked = false;
            const bool groupOpen =
                ui::GroupHeaderCheck(("##gpug_" + g.name).c_str(), header.c_str(), groupState, &groupClicked, searching);
            if (groupClicked) {
                for (const std::string* m : visible) {
                    auto it = std::find(f.gpuModels.begin(), f.gpuModels.end(), *m);
                    if (groupState == ui::CheckState::On) {
                        if (it != f.gpuModels.end()) f.gpuModels.erase(it);
                    } else if (it == f.gpuModels.end()) {
                        f.gpuModels.push_back(*m);
                    }
                }
                filtersChanged(app);
            }
            if (groupOpen) {
                ImGui::Indent();
                for (const std::string* m : visible) {
                    auto it = std::find(f.gpuModels.begin(), f.gpuModels.end(), *m);
                    bool selected = it != f.gpuModels.end();
                    if (ui::SmallCheckbox(m->c_str(), &selected)) {
                        if (selected) {
                            f.gpuModels.push_back(*m);
                        } else {
                            f.gpuModels.erase(it);
                        }
                        filtersChanged(app);
                    }
                }
                ImGui::Unindent();
            }
        }
        if (shownGroups == 0) ui::TextDim("No GPU model matches.");
        ImGui::EndCombo();
    }

    sameLineIfFits(170 * s);
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::BeginCombo("##region", (ICON_FA_GLOBE "  " + regionLabel(f.region)).c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##regionfilter", ICON_FA_MAGNIFYING_GLASS " Filter", &regionFilterText_);
        if (ImGui::Selectable(ICON_FA_GLOBE "  Planet Earth", f.region.empty())) {
            f.region.clear();
            filtersChanged(app, true);
        }
        ImGui::SeparatorText("Continents");
        for (const char* c : continents()) {
            if (!fmt::containsCi(c, regionFilterText_)) continue;
            if (ImGui::Selectable(c, f.region == c)) {
                f.region = c;
                filtersChanged(app, true);
            }
        }
        ImGui::SeparatorText("Countries");
        for (const auto& c : countries()) {
            const std::string label = std::format("{}  {}", c.code, c.name);
            if (!fmt::containsCi(label, regionFilterText_)) continue;
            if (ImGui::Selectable(label.c_str(), f.region == c.code)) {
                f.region = c.code;
                filtersChanged(app, true);
            }
        }
        ImGui::EndCombo();
    }

    sameLineIfFits(220 * s);
    ImGui::SetNextItemWidth(180 * s);
    if (ImGui::BeginCombo("##sort", (ICON_FA_ARROW_DOWN_WIDE_SHORT "  " + std::string(sortOption(f.sortKey).label)).c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        for (const auto& o : sortOptions()) {
            if (ImGui::Selectable(o.label, f.sortKey == o.key)) {
                f.sortKey = o.key;
                f.sortDesc = o.defaultDesc;
                recompute(app);
                filtersChanged(app, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 2 * s);
    if (ui::IconButton(f.sortDesc ? ICON_FA_ARROW_DOWN_WIDE_SHORT : ICON_FA_ARROW_UP_WIDE_SHORT,
                       f.sortDesc ? "Descending (click for ascending)" : "Ascending (click for descending)")) {
        f.sortDesc = !f.sortDesc;
        recompute(app);
        filtersChanged(app, true);
    }

    sameLineIfFits(200 * s);
    Settings& st = app.settings();
    const ImVec4 on = p.accent;
    if (ui::IconButton(ICON_FA_GRIP, "Card view", st.cardView ? &on : nullptr)) {
        st.cardView = true;
        app.markSettingsDirty();
    }
    ImGui::SameLine(0, 0);
    if (ui::IconButton(ICON_FA_TABLE_LIST, "Table view", !st.cardView ? &on : nullptr)) {
        st.cardView = false;
        app.markSettingsDirty();
    }
}

void SearchView::drawFilterPanel(App& app) {
    Settings& st = app.settings();
    SearchFilters& f = st.filters;
    const ui::Palette& p = ui::palette();
    const float fullW = ImGui::GetContentRegionAvail().x;

    // Template box.
    {
        const Template& t = st.currentTemplate;
        ui::BeginCard("##tplcard", ImVec2(0, 0));
        ui::TextDim("Template");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 0.6f);
        if (ui::IconButton(ICON_FA_PEN, "Edit current template")) {
            editTemplate_ = t;
            openTemplateEditor_ = true;
        }
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
        ImGui::TextWrapped("%s", t.name.empty() ? "(custom image)" : t.name.c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::TextWrapped("%s", t.imageWithTag().c_str());
        ImGui::PopStyleColor();
        if (t.usesSsh()) {
            ui::Badge("SSH", p.info);
            ImGui::SameLine();
        }
        if (t.usesJupyter()) {
            ui::Badge("Jupyter", p.warning);
            ImGui::SameLine();
        }
        if (t.isDirect()) {
            ui::Badge("Direct", p.success);
            ImGui::SameLine();
        }
        if (t.runtype == "args") {
            ui::Badge("Entrypoint", p.textDim);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (ImGui::Button(ICON_FA_LAYER_GROUP "  Change Template", ImVec2(-FLT_MIN, 0))) openTemplatePicker_ = true;
        ui::EndCard();
    }
    ImGui::Spacing();

    if (ui::ToggleSwitch("Show Secure Cloud Only", &f.secureCloudOnly)) filtersChanged(app, true);
    ui::HelpMarker("Only datacenter hosts (Secure Cloud).");

    ImGui::SeparatorText("Disk Space");
    {
        float disk = static_cast<float>(f.diskGb);
        ImGui::SetNextItemWidth(fullW * 0.62f);
        if (ImGui::SliderFloat("##disk", &disk, 8.0f, 2048.0f, "%.0f GB", ImGuiSliderFlags_Logarithmic)) {
            f.diskGb = std::round(disk);
            filtersChanged(app);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputDouble("##diskin", &f.diskGb, 0, 0, "%.0f")) {
            f.diskGb = std::clamp(f.diskGb, 1.0, 100000.0);
            filtersChanged(app);
        }
        if (f.templateDiskGb && *f.templateDiskGb > f.diskGb) {
            ui::TextDim(std::format("Template needs {:.0f} GB: prices include {:.0f} GB", *f.templateDiskGb,
                                    f.effectiveDiskGb())
                            .c_str());
        }
        const char* volModes[] = {"No volume", "Link existing volume", "Create new local volume"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##addvol", &rent_.volumeMode, volModes, 3)) {
            if (rent_.volumeMode == 1 && !volumesRes_.loaded) volumesRes_.fetch(app, cmd::showVolumes());
            recompute(app);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add volume: attached when you rent an instance");
        if (rent_.volumeMode == 2) {
            ImGui::SetNextItemWidth(fullW * 0.5f);
            if (ImGui::InputDouble("Volume GB", &rent_.volumeSizeGb, 0, 0, "%.0f")) {
                rent_.volumeSizeGb = std::clamp(rent_.volumeSizeGb, 0.0, 1000000.0);
                recompute(app);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The new volume's cost is included in the shown prices.");
        }
    }

    // Presets: pick one, then Save / Load / Reset.
    ImGui::SeparatorText("Presets");
    {
        if (!selectedPreset_.empty() && !st.presets.contains(selectedPreset_)) selectedPreset_.clear();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float comboW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(comboW);
        const char* preview = selectedPreset_.empty() ? (st.presets.empty() ? "No presets saved" : "Select a preset...")
                                                      : selectedPreset_.c_str();
        // Fixed popup width (= the dropdown): rows sized from the popup's own width would otherwise make an
        // auto-sizing popup grow every frame up to the window border.
        ImGui::SetNextWindowSizeConstraints(ImVec2(comboW, 0.0f), ImVec2(comboW, ImGui::GetFrameHeightWithSpacing() * 12.0f));
        if (ImGui::BeginCombo("##presetsel", preview)) {
            if (st.presets.empty()) ui::TextDim("Save the current filters to create one.");
            std::string toDelete;
            const float trashW = ImGui::CalcTextSize(ICON_FA_TRASH).x + style.FramePadding.x * 2.0f;
            const float rowW = std::max(ImGui::GetFontSize() * 4.0f, ImGui::GetContentRegionAvail().x - trashW - style.ItemSpacing.x);
            for (const auto& [name, preset] : st.presets) {
                ImGui::PushID(name.c_str());
                if (ImGui::Selectable(ui::Ellipsize(name, rowW - style.FramePadding.x).c_str(), name == selectedPreset_, 0, ImVec2(rowW, 0))) {
                    selectedPreset_ = name;
                }
                if (ImGui::IsItemHovered() && ui::Ellipsize(name, rowW - style.FramePadding.x) != name) ImGui::SetTooltip("%s", name.c_str());
                ImGui::SameLine();
                if (ui::IconButton(ICON_FA_TRASH, "Delete this preset")) toDelete = name;
                ImGui::PopID();
            }
            if (!toDelete.empty()) {
                st.presets.erase(toDelete);
                if (selectedPreset_ == toDelete) selectedPreset_.clear();
                app.markSettingsDirty();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saved filter sets");
        const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        if (ui::SecondaryButton(ICON_FA_FLOPPY_DISK " Save", ImVec2(third, 0))) {
            presetName_ = selectedPreset_;
            openSavePreset_ = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current filters as a preset");
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedPreset_.empty());
        if (ui::SecondaryButton(ICON_FA_FOLDER_OPEN " Load", ImVec2(third, 0))) {
            f = st.presets.at(selectedPreset_);
            filtersChanged(app, true);
            app.toasts().info("Loaded preset \"" + selectedPreset_ + "\"");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(selectedPreset_.empty() ? "Select a preset above first" : "Apply the selected preset");
        }
        ImGui::SameLine();
        if (ui::SecondaryButton(ICON_FA_XMARK " Reset", ImVec2(third, 0))) {
            SearchFilters fresh;
            if (auto k = sortKeyFromId(st.defaultSort)) fresh.sortKey = *k;
            fresh.sortDesc = st.defaultSortDesc;
            f = fresh;
            filtersChanged(app, true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset all filters to the defaults");
    }

    ImGui::SeparatorText("Reliability");
    {
        float rel = static_cast<float>(f.minReliability.value_or(0.0) * 100.0);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##rel", &rel, 0.0f, 100.0f, rel <= 0.0f ? "Any" : ">= %.1f%%")) {
            f.minReliability = rel <= 0.0f ? std::nullopt : std::optional<double>(rel / 100.0);
            filtersChanged(app);
        }
    }
    ImGui::SeparatorText("Duration");
    {
        float days = static_cast<float>(f.minDurationDays.value_or(0.0));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##dur", &days, 0.0f, 365.0f, days <= 0.0f ? "Any" : ">= %.1f days",
                               ImGuiSliderFlags_Logarithmic)) {
            f.minDurationDays = days <= 0.05f ? std::nullopt : std::optional<double>(days);
            filtersChanged(app);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum max-instance-duration (time until the host's offer ends)");
    }

    ImGui::SeparatorText("Machines / Hosts");
    {
        auto idField = [&](const char* id, const char* hint, std::string& text) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint(id, hint, &text)) filtersChanged(app);
            const auto parsed = QueryBuilder::parseIdList(text);
            if (!parsed.invalid.empty()) {
                ui::TextWithColor(p.warning, ("Ignored (not a number): " + fmt::join(parsed.invalid, ", ")).c_str());
            }
        };
        idField("##machineids", ICON_FA_SERVER "  Machine IDs, e.g. 12345 67890", f.machineIds);
        idField("##hostids", ICON_FA_USER "  Host IDs, e.g. 256524", f.hostIds);
        ui::TextDim("Only these machines / hosts");
        ui::HelpMarker("Searches vast.ai for exactly these machines or hosts (IDs separated by spaces or commas), so "
                       "they are found even when they are not in the normal random sample. The other filters still "
                       "apply - if a machine is missing, tick Unverified Machines / Unavailable Offers or reset "
                       "filters. Leave empty to search all machines.");
    }

    ImGui::SeparatorText("Machine Options");
    {
        const std::string favLabel = std::format(ICON_FA_STAR " Favorites Only ({})###favonly", st.favoriteMachines.size());
        if (ImGui::Checkbox(favLabel.c_str(), &f.favoritesOnly)) filtersChanged(app, true);
        ui::HelpMarker("Search only your favorite machines (they are asked from vast.ai directly, so they are found "
                       "even outside the random sample). The other filters still apply.\n\nAdd favorites with the star "
                       "button on the Instances page or an offer's right-click menu; manage the list in Settings > "
                       "Favorites. Favorite offers are marked with a gold star.");
    }
    if (ImGui::Checkbox("Unverified Machines", &f.showUnverified)) filtersChanged(app, true);
    ImGui::Indent();
    ImGui::BeginDisabled(!f.showUnverified);
    if (ImGui::Checkbox("Deverified Machines", &f.showDeverified)) {
        recompute(app);
        app.markSettingsDirty();
    }
    ImGui::EndDisabled();
    ImGui::Unindent();
    ui::HelpMarker("Also show machines whose verification was revoked. Requires 'Unverified Machines'.");
    if (ImGui::Checkbox("Incompatible Machines", &f.showIncompatible)) filtersChanged(app, true);
    {
        std::string help = "Show machines that do not meet the selected template's requirements; they are marked "
                           "INCOMPATIBLE. When off they are hidden.\n\n";
        const auto reqs = describeRequirements(f.templateRequirements);
        if (reqs.empty()) {
            help += "The selected template has no requirements (no CUDA version in the image tag, no extra filters), "
                    "so no offer is incompatible.";
        } else {
            help += "Template requirements:";
            for (const auto& r : reqs) help += "\n  " + r;
        }
        ui::HelpMarker(help.c_str());
    }
    if (ImGui::Checkbox("Machines Rented by Me", &f.showRentedByMe)) {
        app.markSettingsDirty();
        recompute(app);  // client-side filter: no new request needed
    }
    ui::HelpMarker("Show offers on machines where you already have an instance (running, loading or stopped). "
                   "They are marked RENTED. When off they are hidden.");
    if (ImGui::Checkbox("Defective Machines", &f.showDefective)) {
        recompute(app);
        app.markSettingsDirty();
    }
    ui::HelpMarker("Show defective machines, marked with a red 'defective' tag and border. When off they are hidden.\n\n"
                   "Defective: low reliability, missing inet/flops/dlperf/disk/CUDA values or performance far below "
                   "the GPU model median. Thresholds are configurable in Settings.");
    // Always rendered (also when 0) so the panel layout never jumps.
    ImGui::Indent();
    ImGui::TextColored(defectiveCount_ > 0 && !f.showDefective ? p.warning : p.textDim, "%d defective offers %s",
                       defectiveCount_, f.showDefective ? "shown" : "hidden");
    ImGui::Unindent();
    if (ImGui::Checkbox("Blacklisted Machines", &f.showBlacklisted)) {
        recompute(app);
        app.markSettingsDirty();
    }
    ui::HelpMarker("Show offers on machines and hosts you blacklisted, marked with a grey 'blacklisted' tag and border. "
                   "When off they are hidden.\n\nBlacklist machines from the Instances page or an offer's right-click "
                   "menu; manage the list in Settings > Blacklist.");
    ImGui::Indent();
    ImGui::TextColored(p.textDim, "%d blacklisted offers %s", blacklistedCount_, f.showBlacklisted ? "shown" : "hidden");
    ImGui::Unindent();
    if (ImGui::Checkbox("Unavailable Offers", &f.showUnavailable)) filtersChanged(app, true);
    if (ImGui::Checkbox("Static IP Address", &f.staticIpOnly)) filtersChanged(app, true);

    ImGui::SeparatorText("Versions");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##cuda", "Min CUDA version (e.g. 12.4)", &f.minCuda)) filtersChanged(app);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##driver", "Min driver version (e.g. 535.104.05)", &f.minDriver)) filtersChanged(app);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##ubuntu", "Min Ubuntu version (e.g. 22.04)", &f.minUbuntu)) filtersChanged(app);

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS "  Ranges", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& m : rangeMetas()) {
            Range& r = f.range(m.field);
            ImGui::TextUnformatted(m.label);
            const std::string label = rangeLabel(m, r);
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(label.c_str()).x));
            ui::TextWithColor(r.active() ? p.accent : p.textDim, label.c_str());
            double lo = r.min.value_or(m.lo);
            double hi = r.max.value_or(m.hi);
            if (ui::RangeSlider(m.id, &lo, &hi, m.lo, m.hi, m.decimals, m.logarithmic)) {
                r.min = lo > m.lo ? std::optional<double>(lo) : std::nullopt;
                r.max = hi < m.hi ? std::optional<double>(hi) : std::nullopt;
                filtersChanged(app);
            }
        }
    }
}

void SearchView::drawResults(App& app) {
    const ui::Palette& p = ui::palette();

    // One fixed-height status line: content changes in place, nothing below it moves. The Search button leads it,
    // so it stays in the same place whatever the window width.
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Search", loading_)) search(app);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%zu offers", visible_.size());
    if (ImGui::IsItemHovered() && mergedRequests_ > 0) {
        ImGui::SetTooltip("%zu distinct offers merged from %d identical requests (%zu offers received).\n"
                          "vast.ai returns a random sample of the matching offers per request; merging several\n"
                          "makes the result more complete and stable. Adjust in Settings > Backend.",
                          offers_.size(), mergedRequests_, receivedOffers_);
    }
    ImGui::SameLine();
    if (loading_) {
        ui::TextDim(lastSearchAt_ >= 0 && !offers_.empty() ? "- updating..." : "- searching...");
    } else if (lastSearchAt_ >= 0) {
        ui::TextDim(std::format("- updated {:.0f}s ago", ImGui::GetTime() - lastSearchAt_).c_str());
    }
    if (mergedRequests_ > 1) {
        ImGui::SameLine();
        ui::TextDim(std::format("- merged from {} requests", mergedRequests_).c_str());
    }
    if (app.apiKey().key.empty()) {
        ImGui::SameLine();
        ui::TextWithColor(p.warning, ICON_FA_KEY " max 64 per request");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Without an API key vast.ai returns at most 64 offers per request, regardless of the limit.\n"
                              "Set a key in Settings to get up to %d per request.",
                              app.settings().searchLimit);
        }
    }
    if (hiddenDeverified_ > 0) {
        ImGui::SameLine();
        ui::TextDim(std::format("- {} deverified hidden", hiddenDeverified_).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Tick 'Unverified Machines' and 'Deverified Machines' in Machine Options to show them.");
        }
    }
    if (hiddenRented_ > 0) {
        ImGui::SameLine();
        ui::TextDim(std::format("- {} rented by you hidden", hiddenRented_).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Offers on machines where you already have an instance.\n"
                              "Tick 'Machines Rented by Me' in Machine Options to show them.");
        }
    }
    if (hiddenIncompatible_ > 0) {
        ImGui::SameLine();
        ui::TextDim(std::format("- {} incompatible hidden", hiddenIncompatible_).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Offers that fail the selected template's requirements.\n"
                              "Tick 'Incompatible Machines' in Machine Options to show them.");
        }
    }
    if (defectiveCount_ > 0 && !app.settings().filters.showDefective) {
        ImGui::SameLine();
        ui::TextDim(std::format("- {} defective hidden", defectiveCount_).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tick 'Defective Machines' in Machine Options to show them.");
    }
    if (blacklistedCount_ > 0 && !app.settings().filters.showBlacklisted) {
        ImGui::SameLine();
        ui::TextDim(std::format("- {} blacklisted hidden", blacklistedCount_).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tick 'Blacklisted Machines' in Machine Options to show them.");
    }
    {
        const SearchFilters& sf = app.settings().filters;
        if (sf.favoritesOnly) {
            const std::size_t n = app.settings().favoriteMachines.size();
            ImGui::SameLine();
            ui::TextWithColor(p.caution, n == 0 ? ICON_FA_STAR " favorites only - no favorites yet"
                                                : std::format(ICON_FA_STAR " favorites only ({} machine{})", n, n == 1 ? "" : "s").c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Only offers on your favorite machines are searched and shown.\n"
                                  "Untick 'Favorites Only' in Machine Options to search all machines.%s",
                                  n == 0 ? "\n\nAdd favorites with the star button on the Instances page or an offer's "
                                           "right-click menu."
                                         : "");
            }
        }
        const std::size_t ids = QueryBuilder::parseIdList(sf.machineIds).ids.size() + QueryBuilder::parseIdList(sf.hostIds).ids.size();
        if (ids > 0) {
            ImGui::SameLine();
            ui::TextWithColor(p.accent, std::format("- only {} machine / host ID{}", ids, ids == 1 ? "" : "s").c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear 'Machines / Hosts' in the filter panel to search all machines.");
        }
    }
    if (!warnings_.empty()) {
        ImGui::SameLine();
        ui::TextWithColor(p.warning, std::format(ICON_FA_TRIANGLE_EXCLAMATION " {}", warnings_.size()).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(warnings_, "\n").c_str());
    }
    if (!error_.empty()) {
        ImGui::SameLine();
        std::string e = error_;
        if (const auto nl = e.find('\n'); nl != std::string::npos) e.resize(nl);
        if (e.size() > 90) e = e.substr(0, 87) + "...";
        ui::TextWithColor(p.danger, (ICON_FA_CIRCLE_XMARK " " + e).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", error_.c_str());
    }

    if (offers_.empty()) {
        ImGui::Spacing();
        ui::TextDim(loading_ ? "Searching offers..." : "No offers match your filters. Try relaxing them or press Search.");
        return;
    }
    if (app.settings().cardView) {
        drawCards(app);
    } else {
        drawTable(app);
    }
}

void SearchView::drawCards(App& app) {
    const Settings& st = app.settings();
    ImGui::BeginChild("##cards", ImVec2(0, 0), ImGuiChildFlags_None);
    ui::OfferCardContext ctx;
    ctx.currency = &st.currency;
    ctx.interruptible = st.filters.rentalType == RentalType::Interruptible;
    ctx.diskGb = st.filters.effectiveDiskGb();
    ctx.rentNow = st.filters.rentalType == RentalType::OnDemand;
    // Cards share one height so the list can be clipped; it is the measured content height of the cards
    // drawn last frame (all cards have the same layout), with a formula-based estimate on the first frame.
    float measured = 0.0f;
    ctx.measuredHeight = &measured;
    ctx.height = cardHeight_ > 0.0f ? cardHeight_ : ui::OfferCardHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_.size()), ctx.height + ImGui::GetStyle().ItemSpacing.y);
    std::optional<std::pair<ui::OfferAction, std::size_t>> pending;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::size_t idx = visible_[static_cast<std::size_t>(i)];
            ctx.rentedByMe = (offers_[idx].machineId && rentedMachines_.contains(*offers_[idx].machineId)) ||
                             rentedOffers_.contains(offers_[idx].id);
            ctx.renting = rentingOffers_.contains(offers_[idx].id);
            ctx.favorite = offers_[idx].machineId && st.favoriteMachines.contains(*offers_[idx].machineId);
            ctx.incompatible = isIncompatible(offers_[idx], st.filters);
            if (ctx.incompatible) ctx.incompatibleWhy = incompatibleReason(offers_[idx], st.filters);
            const auto a = ui::OfferCard(offers_[idx], metrics_[idx], defects_[idx], ctx);
            if (a != ui::OfferAction::None) pending = std::pair{a, idx};
        }
    }
    ImGui::EndChild();
    if (measured > 0.0f) cardHeight_ = std::ceil(measured);
    if (pending) {
        const Offer o = offers_[pending->second];
        handleAction(app, pending->first, o);
    }
}

void SearchView::drawTable(App& app) {
    Settings& st = app.settings();
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##offertable", ColCount, flags)) return;
    ImGui::TableSetupScrollFreeze(2, 1);
    for (int c = 0; c < ColCount; ++c) {
        ImGuiTableColumnFlags cf = ImGuiTableColumnFlags_WidthFixed;
        if (kOfferColumns[c].defaultHidden) cf |= ImGuiTableColumnFlags_DefaultHide;
        if (c == ColRent) cf |= ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide;
        ImGui::TableSetupColumn(kOfferColumns[c].name, cf, kOfferColumns[c].width * uiScale(app));
    }
    ImGui::TableHeadersRow();

    if (!tableColumnsApplied_ && st.offerColumns.size() == ColCount) {
        for (int c = 0; c < ColCount; ++c) ImGui::TableSetColumnEnabled(c, st.offerColumns[static_cast<std::size_t>(c)]);
        tableColumnsApplied_ = true;
    } else {
        for (int c = 0; c < ColCount; ++c) {
            const bool en = (ImGui::TableGetColumnFlags(c) & ImGuiTableColumnFlags_IsEnabled) != 0;
            if (static_cast<std::size_t>(c) < st.offerColumns.size() && st.offerColumns[static_cast<std::size_t>(c)] != en) {
                st.offerColumns[static_cast<std::size_t>(c)] = en;
                app.markSettingsDirty();
            }
        }
    }

    if (syncTableSort_) {
        syncTableSort_ = false;
        const auto col = sortColumnFor(st.filters.sortKey);
        if (col) {
            ImGui::TableSetColumnSortDirection(*col, st.filters.sortDesc ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending, false);
        } else if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            ImGui::TableSetColumnSortDirection(specs->Specs[0].ColumnIndex, ImGuiSortDirection_None, false);
        }
    }
    int sortCol = -1;
    bool sortDesc = false;
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        const int c = specs->Specs[0].ColumnIndex;
        const bool desc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        sortCol = c;
        sortDesc = desc;
        // Header clicks on columns that have a server-side equivalent drive the search sort too.
        if (specs->SpecsDirty) {
            specs->SpecsDirty = false;
            const std::optional<SortKey> key = sortKeyForColumn(c);
            if (key && (st.filters.sortKey != *key || st.filters.sortDesc != desc)) {
                st.filters.sortKey = *key;
                st.filters.sortDesc = desc;
                recompute(app);
                filtersChanged(app);
            }
        }
    }
    // Sorting ~1000+ rows is only redone when the results or the sort column change, not every frame.
    if (tableRowsDirty_ || sortCol != tableSortCol_ || sortDesc != tableSortDesc_) {
        tableRows_ = visible_;
        if (sortCol >= 0) {
            const int c = sortCol;
            const bool desc = sortDesc;
            std::stable_sort(tableRows_.begin(), tableRows_.end(), [&](std::size_t a, std::size_t b) {
                const auto na = numericCell(c, offers_[a], metrics_[a]);
                const auto nb = numericCell(c, offers_[b], metrics_[b]);
                if (na || nb) {
                    if (!na || !nb) return na.has_value();
                    return desc ? *na > *nb : *na < *nb;
                }
                const auto sa = textCell(c, offers_[a], metrics_[a], st.currency);
                const auto sb = textCell(c, offers_[b], metrics_[b], st.currency);
                return desc ? sa > sb : sa < sb;
            });
        }
        tableRowsDirty_ = false;
        tableSortCol_ = sortCol;
        tableSortDesc_ = sortDesc;
    }
    const std::vector<std::size_t>& rows = tableRows_;

    std::optional<std::pair<ui::OfferAction, std::size_t>> pending;
    const bool rentNow = st.filters.rentalType == RentalType::OnDemand;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::size_t idx = rows[static_cast<std::size_t>(i)];
            const Offer& o = offers_[idx];
            const OfferMetrics& m = metrics_[idx];
            const bool defective = defects_[idx].defective();
            const bool incompatible = isIncompatible(o, st.filters);
            const bool rentedByMe = (o.machineId && rentedMachines_.contains(*o.machineId)) || rentedOffers_.contains(o.id);
            const bool favorite = o.machineId && st.favoriteMachines.contains(*o.machineId);
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(idx));
            if (rentedByMe) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ui::col(ui::palette().info, 0.10f));
            for (int c = 0; c < ColCount; ++c) {
                if (!ImGui::TableSetColumnIndex(c)) continue;
                if (c == ColId) {
                    // Favorite machines: gold ID with a star.
                    if (favorite) ImGui::PushStyleColor(ImGuiCol_Text, ui::palette().caution);
                    ui::TableRowSelectable(favorite ? (ICON_FA_STAR " " + textCell(c, o, m, st.currency)).c_str()
                                                    : textCell(c, o, m, st.currency).c_str(),
                                           false);
                    if (favorite) ImGui::PopStyleColor();
                    if (ImGui::BeginPopupContextItem("##rowctx")) {
                        if (auto a = ui::OfferContextMenuItems(o, !rentedByMe, favorite); a != ui::OfferAction::None) pending = std::pair{a, idx};
                        ImGui::EndPopup();
                    }
                } else if (c == ColRent) {
                    if (rentedByMe) {
                        ImGui::BeginDisabled();
                        ImGui::PushStyleColor(ImGuiCol_Text, ui::palette().info);
                        ImGui::SmallButton("RENTED");
                        ImGui::PopStyleColor();
                        ImGui::EndDisabled();
                    } else if (rentingOffers_.contains(o.id)) {
                        ui::SpinnerText("##renting", "renting");
                    } else if (ImGui::SmallButton("RENT")) {
                        pending = std::pair{rentNow ? ui::OfferAction::RentNow : ui::OfferAction::Rent, idx};
                    }
                } else if (c == ColTflopsPerDollar && m.rankTflopsPerDollar) {
                    ui::TextWithColor(ui::rankColor(*m.rankTflopsPerDollar), textCell(c, o, m, st.currency).c_str());
                } else if (c == ColDlpPerDollar && m.rankDlpPerDollar) {
                    ui::TextWithColor(ui::rankColor(*m.rankDlpPerDollar), textCell(c, o, m, st.currency).c_str());
                } else if (c == ColVerification) {
                    ui::TextWithColor(ui::verificationColor(o.verification), o.verification.c_str());
                    if (defective) {
                        ImGui::SameLine();
                        ui::TextWithColor(ui::palette().danger, "defective");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(defects_[idx].reasons, "\n").c_str());
                    }
                    if (defects_[idx].blacklisted()) {
                        ImGui::SameLine();
                        ui::TextWithColor(ui::palette().textDim, "blacklisted");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(defects_[idx].blacklistReasons, "\n").c_str());
                    }
                } else if (c == ColPrice) {
                    ImGui::TextUnformatted(textCell(c, o, m, st.currency).c_str());
                    if (ImGui::IsItemHovered()) ui::OfferPriceTooltip(o, st.currency, st.filters.effectiveDiskGb());
                } else if (c == ColGpu && incompatible) {
                    ui::TextWithColor(ui::palette().danger, textCell(c, o, m, st.currency).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Incompatible with the template:\n%s", incompatibleReason(o, st.filters).c_str());
                } else if (c == ColCuda && incompatible && incompatibleReason(o, st.filters).find("cuda_max_good") != std::string::npos) {
                    ui::TextWithColor(ui::palette().danger, textCell(c, o, m, st.currency).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", incompatibleReason(o, st.filters).c_str());
                } else {
                    ImGui::TextUnformatted(textCell(c, o, m, st.currency).c_str());
                }
                if (c == ColGpu && defective && !incompatible && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", fmt::join(defects_[idx].reasons, "\n").c_str());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    if (pending) {
        const Offer o = offers_[pending->second];
        handleAction(app, pending->first, o);
    }
}

void SearchView::drawRentModal(App& app) {
    const ui::Palette& p = ui::palette();
    const float s = uiScale(app);
    // Fixed width; height follows the content (the template section can be expanded/collapsed), capped to the app window.
    const float dlgW = 720 * s;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(ImVec2(dlgW, 0), ImVec2(dlgW, vp->WorkSize.y - 40 * s));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ui::BeginDialog("Rent instance###rent", ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    {
        // Keep the whole dialog on screen when it grows.
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float maxY = vp->WorkPos.y + vp->WorkSize.y - size.y - 20 * s;
        if (pos.y > maxY) ImGui::SetWindowPos(ImVec2(pos.x, std::max(vp->WorkPos.y + 20 * s, maxY)));
    }
    RentState& r = rent_;
    const Offer& o = r.offer;
    const bool interruptible = app.settings().filters.rentalType == RentalType::Interruptible;

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::Text("%dx %s", o.numGpus, o.gpuName.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ui::TextDim(std::format("offer {}  machine {}  {}  {}/hr", o.id, o.machineId.value_or(0), o.geolocation,
                            fmt::money(o.priceHr, app.settings().currency))
                    .c_str());
    ImGui::Separator();

    const float labelW = 130 * s;
    auto label = [&](const char* t) {
        ImGui::AlignTextToFramePadding();
        ui::TextDim(t);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    // Rental basics stay visible; the template configuration is folded away below.
    label("Template");
    {
        const char* kChange = ICON_FA_LAYER_GROUP "  Change...";
        const float changeW = ImGui::CalcTextSize(kChange).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const char* mode = r.launchMode == 1 ? "Jupyter" : r.launchMode == 2 ? "Entrypoint" : "SSH";
        std::string summary = std::format("{}  -  {}  -  {}{}", r.templateName.empty() ? "Custom image" : r.templateName,
                                          r.image.empty() ? std::string("(template image)") : r.image, mode,
                                          r.launchMode != 2 && r.direct ? " direct" : "");
        const float maxW = ImGui::GetContentRegionAvail().x - changeW - ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::CalcTextSize(summary.c_str()).x > maxW) {
            while (!summary.empty() && ImGui::CalcTextSize((summary + "...").c_str()).x > maxW) summary.pop_back();
            summary += "...";
        }
        ImGui::TextUnformatted(summary.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%zu env vars, %zu ports%s%s\nExpand 'Template settings' below to change them.", r.env.size(),
                              r.ports.size(), r.onstart.empty() ? "" : ", on-start script",
                              r.useTemplateHash ? "\nLaunched with --template_hash" : "");
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - changeW);
        if (ui::SecondaryButton(kChange)) {
            // Opened inside this dialog so it stacks on top of it; applies to this rental only.
            pickerForRent_ = true;
            prepareTemplatePicker(app);
            ImGui::OpenPopup("Select template###tplpick");
        }
        // The chosen template may need more than this machine offers (e.g. a newer CUDA).
        const auto unmet = unmetRequirements(o, templateRequirements(r.templateFilters, r.image));
        if (!unmet.empty()) {
            ImGui::SetCursorPosX(labelW);
            ui::TextWithColor(p.danger, ICON_FA_BAN " This machine does not meet the template's requirements");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(unmet, "\n").c_str());
        }
    }
    label("Disk (GB)");
    ImGui::InputDouble("##rdisk", &r.diskGb, 1, 10, "%.0f");
    r.diskGb = std::clamp(r.diskGb, 1.0, 100000.0);
    label("Label");
    ImGui::InputTextWithHint("##rlabel", "optional", &r.label);

    if (interruptible) {
        label("Bid ($/hr)");
        ImGui::InputDouble("##bid", &r.bidPrice, 0.01, 0.1, "%.4f");
        ImGui::SetCursorPosX(labelW);
        ui::TextDim(std::format("Minimum bid: {}/hr  (default: +5 % like the vast.ai console)",
                                fmt::money(o.minBid, app.settings().currency, 4))
                        .c_str());
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP "  Template settings (image, launch mode, env, ports, on-start)")) {
        ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
        label("Image");
        ImGui::InputText("##image", &r.image);
        if (!r.templateHash.empty()) {
            ImGui::SetCursorPosX(labelW);
            ImGui::Checkbox(("Launch from template hash " + r.templateHash).c_str(), &r.useTemplateHash);
            ui::HelpMarker("Uses the template's settings server-side (--template_hash). Fields below still override.");
        }

        label("Launch mode");
        ImGui::RadioButton("SSH", &r.launchMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Jupyter", &r.launchMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Entrypoint / args", &r.launchMode, 2);
        if (r.launchMode != 2) {
            ImGui::SameLine();
            ImGui::Checkbox("Direct connection", &r.direct);
        } else {
            label("Args");
            ImGui::InputTextWithHint("##rargs", "arguments passed to the image entrypoint", &r.args);
        }

        ImGui::SeparatorText("Environment variables");
        int removeEnv = -1;
        for (std::size_t i = 0; i < r.env.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(200 * s);
            ImGui::InputTextWithHint("##k", "NAME", &r.env[i].first);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.5f);
            ImGui::InputTextWithHint("##v", "value", &r.env[i].second);
            ImGui::SameLine();
            if (ui::IconButton(ICON_FA_XMARK, "Remove")) removeEnv = static_cast<int>(i);
            ImGui::PopID();
        }
        if (removeEnv >= 0) r.env.erase(r.env.begin() + removeEnv);
        if (ui::SecondaryButton(ICON_FA_PLUS " Add variable")) r.env.emplace_back();

        ImGui::SeparatorText("Ports");
        int removePort = -1;
        for (std::size_t i = 0; i < r.ports.size(); ++i) {
            ImGui::PushID(1000 + static_cast<int>(i));
            ImGui::SetNextItemWidth(160 * s);
            ImGui::InputTextWithHint("##port", "8080:8080", &r.ports[i]);
            ImGui::SameLine();
            if (ui::IconButton(ICON_FA_XMARK, "Remove")) removePort = static_cast<int>(i);
            ImGui::PopID();
            if ((i + 1) % 3 != 0 && i + 1 < r.ports.size()) ImGui::SameLine();
        }
        if (removePort >= 0) r.ports.erase(r.ports.begin() + removePort);
        if (ui::SecondaryButton(ICON_FA_PLUS " Add port")) r.ports.emplace_back();
        label("Docker options");
        ImGui::InputTextWithHint("##extra", "additional docker run options, e.g. --shm-size=8g", &r.extraDocker);

        ImGui::SeparatorText("On-start script");
        if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
        ImGui::InputTextMultiline("##onstart", &r.onstart, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f));
        if (ui::monoFont()) ImGui::PopFont();
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    }

    ImGui::SeparatorText("Volume");
    const char* volModes[] = {"No volume", "Link existing volume", "Create new local volume"};
    label("Volume");
    if (ImGui::Combo("##rvol", &r.volumeMode, volModes, 3) && r.volumeMode == 1 && !volumesRes_.loaded) {
        volumesRes_.fetch(app, cmd::showVolumes());
    }
    if (r.volumeMode == 1) {
        label("Existing volume");
        std::string preview = r.volumeId ? std::to_string(r.volumeId) : std::string("select...");
        if (ImGui::BeginCombo("##volsel", preview.c_str())) {
            if (volumesRes_.loading) ui::SpinnerText("##vl", "loading volumes...");
            for (const auto& v : jsonutil::extractArray(volumesRes_.data, {"volumes"})) {
                const auto id = jsonutil::optInt(v, "id").value_or(0);
                const std::string t = std::format("{}  {} GB  {}", id, jsonutil::num(v, "disk_space", jsonutil::num(v, "size")),
                                                  jsonutil::str(v, "status"));
                if (ImGui::Selectable(t.c_str(), r.volumeId == id)) r.volumeId = id;
            }
            ImGui::EndCombo();
        }
        if (o.raw.contains("avail_vol_ask_id")) {
            ImGui::SetCursorPosX(labelW);
            ui::TextDim("Volumes must live on the same machine as the instance.");
        }
    } else if (r.volumeMode == 2) {
        label("Size (GB)");
        ImGui::InputDouble("##rvolsize", &r.volumeSizeGb, 1, 10, "%.0f");
        const auto ask = jsonutil::optInt(o.raw, "avail_vol_ask_id");
        ImGui::SetCursorPosX(labelW);
        if (ask) {
            ui::TextDim(std::format("Uses this machine's volume offer {}", *ask).c_str());
        } else {
            ui::TextWithColor(p.warning, "This offer has no local volume offer available.");
        }
    }
    if (r.volumeMode != 0) {
        label("Mount path");
        ImGui::InputText("##mount", &r.mountPath);
    }
    ImGui::SetCursorPosX(labelW);
    ImGui::Checkbox("Fail if the machine is unavailable (--cancel-unavail)", &r.cancelUnavail);

    const ApiCall call = buildRentCall(r, interruptible);

    ImGui::Separator();
    ImGui::Spacing();
    const float bw = 150 * s;
    ImGui::BeginDisabled(r.image.empty() && !r.useTemplateHash);
    if (ui::BusyButton(ICON_FA_ROCKET, "Rent now", r.submitting, true, ImVec2(bw, 0)) && !r.submitting) {
        r.submitting = true;
        app.client().submit(call, [this, &app](const ApiResult& res) {
            rent_.submitting = false;
            if (!res.ok) return;
            const auto id = jsonutil::optInt(res.data, "new_contract");
            app.toasts().success("Instance created", id ? "Instance ID " + std::to_string(*id) : res.text);
            app.refreshInstances();
            rent_.done = true;
        });
    }
    ImGui::EndDisabled();
    if (r.done) {
        r.done = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::SecondaryButton("Close", ImVec2(bw, 0))) ImGui::CloseCurrentPopup();
    drawTemplatePicker(app);  // nested: opened by "Change..." above
    ImGui::EndPopup();
}

void SearchView::loadTemplateTab(App& app, int tab, bool force) {
    Resource& r = tplRes_[static_cast<std::size_t>(tab)];
    if (r.loading || (r.loaded && !force)) return;
    if (tab == 1) {
        fetchMyTemplates(app, r);
    } else if (tab == 0) {
        r.fetch(app, cmd::searchTemplates("recommended=true"));
    } else {
        r.fetch(app, cmd::searchTemplates(fmt::trim(templateQuery_)));
    }
}

void SearchView::drawTemplatePicker(App& app) {
    const float s = uiScale(app);
    ImGui::SetNextWindowSize(ImVec2(920 * s, 600 * s), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ui::BeginDialog("Select template###tplpick")) return;

    // Display order: My templates first (resource indices stay 0 recommended, 1 mine, 2 search).
    constexpr int kOrder[] = {1, 0, 2};
    constexpr const char* kTabs[] = {ICON_FA_STAR " Recommended", ICON_FA_USER " My templates",
                                     ICON_FA_MAGNIFYING_GLASS " Search"};
    if (ImGui::BeginTabBar("##tpltabs")) {
        for (const int t : kOrder) {
            const ImGuiTabItemFlags tf = tplTabRequest_ == t ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kTabs[t], nullptr, tf)) {
                if (tplTab_ != t) {
                    tplTab_ = t;
                    loadTemplateTab(app, t, false);
                }
                ImGui::EndTabItem();
            }
        }
        tplTabRequest_ = -1;
        ImGui::EndTabBar();
    }
    Resource& res = tplRes_[static_cast<std::size_t>(tplTab_)];
    ui::JsonTableState& table = tplTables_[static_cast<std::size_t>(tplTab_)];

    // Fixed toolbar: the same widgets on every tab, so switching tabs never shifts the table.
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", res.loading, false)) loadTemplateTab(app, tplTab_, true);
    ImGui::SameLine();
    ImGui::BeginDisabled(tplTab_ != 2);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    if (ImGui::InputTextWithHint("##tplq", "query, e.g. name=PyTorch  image=vastai/comfy", &templateQuery_,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        loadTemplateTab(app, 2, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (tplTab_ == 1 && app.apiKey().key.empty()) {
        ui::TextWithColor(ui::palette().warning, ICON_FA_KEY " Set an API key in Settings to list your templates.");
    } else if (!res.error.empty()) {
        ui::TextWithColor(ui::palette().danger, (ICON_FA_CIRCLE_XMARK " " + res.error.substr(0, 100)).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", res.error.c_str());
    } else {
        ui::TextDim("Click a row, then 'Use selected' (or double-click).");
    }

    const json rows = jsonutil::extractArray(res.data, {"templates"});
    std::optional<int> chosen;
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"name", "image", "tag", "private", "recommended", "hash_id", "id", "count_created", "runtype",
                             "ssh_direct", "jup_direct"};
    opts.height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f -
                  ImGui::GetStyle().ItemSpacing.y * 2.0f;
    opts.rowMenu = [&](const json&, int idx) {
        if (ImGui::MenuItem(ICON_FA_CHECK "  Use this template")) chosen = idx;
    };
    if (auto dbl = ui::JsonTable("##tpltable", rows, table, opts)) chosen = dbl;

    const bool hasSel = table.selected >= 0 && static_cast<std::size_t>(table.selected) < rows.size();
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button(ICON_FA_CHECK "  Use selected")) chosen = table.selected;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!pickerForRent_ && ui::SecondaryButton(ICON_FA_CUBE " Use custom image...")) {
        editTemplate_ = app.settings().currentTemplate;
        editTemplate_.hashId.clear();
        editTemplate_.id.reset();
        editTemplate_.name = "Custom image";
        ImGui::CloseCurrentPopup();
        openTemplateEditor_ = true;
    }
    if (!pickerForRent_) ImGui::SameLine();
    if (ui::SecondaryButton("Close")) ImGui::CloseCurrentPopup();

    if (chosen && *chosen >= 0 && static_cast<std::size_t>(*chosen) < rows.size()) {
        const Template t = rows[static_cast<std::size_t>(*chosen)].get<Template>();
        if (pickerForRent_) {
            applyTemplateToRent(app, rent_, t);  // this rental only; the Search page keeps its template
        } else {
            app.setCurrentTemplate(t);
            app.toasts().info("Template selected", t.name);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SearchView::drawTemplateEditor(App& app) {
    const float s = uiScale(app);
    ImGui::SetNextWindowSize(ImVec2(640 * s, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ui::BeginDialog("Edit template###tpledit")) return;
    Template& t = editTemplate_;
    const float labelW = 140 * s;
    auto label = [&](const char* txt) {
        ImGui::AlignTextToFramePadding();
        ui::TextDim(txt);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    label("Name");
    ImGui::InputText("##tname", &t.name);
    label("Image");
    ImGui::InputText("##timage", &t.image);
    label("Tag");
    ImGui::InputText("##ttag", &t.tag);
    label("Docker options");
    ImGui::InputTextWithHint("##tenv", "-e KEY=value -p 8080:8080", &t.env);
    label("Launch mode");
    int mode = t.usesJupyter() ? 1 : (t.runtype == "args" ? 2 : 0);
    bool modeChanged = ImGui::RadioButton("SSH", &mode, 0);
    ImGui::SameLine();
    modeChanged |= ImGui::RadioButton("Jupyter", &mode, 1);
    ImGui::SameLine();
    modeChanged |= ImGui::RadioButton("Args", &mode, 2);
    bool direct = t.isDirect();
    ImGui::SameLine();
    const bool directChanged = ImGui::Checkbox("Direct", &direct);
    if (modeChanged || directChanged) {
        t.runtype = mode == 1 ? "jupyter" : (mode == 2 ? "args" : "ssh");
        t.useSsh = mode != 2;
        t.sshDirect = direct && mode != 2;
        t.jupyterDirect = direct && mode == 1;
    }
    label("Recommended disk");
    double disk = t.recommendedDiskGb.value_or(0.0);
    if (ImGui::InputDouble("##tdisk", &disk, 1, 10, "%.0f GB")) t.recommendedDiskGb = disk > 0 ? std::optional(disk) : std::nullopt;
    ImGui::TextUnformatted("On-start script");
    if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
    ImGui::InputTextMultiline("##tonstart", &t.onstart, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5));
    if (ui::monoFont()) ImGui::PopFont();
    if (!t.hashId.empty()) {
        ui::TextDim(("Template hash: " + t.hashId + " (local edits apply to new rentals only)").c_str());
    }
    if (ImGui::Button(ICON_FA_CHECK "  Use", ImVec2(120 * s, 0))) {
        app.setCurrentTemplate(t);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::SecondaryButton("Cancel", ImVec2(120 * s, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SearchView::drawPresetPopups(App& app) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ui::BeginDialog("Save filter preset###savepreset", ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("Preset name");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
    const bool enter = ImGui::InputText("##pname", &presetName_, ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string name = fmt::trim(presetName_);
    if (app.settings().presets.contains(name)) ui::TextWithColor(ui::palette().warning, "Overwrites the existing preset.");
    ImGui::BeginDisabled(name.empty());
    if (ImGui::Button("Save", ImVec2(ImGui::GetFontSize() * 7, 0)) || (enter && !name.empty())) {
        app.settings().presets[name] = app.settings().filters;
        selectedPreset_ = name;
        app.markSettingsDirty();
        app.toasts().success("Preset \"" + name + "\" saved");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui::SecondaryButton("Cancel", ImVec2(ImGui::GetFontSize() * 7, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SearchView::drawFloating(App& app) {
    (void)app;
    for (auto it = jsonWindows_.begin(); it != jsonWindows_.end();) {
        bool open = true;
        const std::string title = std::format("Offer {} JSON###offerjson{}", it->first, it->first);
        ImGui::SetNextWindowSize(ImVec2(520, 620), ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin(title.c_str(), &open);
        if (!ImGui::IsWindowDocked() && ui::ClickedOutsideCurrentWindow()) open = false;
        if (visible) {
            if (ImGui::Button(ICON_FA_COPY " Copy JSON")) ImGui::SetClipboardText(it->second.dump(2).c_str());
            ui::JsonText("##json", it->second);
        }
        ImGui::End();
        if (!open) {
            it = jsonWindows_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace vast
