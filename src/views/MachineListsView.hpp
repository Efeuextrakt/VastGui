#pragma once
// "Favorites & Blacklist" page: the current offers of the favorite and blacklisted machines (and of the machines
// of blacklisted hosts), shown as Search cards with a Remove button instead of RENT.

#include "logic/DefectFilter.hpp"
#include "logic/Metrics.hpp"
#include "model/Offer.hpp"
#include "views/Resource.hpp"

#include <cstdint>
#include <set>
#include <vector>

namespace vast {

class App;

class MachineListsView {
public:
    void draw(App& app);

private:
    void fetch(App& app);
    // One card per machine (its whole-machine offer), or a placeholder row when it has no offer right now.
    void drawMachines(App& app, const std::set<std::int64_t>& ids, bool favorites);
    void drawHosts(App& app);

    Resource machinesRes_;  // offers of the favorite + blacklisted machines
    Resource hostsRes_;     // offers on the blacklisted hosts
    std::vector<Offer> machineOffers_;
    std::vector<Offer> hostOffers_;
    std::vector<OfferMetrics> machineMetrics_;
    std::vector<OfferMetrics> hostMetrics_;
    std::set<std::int64_t> fetchedMachines_;  // ids the last request asked for
    std::set<std::int64_t> fetchedHosts_;
    int lastFrame_ = -10;
    float cardHeight_ = 0.0f;
};

}  // namespace vast
