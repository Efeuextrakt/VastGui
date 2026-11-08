#pragma once
// Derived offer metrics. Higher TFLOPS/$/hr and DLP/$/hr are better.

#include "logic/SearchFilters.hpp"
#include "model/Offer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vast {

// num / den, or nullopt when either value is missing, non-finite, or den <= 0 / num < 0.
[[nodiscard]] std::optional<double> safeRatio(std::optional<double> num, std::optional<double> den);

[[nodiscard]] std::optional<double> tflopsPerDollar(const Offer& o);  // total_flops / dph_total
[[nodiscard]] std::optional<double> dlpPerDollar(const Offer& o);     // dlperf / dph_total

struct OfferMetrics {
    std::optional<double> tflopsPerDollar;
    std::optional<double> dlpPerDollar;
    // 0 = best (most per dollar) .. 1 = worst within the current result set; nullopt when the metric is missing.
    std::optional<float> rankTflopsPerDollar;
    std::optional<float> rankDlpPerDollar;
};

[[nodiscard]] std::vector<OfferMetrics> computeMetrics(std::span<const Offer> offers);

// Position of `value` within ascending `sorted` values mapped to 0..1 (ties share the mean rank).
[[nodiscard]] float percentileRank(double value, std::span<const double> sorted);
[[nodiscard]] std::optional<double> median(std::vector<double> values);
// Colour ranks for "higher is better" values: 0 = best, 1 = worst; nullopt for missing values and when fewer than
// two values exist (nothing to compare against).
[[nodiscard]] std::vector<std::optional<float>> higherIsBetterRanks(std::span<const std::optional<double>> values);

// vast.ai answers each offer search with a random sample of the matching offers and re-picks the GPU subset
// (and so the offer id) of multi-GPU machines on every request. An offer is therefore identified by
// machine + GPU count; the id is only a fallback when the machine is unknown.
using OfferIdentity = std::pair<std::int64_t, std::int64_t>;
[[nodiscard]] OfferIdentity offerIdentity(const Offer& o);
// Merges several search responses: one offer per identity (the cheapest; ties keep the first seen),
// in first-seen order.
[[nodiscard]] std::vector<Offer> mergeOfferSamples(std::vector<std::vector<Offer>> samples);

// --- Reserved (prepaid) discount --------------------------------------------------------------------------------
// vast.ai's prepay discount for t prepaid months (30 days each) is max * t / (t + 1), where max is the host's
// credit_discount_max (fitted to the server: 1 month -> 50% of max, 3 -> 75%, 6 -> ~86%, 12 -> ~92%).
inline constexpr double kHoursPerReserveMonth = 720.0;
struct ReservedPlan {
    double months = 0;
    double discount = 0;     // 0..1
    double ratePerHour = 0;  // discounted $/hr
    double amount = 0;       // credits deducted now
};
[[nodiscard]] double reservedDiscount(double maxDiscount, double months);
[[nodiscard]] ReservedPlan reservedPlanForMonths(double dphTotal, double maxDiscount, double months);
// Inverse: the plan a prepaid amount buys (solves amount = dph * (1 - discount(t)) * 720 * t for t).
[[nodiscard]] std::optional<ReservedPlan> reservedPlanForAmount(double dphTotal, double maxDiscount, double amount);

// Hours until the account credit reaches 0. `rate` is what the account pays now ($/hr); each `later` entry adds
// its rate after the given number of hours (a prepaid instance whose own credit runs out switches to the account).
// nullopt when nothing is billed to the account, ever.
struct LaterCharge {
    double afterHours = 0;
    double rate = 0;
};
[[nodiscard]] std::optional<double> creditRunOutHours(double credit, double rate, std::vector<LaterCharge> later);

// Deverified offers are only shown when both "Unverified" and its "Deverified" sub-option are on.
[[nodiscard]] bool passesVerificationFilter(const Offer& o, const SearchFilters& f);

// Template requirements in the REST query format ({"field": {"op": value}}): the template's extra_filters
// (JSON object or vastai query string) plus the CUDA version implied by the image tag. Availability keys are dropped.
[[nodiscard]] json templateRequirements(std::string_view extraFilters, std::string_view imageWithTag);
// Minimum CUDA version demanded by the requirements, if any.
[[nodiscard]] std::optional<double> requiredMinCuda(const json& requirements);
// Human-readable requirements ("cuda_max_good >= 12.4"), one per entry.
[[nodiscard]] std::vector<std::string> describeRequirements(const json& requirements);
// Requirements the offer does not meet; fields the offer does not report are not judged.
[[nodiscard]] std::vector<std::string> unmetRequirements(const Offer& o, const json& requirements);

// True when the offer fails any requirement of the selected template (f.templateRequirements).
[[nodiscard]] bool isIncompatible(const Offer& o, const SearchFilters& f);
[[nodiscard]] std::string incompatibleReason(const Offer& o, const SearchFilters& f);
[[nodiscard]] std::optional<double> sortValue(const Offer& o, const OfferMetrics& m, SortKey key);
// Returns indices into `offers` sorted by key; missing values always sort last.
[[nodiscard]] std::vector<std::size_t> sortedOrder(std::span<const Offer> offers, std::span<const OfferMetrics> metrics,
                                                   SortKey key, bool descending);

// "0.0123" style formatting with an em dash for missing values.
[[nodiscard]] std::string formatMetric(std::optional<double> v, int decimals);

inline constexpr const char* kMissing = "\xE2\x80\x94";  // em dash

}  // namespace vast
