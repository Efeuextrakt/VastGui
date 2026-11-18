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
    std::set<std::int64_t> rented;
    for (const auto& inst : app.instances()) {
        if (inst.machineId) rented.insert(*inst.machineId);
    }

    ui::OfferCardContext ctx;
    ctx.currency = &s.currency;
    ctx.diskGb = s.filters.effectiveDiskGb();
    ctx.actionLabel = ICON_FA_XMARK "  REMOVE";
    float measured = 0.0f;
    ctx.measuredHeight = &measured;
    ctx.height = cardHeight_ > 0.0f ? cardHeight_ : ui::OfferCardHeight();

    std::optional<std::int64_t> remove;
    std::optional<std::pair<ui::OfferAction, Offer>> pending;
    std::vector<std::int64_t> noOffer;
    std::vector<std::int64_t> waiting;
    for (const auto id : ids) {
        const auto it = best.find(id);
        if (it == best.end()) {
            const bool known = machinesRes_.loaded && !machinesRes_.loading && fetchedMachines_.contains(id);
            (known ? noOffer : waiting).push_back(id);
            continue;
        }
        const Offer& o = machineOffers_[it->second];
        ctx.rentedByMe = rented.contains(id);
        ctx.favorite = s.favoriteMachines.contains(id);
        const auto a = ui::OfferCard(o, machineMetrics_[it->second], defects[it->second], ctx);
        if (a == ui::OfferAction::ListAction) {
            remove = id;
        } else if (a != ui::OfferAction::None) {
            pending = std::pair{a, o};
        }
    }
    if (measured > 0.0f) cardHeight_ = std::ceil(measured);

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

    ui::OfferCardContext ctx;
    ctx.currency = &s.currency;
    ctx.diskGb = s.filters.effectiveDiskGb();
    ctx.actionLabel = "";  // removed per host, above its machines
    float measured = 0.0f;
    ctx.measuredHeight = &measured;
    ctx.height = cardHeight_ > 0.0f ? cardHeight_ : ui::OfferCardHeight();

    std::optional<std::int64_t> remove;
    std::optional<std::pair<ui::OfferAction, Offer>> pending;
    for (const auto host : hosts) {
        ImGui::PushID(static_cast<int>(host));
        std::vector<std::size_t> cards;
        for (const auto& [machine, idx] : best) {
            if (hostOffers_[idx].hostId == host) cards.push_back(idx);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format(ICON_FA_USER "  Host {}", host).c_str());
        ImGui::SameLine();
        const bool known = hostsRes_.loaded && !hostsRes_.loading && fetchedHosts_.contains(host);
        ui::TextDim(!known ? "loading..."
                    : cards.empty()
                        ? "no machine listed right now"
                        : std::format("{} machine{} listed", cards.size(), cards.size() == 1 ? "" : "s").c_str());
        ImGui::SameLine();
        if (ui::SecondaryButton(ICON_FA_XMARK "  Remove host")) remove = host;
        for (const std::size_t idx : cards) {
            const Offer& o = hostOffers_[idx];
            ctx.favorite = o.machineId && s.favoriteMachines.contains(*o.machineId);
            const auto a = ui::OfferCard(o, hostMetrics_[idx], defects[idx], ctx);
            if (a != ui::OfferAction::None && a != ui::OfferAction::ListAction) pending = std::pair{a, o};
        }
        ImGui::PopID();
    }
    if (measured > 0.0f) cardHeight_ = std::max(cardHeight_, std::ceil(measured));

    if (remove) {
        s.blacklist.hosts.erase(*remove);
        app.toasts().info("Host " + std::to_string(*remove) + " removed from the blacklist");
        app.markSettingsDirty();
        app.searchView().onBlacklistChanged(app);
    }
    if (pending) app.searchView().handleOfferAction(app, pending->first, pending->second);
}

}  // namespace vast
