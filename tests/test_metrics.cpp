#include "Fixtures.hpp"
#include "doctest/doctest.h"
#include "logic/Metrics.hpp"

#include <cmath>
#include <limits>

using namespace vast;

TEST_SUITE("Metrics") {
    TEST_CASE("TFLOPS/$/hr and DLP/$/hr") {
        const Offer o = fixtures::makeOffer(1, "RTX 4090", 1, 0.5, 100.0, 50.0);
        CHECK(tflopsPerDollar(o).value() == doctest::Approx(200.0));
        CHECK(dlpPerDollar(o).value() == doctest::Approx(100.0));
    }

    TEST_CASE("real offer fixture") {
        const Offer o = fixtures::offerFrom(fixtures::kHealthyOffer);
        CHECK(o.id == 57147106);
        CHECK(o.countryCode() == "CN");
        CHECK(tflopsPerDollar(o).value() == doctest::Approx(81.4 / 0.4759));
        CHECK(dlpPerDollar(o).value() == doctest::Approx(123.65 / 0.4759));
    }

    TEST_CASE("division by zero and missing values yield nullopt") {
        Offer o = fixtures::makeOffer(1, "RTX 4090", 1, 0.0, 80.0, 100.0);
        CHECK_FALSE(tflopsPerDollar(o).has_value());  // zero price
        CHECK_FALSE(dlpPerDollar(o).has_value());
        o.dphTotal.reset();
        CHECK_FALSE(tflopsPerDollar(o).has_value());  // missing price
        CHECK_FALSE(dlpPerDollar(o).has_value());
        o = fixtures::makeOffer(2, "RTX 4090", 1, 0.5, 0.0, 0.0);
        CHECK_FALSE(tflopsPerDollar(o).has_value());  // zero performance is not a meaningful value
        CHECK_FALSE(dlpPerDollar(o).has_value());
        o.totalFlops.reset();
        o.dlperf.reset();
        CHECK_FALSE(tflopsPerDollar(o).has_value());
        CHECK_FALSE(dlpPerDollar(o).has_value());
        o = fixtures::makeOffer(3, "RTX 4090", 1, -0.5, -10.0, std::numeric_limits<double>::quiet_NaN());
        CHECK_FALSE(tflopsPerDollar(o).has_value());
        CHECK_FALSE(dlpPerDollar(o).has_value());
        CHECK_FALSE(safeRatio(1.0, std::numeric_limits<double>::infinity()).has_value());
        CHECK_FALSE(safeRatio(std::numeric_limits<double>::infinity(), 1.0).has_value());
    }

    TEST_CASE("messy JSON: strings, nulls, missing") {
        const json arr = json::parse(fixtures::kMessyOffers);
        const Offer a = arr[0].get<Offer>();
        const Offer b = arr[1].get<Offer>();
        CHECK(a.numGpus == 2);
        CHECK(a.machineId == 11);
        CHECK(tflopsPerDollar(a).value() == doctest::Approx(71.2 / 0.40));
        CHECK_FALSE(dlpPerDollar(a).has_value());     // dlperf null
        CHECK_FALSE(tflopsPerDollar(b).has_value());  // dph 0
        CHECK(a.reliability.value() == doctest::Approx(0.97));
        CHECK(b.reliability.value() == doctest::Approx(0.99));
    }

    TEST_CASE("formatting shows an em dash for missing values") {
        CHECK(formatMetric(std::nullopt, 4) == std::string(kMissing));
        CHECK(formatMetric(0.012345, 4) == "0.0123");
        CHECK(formatMetric(std::numeric_limits<double>::quiet_NaN(), 2) == std::string(kMissing));
    }

    TEST_CASE("colour ranks: 0 = best (most per dollar), 1 = worst") {
        std::vector<Offer> offers{
            fixtures::makeOffer(1, "RTX 4090", 1, 1.0, 100.0, 100.0),  // 100 TFLOPS/$/hr
            fixtures::makeOffer(2, "RTX 4090", 1, 2.0, 100.0, 100.0),  // 50
            fixtures::makeOffer(3, "RTX 4090", 1, 4.0, 100.0, 100.0),  // 25
            fixtures::makeOffer(4, "RTX 4090", 1, 1.0, 0.0, 0.0),      // missing
        };
        const auto m = computeMetrics(offers);
        REQUIRE(m.size() == 4);
        CHECK(*m[0].rankTflopsPerDollar == doctest::Approx(0.0));
        CHECK(*m[1].rankTflopsPerDollar == doctest::Approx(0.5));
        CHECK(*m[2].rankTflopsPerDollar == doctest::Approx(1.0));
        CHECK_FALSE(m[3].rankTflopsPerDollar.has_value());
        CHECK(*m[0].rankDlpPerDollar == doctest::Approx(0.0));
        CHECK(*m[2].rankDlpPerDollar == doctest::Approx(1.0));
        CHECK_FALSE(m[3].rankDlpPerDollar.has_value());
    }

    TEST_CASE("percentile rank ties and single values") {
        const std::vector<double> sorted{1.0, 2.0, 2.0, 3.0};
        CHECK(percentileRank(2.0, sorted) == doctest::Approx(0.5));
        CHECK(percentileRank(1.0, sorted) == doctest::Approx(0.0));
        CHECK(percentileRank(3.0, sorted) == doctest::Approx(1.0));
        const std::vector<double> one{5.0};
        CHECK(percentileRank(5.0, one) == doctest::Approx(0.0));
    }

    TEST_CASE("median") {
        CHECK_FALSE(median({}).has_value());
        CHECK(median({3.0, 1.0, 2.0}).value() == doctest::Approx(2.0));
        CHECK(median({4.0, 1.0, 2.0, 3.0}).value() == doctest::Approx(2.5));
    }

    TEST_CASE("sorting keeps missing values last in both directions") {
        std::vector<Offer> offers{
            fixtures::makeOffer(1, "RTX 4090", 1, 2.0, 100.0, 100.0),  // 50
            fixtures::makeOffer(2, "RTX 4090", 1, 1.0, 0.0, 100.0),    // missing
            fixtures::makeOffer(3, "RTX 4090", 1, 1.0, 100.0, 100.0),  // 100
        };
        const auto m = computeMetrics(offers);
        const auto desc = sortedOrder(offers, m, SortKey::TflopsPerDollar, true);
        CHECK(desc == std::vector<std::size_t>{2, 0, 1});
        const auto asc = sortedOrder(offers, m, SortKey::TflopsPerDollar, false);
        CHECK(asc == std::vector<std::size_t>{0, 2, 1});
    }

    TEST_CASE("deverified offers need both Unverified and its Deverified sub-option") {
        const Offer dev = fixtures::makeOffer(1, "RTX 4090", 1, 0.5, 80.0, 100.0, 0.99, "deverified");
        const Offer unv = fixtures::makeOffer(2, "RTX 4090", 1, 0.5, 80.0, 100.0, 0.99, "unverified");
        SearchFilters f;
        CHECK_FALSE(passesVerificationFilter(dev, f));
        CHECK(passesVerificationFilter(unv, f));
        f.showDeverified = true;  // sub-option alone has no effect
        CHECK_FALSE(passesVerificationFilter(dev, f));
        f.showUnverified = true;
        CHECK(passesVerificationFilter(dev, f));
        f.showDeverified = false;
        CHECK_FALSE(passesVerificationFilter(dev, f));
    }

    TEST_CASE("reserved discount matches the server and the web console") {
        // Measured on POST /bundles/ (type=reserved) with credit_discount_max 0.4.
        CHECK(reservedDiscount(0.4, 7.0 / 30.0) == doctest::Approx(0.0757).epsilon(0.01));
        CHECK(reservedDiscount(0.4, 1) == doctest::Approx(0.2));
        CHECK(reservedDiscount(0.4, 3) == doctest::Approx(0.3));
        CHECK(reservedDiscount(0.4, 6) == doctest::Approx(0.3429).epsilon(0.001));
        CHECK(reservedDiscount(0.15, 1) == doctest::Approx(0.075));
        CHECK(reservedDiscount(0.0, 6) == 0.0);
        // Web console: $0.496/hr, host max 50% -> 1 month "$267.69 (Save 25%) $0.372/hr", badge "Save 43%" (6 months).
        const auto p1 = reservedPlanForMonths(0.496, 0.5, 1);
        CHECK(p1.discount == doctest::Approx(0.25));
        CHECK(p1.ratePerHour == doctest::Approx(0.372));
        CHECK(p1.amount == doctest::Approx(267.84));
        CHECK(reservedPlanForMonths(0.496, 0.5, 6).discount == doctest::Approx(0.4286).epsilon(0.001));
        // Inverse: the amount of a plan buys exactly that plan.
        for (double months : {0.25, 1.0, 3.0, 6.0}) {
            const auto plan = reservedPlanForMonths(1.25, 0.4, months);
            const auto back = reservedPlanForAmount(1.25, 0.4, plan.amount);
            REQUIRE(back.has_value());
            CHECK(back->months == doctest::Approx(months));
            CHECK(back->discount == doctest::Approx(plan.discount));
        }
        CHECK_FALSE(reservedPlanForAmount(0.0, 0.4, 100).has_value());
        CHECK_FALSE(reservedPlanForAmount(1.0, 0.4, 0).has_value());
        CHECK(reservedPlanForAmount(1.0, 0.0, 720)->months == doctest::Approx(1.0));  // no discount: plain hours
    }

    TEST_CASE("higher-is-better ranks: missing values and single values stay uncoloured") {
        const std::vector<std::optional<double>> three{50.0, std::nullopt, 100.0};
        const auto r = higherIsBetterRanks(three);
        REQUIRE(r.size() == 3);
        CHECK(*r[0] == doctest::Approx(1.0f));  // worst
        CHECK_FALSE(r[1].has_value());
        CHECK(*r[2] == doctest::Approx(0.0f));  // best
        const std::vector<std::optional<double>> one{42.0, std::nullopt};
        const auto r1 = higherIsBetterRanks(one);
        CHECK_FALSE(r1[0].has_value());
        CHECK_FALSE(r1[1].has_value());
    }

    TEST_CASE("merging search samples: one offer per machine + GPU count, cheapest wins") {
        auto offer = [](std::int64_t id, std::int64_t machine, int gpus, double dph) {
            Offer o = fixtures::makeOffer(id, "RTX 4090", gpus, dph, 80.0, 100.0);
            o.machineId = machine;
            return o;
        };
        // Same machine, 2-GPU bundle with a different id (other GPU subset) in the second sample.
        std::vector<std::vector<Offer>> samples{
            {offer(1, 500, 1, 0.30), offer(2, 500, 2, 0.60), offer(3, 600, 1, 0.20)},
            {offer(11, 500, 2, 0.58), offer(12, 700, 4, 1.20), offer(13, 500, 1, 0.30)},
        };
        const auto merged = mergeOfferSamples(samples);
        REQUIRE(merged.size() == 4);
        CHECK(merged[0].id == 1);   // first seen kept on equal price
        CHECK(merged[1].id == 11);  // cheaper duplicate replaces the earlier one, position stays
        CHECK(merged[2].id == 3);
        CHECK(merged[3].id == 12);

        Offer noMachine = offer(99, 0, 1, 0.1);
        noMachine.machineId.reset();
        CHECK(mergeOfferSamples({{noMachine}, {noMachine}}).size() == 1);  // falls back to the offer id
        CHECK(mergeOfferSamples({}).empty());
    }

    TEST_CASE("sort ties are ordered by machine, so equal values don't swap between searches") {
        std::vector<Offer> a{fixtures::makeOffer(5, "RTX 4090", 1, 1.0, 100.0, 100.0),
                             fixtures::makeOffer(3, "RTX 4090", 1, 1.0, 100.0, 100.0)};
        std::vector<Offer> b{a[1], a[0]};  // same offers, arrival order swapped
        const auto oa = sortedOrder(a, computeMetrics(a), SortKey::Price, false);
        const auto ob = sortedOrder(b, computeMetrics(b), SortKey::Price, false);
        CHECK(a[oa[0]].id == 3);
        CHECK(b[ob[0]].id == 3);
    }

    TEST_CASE("incompatible: offer fails the template's requirements") {
        Offer o = fixtures::makeOffer(1, "RTX 4090", 1, 0.5, 80.0, 100.0, 0.99, "verified");
        o.raw["cuda_max_good"] = 12.1;
        o.raw["gpu_ram"] = 24564;
        o.raw["driver_version"] = "535.104.05";
        SearchFilters f;
        CHECK_FALSE(isIncompatible(o, f));  // no template requirement

        // CUDA from the image tag.
        f.templateRequirements = templateRequirements("null", "vastai/pytorch:2.5-cuda12.4");
        CHECK(requiredMinCuda(f.templateRequirements) == doctest::Approx(12.4));
        CHECK(isIncompatible(o, f));
        CHECK(incompatibleReason(o, f).find("cuda_max_good >= 12.4") != std::string::npos);
        o.raw["cuda_max_good"] = 12.4;
        CHECK_FALSE(isIncompatible(o, f));  // equal is compatible
        CHECK(incompatibleReason(o, f).empty());

        // extra_filters as a JSON object (API format, gpu_ram in MB) - availability keys are ignored.
        f.templateRequirements = templateRequirements(R"({"gpu_ram": {"gte": 40000}, "verified": {"eq": true}})", "img:latest");
        CHECK(describeRequirements(f.templateRequirements) == std::vector<std::string>{"gpu_ram >= 40000"});
        CHECK(isIncompatible(o, f));
        o.raw["gpu_ram"] = 81920;
        CHECK_FALSE(isIncompatible(o, f));

        // extra_filters as a vastai query string, merged with the image-tag CUDA (stricter one wins).
        f.templateRequirements = templateRequirements("cuda_vers>=11.8 driver_version>=550", "img:cuda-12.6-runtime");
        CHECK(requiredMinCuda(f.templateRequirements) == doctest::Approx(12.6));
        const auto unmet = unmetRequirements(o, f.templateRequirements);
        CHECK(unmet.size() == 2);  // CUDA 12.4 < 12.6 and driver 535 < 550

        // Fields the offer does not report are not judged; no requirements at all -> nothing incompatible.
        o.raw.erase("cuda_max_good");
        o.raw.erase("driver_version");
        CHECK_FALSE(isIncompatible(o, f));
        f.templateRequirements = templateRequirements("", "docker.io/me/app:latest");
        CHECK(f.templateRequirements.empty());
    }
}
