#include "logic/DefectFilter.hpp"

#include "logic/Metrics.hpp"

#include <format>
#include <map>

namespace vast {

void to_json(json& j, const DefectThresholds& t) {
    j = json{{"check_reliability", t.checkReliability},
             {"min_reliability", t.minReliability},
             {"check_missing_values", t.checkMissingValues},
             {"check_low_performance", t.checkLowPerformance},
             {"max_below_median", t.maxBelowMedian},
             {"min_samples_for_median", t.minSamplesForMedian},
             {"check_blacklist", t.checkBlacklist}};
}

void from_json(const json& j, DefectThresholds& t) {
    using namespace jsonutil;
    t = DefectThresholds{};
    t.checkReliability = optBool(j, "check_reliability").value_or(t.checkReliability);
    t.minReliability = optDouble(j, "min_reliability").value_or(t.minReliability);
    t.checkMissingValues = optBool(j, "check_missing_values").value_or(t.checkMissingValues);
    t.checkLowPerformance = optBool(j, "check_low_performance").value_or(t.checkLowPerformance);
    t.maxBelowMedian = optDouble(j, "max_below_median").value_or(t.maxBelowMedian);
    t.minSamplesForMedian = static_cast<int>(optInt(j, "min_samples_for_median").value_or(t.minSamplesForMedian));
    t.checkBlacklist = optBool(j, "check_blacklist").value_or(t.checkBlacklist);
}

void to_json(json& j, const Blacklist& b) { j = json{{"machines", b.machines}, {"hosts", b.hosts}}; }

void from_json(const json& j, Blacklist& b) {
    b = Blacklist{};
    auto read = [&](const char* key, std::set<std::int64_t>& dst) {
        if (const json* a = jsonutil::find(j, key); a && a->is_array()) {
            for (const auto& v : *a) {
                if (v.is_number()) dst.insert(v.get<std::int64_t>());
            }
        }
    };
    read("machines", b.machines);
    read("hosts", b.hosts);
}

namespace {

constexpr const char* kRequired[] = {"inet_down", "inet_up", "total_flops", "dlperf", "disk_bw", "cuda_max_good"};

std::optional<double> requiredValue(const Offer& o, std::string_view field) {
    if (field == "inet_down") return o.inetDown;
    if (field == "inet_up") return o.inetUp;
    if (field == "total_flops") return o.totalFlops;
    if (field == "dlperf") return o.dlperf;
    if (field == "disk_bw") return o.diskBw;
    if (field == "cuda_max_good") return o.cudaMaxGood;
    return std::nullopt;
}

std::optional<double> perGpu(const std::optional<double>& v, int numGpus) {
    if (!v || *v <= 0.0) return std::nullopt;
    return *v / static_cast<double>(numGpus > 0 ? numGpus : 1);
}

struct ModelMedians {
    std::optional<double> dlperf;
    std::optional<double> flops;
};

}  // namespace

std::span<const char* const> requiredOfferFields() { return kRequired; }

std::vector<DefectInfo> evaluateDefects(std::span<const Offer> offers, const DefectThresholds& t,
                                        const Blacklist& blacklist) {
    std::vector<DefectInfo> out(offers.size());

    // Medians of per-GPU performance by GPU model, so 1x and 8x offers are comparable.
    std::map<std::string, ModelMedians> medians;
    if (t.checkLowPerformance) {
        std::map<std::string, std::pair<std::vector<double>, std::vector<double>>> samples;
        for (const auto& o : offers) {
            auto& s = samples[o.gpuName];
            if (auto v = perGpu(o.dlperf, o.numGpus)) s.first.push_back(*v);
            if (auto v = perGpu(o.totalFlops, o.numGpus)) s.second.push_back(*v);
        }
        for (auto& [model, s] : samples) {
            ModelMedians m;
            if (static_cast<int>(s.first.size()) >= t.minSamplesForMedian) m.dlperf = median(s.first);
            if (static_cast<int>(s.second.size()) >= t.minSamplesForMedian) m.flops = median(s.second);
            medians[model] = m;
        }
    }

    const double factor = 1.0 - t.maxBelowMedian;
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const Offer& o = offers[i];
        DefectInfo& d = out[i];

        if (t.checkReliability && o.reliability && *o.reliability < t.minReliability) {
            d.flags |= DefectLowReliability;
            d.reasons.push_back(
                std::format("reliability {:.1f}% < {:.1f}%", *o.reliability * 100.0, t.minReliability * 100.0));
        }

        if (t.checkMissingValues) {
            std::string missing;
            for (const char* f : kRequired) {
                const auto v = requiredValue(o, f);
                if (!v || *v <= 0.0) {
                    if (!missing.empty()) missing += ", ";
                    missing += f;
                }
            }
            if (!missing.empty()) {
                d.flags |= DefectMissingValues;
                d.reasons.push_back("missing/zero: " + missing);
            }
        }

        if (t.checkLowPerformance) {
            if (auto it = medians.find(o.gpuName); it != medians.end()) {
                const auto dl = perGpu(o.dlperf, o.numGpus);
                const auto fl = perGpu(o.totalFlops, o.numGpus);
                if (dl && it->second.dlperf && *dl < factor * *it->second.dlperf) {
                    d.flags |= DefectLowPerformance;
                    d.reasons.push_back(std::format("DLPerf/GPU {:.1f} is {:.0f}% below {} median {:.1f}", *dl,
                                                    (1.0 - *dl / *it->second.dlperf) * 100.0, o.gpuName,
                                                    *it->second.dlperf));
                }
                if (fl && it->second.flops && *fl < factor * *it->second.flops) {
                    d.flags |= DefectLowPerformance;
                    d.reasons.push_back(std::format("TFLOPS/GPU {:.1f} is {:.0f}% below {} median {:.1f}", *fl,
                                                    (1.0 - *fl / *it->second.flops) * 100.0, o.gpuName,
                                                    *it->second.flops));
                }
            }
        }

        if (t.checkBlacklist) {
            if (blacklist.hasMachine(o.machineId)) {
                d.flags |= DefectBlacklisted;
                d.reasons.push_back(std::format("machine {} is on your blacklist", *o.machineId));
            }
            if (blacklist.hasHost(o.hostId)) {
                d.flags |= DefectBlacklisted;
                d.reasons.push_back(std::format("host {} is on your blacklist", *o.hostId));
            }
        }
    }
    return out;
}

}  // namespace vast
