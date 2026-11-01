#include "logic/Metrics.hpp"

#include "logic/Format.hpp"
#include "logic/QueryBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <numeric>

namespace vast {

std::optional<double> safeRatio(std::optional<double> num, std::optional<double> den) {
    if (!num || !den) return std::nullopt;
    if (!std::isfinite(*num) || !std::isfinite(*den)) return std::nullopt;
    if (*den <= 0.0 || *num < 0.0) return std::nullopt;
    const double r = *num / *den;
    if (!std::isfinite(r)) return std::nullopt;
    return r;
}

std::optional<double> tflopsPerDollar(const Offer& o) {
    if (!o.totalFlops || *o.totalFlops <= 0.0) return std::nullopt;
    return safeRatio(o.totalFlops, o.dphTotal);
}

std::optional<double> dlpPerDollar(const Offer& o) {
    if (!o.dlperf || *o.dlperf <= 0.0) return std::nullopt;
    return safeRatio(o.dlperf, o.dphTotal);
}

float percentileRank(double value, std::span<const double> sorted) {
    if (sorted.size() <= 1) return 0.0f;
    const auto lo = std::lower_bound(sorted.begin(), sorted.end(), value);
    const auto hi = std::upper_bound(sorted.begin(), sorted.end(), value);
    const double first = static_cast<double>(lo - sorted.begin());
    const double last = hi == lo ? first : static_cast<double>(hi - sorted.begin()) - 1.0;
    const double mid = (first + last) * 0.5;
    return static_cast<float>(std::clamp(mid / static_cast<double>(sorted.size() - 1), 0.0, 1.0));
}

std::vector<std::optional<float>> higherIsBetterRanks(std::span<const std::optional<double>> values) {
    std::vector<double> sorted;
    for (const auto& v : values) {
        if (v) sorted.push_back(*v);
    }
    std::vector<std::optional<float>> out(values.size());
    if (sorted.size() < 2) return out;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i]) out[i] = 1.0f - percentileRank(*values[i], sorted);
    }
    return out;
}

double reservedDiscount(double maxDiscount, double months) {
    if (!(maxDiscount > 0.0) || !(months > 0.0)) return 0.0;
    return std::clamp(maxDiscount, 0.0, 1.0) * months / (months + 1.0);
}

ReservedPlan reservedPlanForMonths(double dphTotal, double maxDiscount, double months) {
    ReservedPlan p;
    p.months = std::max(0.0, months);
    p.discount = reservedDiscount(maxDiscount, p.months);
    p.ratePerHour = std::max(0.0, dphTotal) * (1.0 - p.discount);
    p.amount = p.ratePerHour * kHoursPerReserveMonth * p.months;
    return p;
}

std::optional<ReservedPlan> reservedPlanForAmount(double dphTotal, double maxDiscount, double amount) {
    if (!(dphTotal > 0.0) || !(amount > 0.0)) return std::nullopt;
    const double m = std::clamp(maxDiscount, 0.0, 1.0);
    const double a = amount / (dphTotal * kHoursPerReserveMonth);  // months at the undiscounted rate
    // t * (1 - m*t/(t+1)) = a  <=>  (1-m) t^2 + (1-a) t - a = 0, positive root.
    double t = 0;
    if (1.0 - m < 1e-9) {
        // Linear case (100% max discount): (1-a) t = a.
        if (a >= 1.0) return std::nullopt;
        t = a / (1.0 - a);
    } else {
        const double qa = 1.0 - m, qb = 1.0 - a, qc = -a;
        t = (-qb + std::sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
    }
    if (!std::isfinite(t) || t <= 0.0) return std::nullopt;
    ReservedPlan p = reservedPlanForMonths(dphTotal, m, t);
    p.amount = amount;
    return p;
}

std::optional<double> median(std::vector<double> values) {
    if (values.empty()) return std::nullopt;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

std::vector<OfferMetrics> computeMetrics(std::span<const Offer> offers) {
    std::vector<OfferMetrics> out(offers.size());
    std::vector<double> tflopsVals;
    std::vector<double> dlpVals;
    for (std::size_t i = 0; i < offers.size(); ++i) {
        OfferMetrics& m = out[i];
        m.tflopsPerDollar = tflopsPerDollar(offers[i]);
        m.dlpPerDollar = dlpPerDollar(offers[i]);
        if (m.tflopsPerDollar) tflopsVals.push_back(*m.tflopsPerDollar);
        if (m.dlpPerDollar) dlpVals.push_back(*m.dlpPerDollar);
    }
    std::sort(tflopsVals.begin(), tflopsVals.end());
    std::sort(dlpVals.begin(), dlpVals.end());
    // Higher is better, so the largest value gets rank 0 (best).
    for (auto& m : out) {
        if (m.tflopsPerDollar) m.rankTflopsPerDollar = 1.0f - percentileRank(*m.tflopsPerDollar, tflopsVals);
        if (m.dlpPerDollar) m.rankDlpPerDollar = 1.0f - percentileRank(*m.dlpPerDollar, dlpVals);
    }
    return out;
}

bool passesVerificationFilter(const Offer& o, const SearchFilters& f) {
    if (o.verification != "deverified") return true;
    return f.showUnverified && f.showDeverified;
}

OfferIdentity offerIdentity(const Offer& o) {
    if (o.machineId) return {*o.machineId, o.numGpus};
    return {-1, o.id};  // machine ids are positive, so this never collides with a real machine
}

std::vector<Offer> mergeOfferSamples(std::vector<std::vector<Offer>> samples) {
    std::vector<Offer> out;
    std::map<OfferIdentity, std::size_t> index;
    for (auto& sample : samples) {
        for (Offer& o : sample) {
            const auto [it, inserted] = index.try_emplace(offerIdentity(o), out.size());
            if (inserted) {
                out.push_back(std::move(o));
                continue;
            }
            Offer& kept = out[it->second];
            if (o.dphTotal && (!kept.dphTotal || *o.dphTotal < *kept.dphTotal)) kept = std::move(o);
        }
    }
    return out;
}

bool isIncompatible(const Offer& o, const SearchFilters& f) {
    return !unmetRequirements(o, f.templateRequirements).empty();
}

std::string incompatibleReason(const Offer& o, const SearchFilters& f) {
    const auto unmet = unmetRequirements(o, f.templateRequirements);
    if (unmet.empty()) return {};
    std::string s = "Incompatible with the selected template:";
    for (const auto& u : unmet) s += "\n  " + u;
    return s;
}

namespace {

const char* opSymbol(std::string_view op) {
    if (op == "gte") return ">=";
    if (op == "lte") return "<=";
    if (op == "gt") return ">";
    if (op == "lt") return "<";
    if (op == "eq") return "=";
    if (op == "neq") return "!=";
    if (op == "in") return "in";
    if (op == "notin" || op == "nin") return "not in";
    return "?";
}

std::string valueText(const json& v, std::string_view field = {}) {
    if (field == "driver_version" && v.is_number() && v.get<double>() >= 1e6) {
        const auto n = static_cast<long long>(v.get<double>());
        return std::format("{}.{}.{}", n / 1000000, (n / 1000) % 1000, n % 1000);
    }
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_float()) return QueryBuilder::formatNumber(v.get<double>());
    return v.dump();
}

std::optional<double> numberOf(std::string_view field, const json& v) {
    if (field == "driver_version") {
        // Encoded like the server (major*1e6 + minor*1e3 + patch); a bare major version is scaled up.
        if (v.is_number()) {
            const double d = v.get<double>();
            return d < 1e6 ? d * 1e6 : d;
        }
        if (!v.is_string()) return std::nullopt;
        if (const auto d = QueryBuilder::numericDriverVersion(QueryBuilder::normalizeDriverVersion(v.get<std::string>()))) {
            return static_cast<double>(*d);
        }
        return std::nullopt;
    }
    if (v.is_number()) return v.get<double>();
    if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
    if (!v.is_string()) return std::nullopt;
    const std::string s = fmt::trim(v.get<std::string>());
    try {
        std::size_t pos = 0;
        const double d = std::stod(s, &pos);
        if (pos == s.size()) return d;
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

bool sameValue(std::string_view field, const json& a, const json& b) {
    const auto na = numberOf(field, a), nb = numberOf(field, b);
    if (na && nb) return std::abs(*na - *nb) < 1e-9;
    std::string sa = fmt::toLower(valueText(a)), sb = fmt::toLower(valueText(b));
    std::ranges::replace(sa, '_', ' ');
    std::ranges::replace(sb, '_', ' ');
    return sa == sb;
}

bool satisfies(std::string_view field, const json& have, std::string_view op, const json& want) {
    if (want.is_null() || (want.is_string() && fmt::toLower(want.get<std::string>()) == "any")) return true;
    if (op == "in" || op == "notin" || op == "nin") {
        if (!want.is_array()) return true;
        const bool found = std::ranges::any_of(want, [&](const json& w) { return sameValue(field, have, w); });
        return op == "in" ? found : !found;
    }
    if (op == "eq") return sameValue(field, have, want);
    if (op == "neq") return !sameValue(field, have, want);
    const auto h = numberOf(field, have), w = numberOf(field, want);
    if (!h || !w) return true;  // cannot judge
    constexpr double eps = 1e-6;
    if (op == "gte") return *h + eps >= *w;
    if (op == "lte") return *h - eps <= *w;
    if (op == "gt") return *h > *w + eps;
    if (op == "lt") return *h < *w - eps;
    return true;
}

}  // namespace

json templateRequirements(std::string_view extraFilters, std::string_view imageWithTag) {
    json reqs = json::object();
    const std::string ef = fmt::trim(extraFilters);
    if (!ef.empty() && ef != "null") {
        json j = json::parse(ef, nullptr, false);
        if (!j.is_discarded()) {
            if (j.is_object()) reqs = std::move(j);
        } else if (auto parsed = QueryBuilder::parseQuery(ef); parsed.ok()) {
            reqs = std::move(parsed.query);
        }
    }
    // Availability / paging keys are search options, not machine requirements.
    for (const char* k : {"order", "type", "limit", "allocated_storage", "external", "rentable", "rented", "verified"}) {
        reqs.erase(k);
    }
    for (auto it = reqs.begin(); it != reqs.end();) {
        if (it->is_object() && !it->empty()) {
            ++it;
        } else {
            it = reqs.erase(it);
        }
    }
    if (const auto cuda = QueryBuilder::cudaFromImageTag(imageWithTag)) {
        json& c = reqs["cuda_max_good"];
        if (!c.is_object()) c = json::object();
        const auto cur = c.contains("gte") ? numberOf("cuda_max_good", c["gte"]) : std::nullopt;
        if (!cur || *cuda > *cur) c["gte"] = *cuda;
    }
    return reqs;
}

std::optional<double> requiredMinCuda(const json& requirements) {
    const auto it = requirements.find("cuda_max_good");
    if (it == requirements.end() || !it->is_object()) return std::nullopt;
    std::optional<double> out;
    for (const char* op : {"gte", "gt", "eq"}) {
        if (const auto v = it->find(op); v != it->end()) {
            if (const auto n = numberOf("cuda_max_good", *v)) out = std::max(out.value_or(0.0), *n);
        }
    }
    return out;
}

std::vector<std::string> describeRequirements(const json& requirements) {
    std::vector<std::string> out;
    if (!requirements.is_object()) return out;
    for (const auto& [field, cond] : requirements.items()) {
        if (!cond.is_object()) continue;
        for (const auto& [op, want] : cond.items()) {
            out.push_back(std::format("{} {} {}", field, opSymbol(op), valueText(want, field)));
        }
    }
    return out;
}

std::vector<std::string> unmetRequirements(const Offer& o, const json& requirements) {
    std::vector<std::string> out;
    if (!requirements.is_object() || !o.raw.is_object()) return out;
    for (const auto& [field, cond] : requirements.items()) {
        if (!cond.is_object()) continue;
        const auto have = o.raw.find(field);
        if (have == o.raw.end() || have->is_null()) continue;
        for (const auto& [op, want] : cond.items()) {
            if (!satisfies(field, *have, op, want)) {
                out.push_back(std::format("{} {} {} (this machine: {})", field, opSymbol(op), valueText(want, field),
                                          valueText(*have, field)));
            }
        }
    }
    return out;
}

std::optional<double> sortValue(const Offer& o, const OfferMetrics& m, SortKey key) {
    switch (key) {
        case SortKey::Score: return o.score;
        case SortKey::Price: return o.dphTotal;
        case SortKey::TflopsPerDollar: return m.tflopsPerDollar;
        case SortKey::DlpPerDollar: return m.dlpPerDollar;
        case SortKey::Reliability: return o.reliability;
        case SortKey::InetDown: return o.inetDown;
        case SortKey::InetUp: return o.inetUp;
        case SortKey::DiskBw: return o.diskBw;
        case SortKey::Duration: return o.durationSec;
        case SortKey::Vram: return o.gpuRamMb;
        case SortKey::Cuda: return o.cudaMaxGood;
        case SortKey::Tflops: return o.totalFlops;
        case SortKey::Dlperf: return o.dlperf;
        case SortKey::NumGpus: return static_cast<double>(o.numGpus);
        case SortKey::Count: break;
    }
    return std::nullopt;
}

std::vector<std::size_t> sortedOrder(std::span<const Offer> offers, std::span<const OfferMetrics> metrics, SortKey key,
                                     bool descending) {
    std::vector<std::size_t> idx(offers.size());
    std::iota(idx.begin(), idx.end(), std::size_t{0});
    std::vector<std::optional<double>> keys(offers.size());
    for (std::size_t i = 0; i < offers.size(); ++i) {
        keys[i] = i < metrics.size() ? sortValue(offers[i], metrics[i], key) : std::nullopt;
    }
    std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
        const auto& ka = keys[a];
        const auto& kb = keys[b];
        if (ka && kb && *ka != *kb) return descending ? *ka > *kb : *ka < *kb;
        if (ka.has_value() != kb.has_value()) return ka.has_value();
        // Equal (or both missing) values: order by machine + GPU count so ties don't swap between searches.
        return offerIdentity(offers[a]) < offerIdentity(offers[b]);
    });
    return idx;
}

std::string formatMetric(std::optional<double> v, int decimals) {
    if (!v || !std::isfinite(*v)) return kMissing;
    return std::format("{:.{}f}", *v, decimals);
}

}  // namespace vast
