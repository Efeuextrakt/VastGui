#include "views/MachineListsView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "views/SearchView.hpp"
#include "widgets/OfferCard.hpp"
#include "widgets/Spinner.hpp"
#include "widgets/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <utility>

namespace vast {

namespace {

std::string idList(const std::set<std::int64_t>& ids) {
    std::string out;
    for (const auto id : ids) out += (out.empty() ? "" : " ") + std::to_string(id);
    return out;
}

// Every offer of the listed machines / hosts, priced like Search (on-demand, the panel's disk size).
SearchFilters listFilters(App& app) {
    const SearchFilters& cur = app.settings().filters;
    SearchFilters f;
    f.showUnverified = true;    // verified=any
    f.showUnavailable = true;   // rentable=any: also offers that cannot be rented right now
    f.showIncompatible = true;  // no template CUDA constraint
    f.diskGb = cur.diskGb;
    f.templateDiskGb = cur.templateDiskGb;
    f.limit = 1000;
    return f;
}

std::vector<Offer> parseOffers(const json& data) {
    std::vector<Offer> out;
    for (const auto& j : jsonutil::extractArray(data, {"offers"})) {
        if (j.is_object()) out.push_back(j.get<Offer>());
    }
    return out;
}

// One offer per machine: the one with the most GPUs (the whole machine), then the lowest price.
std::map<std::int64_t, std::size_t> bestPerMachine(const std::vector<Offer>& offers) {
    std::map<std::int64_t, std::size_t> best;
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const Offer& o = offers[i];
        if (!o.machineId) continue;
        auto [it, inserted] = best.try_emplace(*o.machineId, i);
        if (inserted) continue;
        const Offer& b = offers[it->second];
        if (o.numGpus > b.numGpus ||
            (o.numGpus == b.numGpus && o.priceHr.value_or(1e9) < b.priceHr.value_or(1e9))) {
            it->second = i;
        }
    }
    return best;
}

bool isSubset(const std::set<std::int64_t>& a, const std::set<std::int64_t>& b) {
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

std::set<std::int64_t> rentedMachines(App& app) {
    std::set<std::int64_t> out;
    for (const auto& inst : app.instances()) {
        if (inst.machineId) out.insert(*inst.machineId);
    }
    return out;
}

// Search's offer table for one list; the lists share their column choice (Settings::machineListColumns).
ui::OfferTableResult listTable(App& app, ui::OfferTable& table, const char* id, const std::vector<Offer>& offers,
                               const std::vector<OfferMetrics>& metrics, const std::vector<DefectInfo>& defects,
                               const std::vector<std::size_t>& rows, const char* actionLabel,
                               const std::set<std::int64_t>& rented) {
    Settings& s = app.settings();
    if (s.machineListColumns.size() != ui::OfferTable::defaultColumns().size()) {
        s.machineListColumns = ui::OfferTable::defaultColumns();
    }
    ui::OfferTableConfig cfg;
    cfg.currency = &s.currency;
    cfg.diskGb = s.filters.effectiveDiskGb();
    cfg.actionLabel = actionLabel;
    cfg.scale = app.dpiScale() * s.uiScale;
    // As high as its rows, but scrolling inside the page's remaining space when there are many.
    const float minH = ui::OfferTable::heightForRows(std::min<std::size_t>(rows.size(), 5));
    cfg.height = std::min(ui::OfferTable::heightForRows(rows.size()), std::max(minH, ImGui::GetContentRegionAvail().y));
    bool columnsChanged = false;
    cfg.columns = &s.machineListColumns;
    cfg.columnsChanged = &columnsChanged;
    cfg.rowInfo = [&](std::size_t idx) {
        const Offer& o = offers[idx];
        ui::OfferRowInfo info;
        info.rentedByMe = o.machineId && rented.contains(*o.machineId);
        info.favorite = o.machineId && s.favoriteMachines.contains(*o.machineId);
        return info;
    };
    const ui::OfferTableResult r = table.draw(id, offers, metrics, defects, rows, cfg);
    if (columnsChanged) app.markSettingsDirty();
    return r;
}
}  // namespace

void MachineListsView::fetch(App& app) {
    if (machinesRes_.loading || hostsRes_.loading) return;
    const Settings& s = app.settings();
    std::set<std::int64_t> machines = s.favoriteMachines;
    machines.insert(s.blacklist.machines.begin(), s.blacklist.machines.end());
    fetchedMachines_ = machines;
    fetchedHosts_ = s.blacklist.hosts;
    if (!machines.empty()) {
        SearchFilters f = listFilters(app);
        f.machineIds = idList(machines);
        ApiCall c = cmd::searchOffers(f);
        c.title = "Load favorite / blacklisted machines";
        machinesRes_.fetch(app, std::move(c), [this](Resource&, const ApiResult& r) {
            if (!r.ok) return;
            machineOffers_ = parseOffers(r.data);
            machineMetrics_ = computeMetrics(machineOffers_);
            favTable_.invalidate();
            blTable_.invalidate();
        });
    } else {
        machineOffers_.clear();
        machineMetrics_.clear();
    }
    if (!s.blacklist.hosts.empty()) {
        SearchFilters f = listFilters(app);
        f.hostIds = idList(s.blacklist.hosts);
        ApiCall c = cmd::searchOffers(f);
        c.title = "Load blacklisted hosts";
        hostsRes_.fetch(app, std::move(c), [this](Resource&, const ApiResult& r) {
            if (!r.ok) return;
            hostOffers_ = parseOffers(r.data);
            hostMetrics_ = computeMetrics(hostOffers_);
            hostTable_.invalidate();
        });
    } else {
        hostOffers_.clear();
        hostMetrics_.clear();
    }
}

void MachineListsView::draw(App& app) {
    Settings& s = app.settings();
    const bool busy = machinesRes_.loading || hostsRes_.loading;

    // Load when the page is opened (at most once a minute) and whenever a machine / host was added to a list.
    const int frame = ImGui::GetFrameCount();
    const bool entered = frame - lastFrame_ > 1;
    lastFrame_ = frame;
    std::set<std::int64_t> machines = s.favoriteMachines;
    machines.insert(s.blacklist.machines.begin(), s.blacklist.machines.end());
    const bool added = !isSubset(machines, fetchedMachines_) || !isSubset(s.blacklist.hosts, fetchedHosts_);
    const double last = std::max(machinesRes_.attemptedAt, hostsRes_.attemptedAt);
    const bool stale = entered && (last == 0 || ImGui::GetTime() - last > 60.0);
    if ((added || stale) && !busy) fetch(app);

    ui::PageHeader(ICON_FA_STAR, "Favorites & Blacklist",
                   std::format("{} favorite machines, {} blacklisted machines, {} blacklisted hosts", s.favoriteMachines.size(),
                               s.blacklist.machines.size(), s.blacklist.hosts.size())
                       .c_str());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", busy)) fetch(app);
    ImGui::SameLine();
    {
        const ImVec4 on = ui::palette().accent;
        if (ui::IconButton(ICON_FA_GRIP, "Card view", s.machineListCardView ? &on : nullptr)) {
            s.machineListCardView = true;
            app.markSettingsDirty();
        }
        ImGui::SameLine(0, 0);
        if (ui::IconButton(ICON_FA_TABLE_LIST, "Table view", !s.machineListCardView ? &on : nullptr)) {
            s.machineListCardView = false;
            app.markSettingsDirty();
        }
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::TextDim(std::format("Current on-demand offers of these machines, priced for {:.0f} GB disk like Search. "
                            "Right-click a card for more.",
                            s.filters.effectiveDiskGb())
                    .c_str());
    ui::InlineError(machinesRes_.error);
    ui::InlineError(hostsRes_.error);

    if (ImGui::BeginTabBar("##machinelists")) {
        const std::string favTab = std::format(ICON_FA_STAR " Favorites ({})###fav", s.favoriteMachines.size());
        if (ImGui::BeginTabItem(favTab.c_str())) {
            ImGui::BeginChild("##favlist");
            drawMachines(app, s.favoriteMachines, true);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        const std::string blTab =
            std::format(ICON_FA_BAN " Blacklist ({})###bl", s.blacklist.machines.size() + s.blacklist.hosts.size());
        if (ImGui::BeginTabItem(blTab.c_str())) {
            ImGui::BeginChild("##bllist");
            if (s.blacklist.machines.empty() && s.blacklist.hosts.empty()) {
                ui::TextDim("No blacklisted machines or hosts. Blacklist a machine with the ban button on the Instances "
                            "page, or hide a machine / host from an offer's right-click menu in Search.");
            } else {
                if (!s.blacklist.machines.empty()) {
                    ImGui::SeparatorText("Machines");
                    drawMachines(app, s.blacklist.machines, false);
                }
                drawHosts(app);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void MachineListsView::drawMachines(App& app, const std::set<std::int64_t>& idsRef, bool favorites) {
    Settings& s = app.settings();
    if (idsRef.empty()) {
        ui::TextDim("No favorite machines yet. Add one with the star button on the Instances page or from an offer's "
                    "right-click menu in Search.");
        return;
    }
    const std::set<std::int64_t> ids = idsRef;  // the lists change when a button is clicked
    const auto best = bestPerMachine(machineOffers_);
    const auto defects = evaluateDefects(machineOffers_, s.defects, s.blacklist);
    const std::set<std::int64_t> rented = rentedMachines(app);

    std::vector<std::size_t> rows;  // offer index per listed machine, in ID order
    std::vector<std::int64_t> noOffer;
    std::vector<std::int64_t> waiting;
    for (const auto id : ids) {
        if (const auto it = best.find(id); it != best.end()) {
            rows.push_back(it->second);
        } else {
            const bool known = machinesRes_.loaded && !machinesRes_.loading && fetchedMachines_.contains(id);
            (known ? noOffer : waiting).push_back(id);
        }
    }

    std::optional<std::int64_t> remove;
    std::optional<std::pair<ui::OfferAction, Offer>> pending;
    auto handle = [&](ui::OfferAction a, std::size_t idx) {
        const Offer& o = machineOffers_[idx];
        if (a == ui::OfferAction::ListAction) {
            if (o.machineId) remove = *o.machineId;
        } else if (a != ui::OfferAction::None) {
            pending = std::pair{a, o};
        }
    };
    if (s.machineListCardView) {
        ui::OfferCardContext ctx;
        ctx.currency = &s.currency;
        ctx.diskGb = s.filters.effectiveDiskGb();
        ctx.actionLabel = ICON_FA_XMARK "  REMOVE";
        float measured = 0.0f;
        ctx.measuredHeight = &measured;
        ctx.height = cardHeight_ > 0.0f ? cardHeight_ : ui::OfferCardHeight();
        for (const std::size_t idx : rows) {
            const Offer& o = machineOffers_[idx];
            ctx.rentedByMe = o.machineId && rented.contains(*o.machineId);
            ctx.favorite = o.machineId && s.favoriteMachines.contains(*o.machineId);
            handle(ui::OfferCard(o, machineMetrics_[idx], defects[idx], ctx), idx);
        }
        if (measured > 0.0f) cardHeight_ = std::ceil(measured);
    } else if (!rows.empty()) {
        const ui::OfferTableResult r = listTable(app, favorites ? favTable_ : blTable_, favorites ? "##favtable" : "##bltable",
                                                 machineOffers_, machineMetrics_, defects, rows, "REMOVE", rented);
        handle(r.action, r.index);
    }

    if (!waiting.empty()) ui::SpinnerText(favorites ? "##favwait" : "##blwait", "loading offers...");
    if (!noOffer.empty()) {
        ImGui::SeparatorText("No offer right now");
        ui::TextDim("These machines are not listed at the moment (unlisted by the host, fully rented or offline).");
        for (const auto id : noOffer) {
            ImGui::PushID(static_cast<int>(id));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(std::format(ICON_FA_SERVER "  Machine {}", id).c_str());
            ImGui::SameLine(ImGui::GetFontSize() * 12.0f);
            if (ui::SecondaryButton(ICON_FA_XMARK "  Remove")) remove = id;
            ImGui::PopID();
        }
    }

    if (remove) {
        if (favorites) {
            s.setFavoriteMachine(*remove, false);
            app.toasts().info("Machine " + std::to_string(*remove) + " removed from favorites");
        } else {
            s.setBlacklistedMachine(*remove, false);
            app.toasts().info("Machine " + std::to_string(*remove) + " removed from the blacklist");
        }
        app.markSettingsDirty();
        app.searchView().onBlacklistChanged(app);
    }
    if (pending) app.searchView().handleOfferAction(app, pending->first, pending->second);
}

void MachineListsView::drawHosts(App& app) {
    Settings& s = app.settings();
    if (s.blacklist.hosts.empty()) return;
    ImGui::Spacing();
    ImGui::SeparatorText("Hosts");
    const std::set<std::int64_t> hosts = s.blacklist.hosts;
    const auto best = bestPerMachine(hostOffers_);
    const auto defects = evaluateDefects(hostOffers_, s.defects, s.blacklist);
    const std::set<std::int64_t> rented = rentedMachines(app);
    const bool cards = s.machineListCardView;

    ui::OfferCardContext ctx;
    ctx.currency = &s.currency;
    ctx.diskGb = s.filters.effectiveDiskGb();
    ctx.actionLabel = "";  // removed per host, above its machines
    float measured = 0.0f;
    ctx.measuredHeight = &measured;
    ctx.height = cardHeight_ > 0.0f ? cardHeight_ : ui::OfferCardHeight();

    std::optional<std::int64_t> remove;
    std::optional<std::pair<ui::OfferAction, Offer>> pending;
    std::vector<std::size_t> tableRows;  // table view: the machines of all hosts in one table
    for (const auto host : hosts) {
        ImGui::PushID(static_cast<int>(host));
        std::vector<std::size_t> machines;
        for (const auto& [machine, idx] : best) {
            if (hostOffers_[idx].hostId == host) machines.push_back(idx);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format(ICON_FA_USER "  Host {}", host).c_str());
        ImGui::SameLine();
        const bool known = hostsRes_.loaded && !hostsRes_.loading && fetchedHosts_.contains(host);
        ui::TextDim(!known ? "loading..."
                    : machines.empty()
                        ? "no machine listed right now"
                        : std::format("{} machine{} listed", machines.size(), machines.size() == 1 ? "" : "s").c_str());
        ImGui::SameLine();
        if (ui::SecondaryButton(ICON_FA_XMARK "  Remove host")) remove = host;
        if (cards) {
            for (const std::size_t idx : machines) {
                const Offer& o = hostOffers_[idx];
                ctx.rentedByMe = o.machineId && rented.contains(*o.machineId);
                ctx.favorite = o.machineId && s.favoriteMachines.contains(*o.machineId);
                const auto a = ui::OfferCard(o, hostMetrics_[idx], defects[idx], ctx);
                if (a != ui::OfferAction::None && a != ui::OfferAction::ListAction) pending = std::pair{a, o};
            }
        } else {
            tableRows.insert(tableRows.end(), machines.begin(), machines.end());
        }
        ImGui::PopID();
    }
    if (measured > 0.0f) cardHeight_ = std::max(cardHeight_, std::ceil(measured));
    if (!cards && !tableRows.empty()) {
        const ui::OfferTableResult r =
            listTable(app, hostTable_, "##hosttable", hostOffers_, hostMetrics_, defects, tableRows, "REMOVE HOST", rented);
        if (r.action == ui::OfferAction::ListAction) {
            remove = hostOffers_[r.index].hostId;
        } else if (r.action != ui::OfferAction::None) {
            pending = std::pair{r.action, hostOffers_[r.index]};
        }
    }

    if (remove) {
        s.blacklist.hosts.erase(*remove);
        app.toasts().info("Host " + std::to_string(*remove) + " removed from the blacklist");
        app.markSettingsDirty();
        app.searchView().onBlacklistChanged(app);
    }
    if (pending) app.searchView().handleOfferAction(app, pending->first, pending->second);
}

}  // namespace vast
