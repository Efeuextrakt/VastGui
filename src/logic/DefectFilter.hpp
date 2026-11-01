#pragma once
// Flags offers from machines that are likely broken or misreported. No UI dependency.

#include "model/Json.hpp"
#include "model/Offer.hpp"

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace vast {

struct DefectThresholds {
    bool checkReliability = true;
    double minReliability = 0.90;       // 0..1
    bool checkMissingValues = true;
    bool checkLowPerformance = true;
    double maxBelowMedian = 0.40;       // flagged when value < (1 - maxBelowMedian) * median
    int minSamplesForMedian = 3;        // need this many offers of a GPU model to judge
    bool checkBlacklist = true;
};

void to_json(json& j, const DefectThresholds& t);
void from_json(const json& j, DefectThresholds& t);

struct Blacklist {
    std::set<std::int64_t> machines;
    std::set<std::int64_t> hosts;
    [[nodiscard]] bool hasMachine(std::optional<std::int64_t> id) const { return id && machines.contains(*id); }
    [[nodiscard]] bool hasHost(std::optional<std::int64_t> id) const { return id && hosts.contains(*id); }
};

void to_json(json& j, const Blacklist& b);
void from_json(const json& j, Blacklist& b);

// Deverified status alone is not a defect: the user decides via the blacklist.
enum DefectFlag : std::uint32_t {
    DefectNone = 0,
    DefectLowReliability = 1u << 1,
    DefectMissingValues = 1u << 2,
    DefectLowPerformance = 1u << 3,
    DefectBlacklisted = 1u << 4,
};

struct DefectInfo {
    std::uint32_t flags = DefectNone;
    std::vector<std::string> reasons;
    [[nodiscard]] bool defective() const { return flags != DefectNone; }
};

// Fields that must be present and > 0 for a healthy offer.
[[nodiscard]] std::span<const char* const> requiredOfferFields();

[[nodiscard]] std::vector<DefectInfo> evaluateDefects(std::span<const Offer> offers, const DefectThresholds& t,
                                                      const Blacklist& blacklist);

}  // namespace vast
