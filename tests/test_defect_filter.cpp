#include "Fixtures.hpp"
#include "doctest/doctest.h"
#include "logic/DefectFilter.hpp"

using namespace vast;

namespace {
std::vector<Offer> fourGood() {
    return {fixtures::makeOffer(1, "RTX 4090", 1, 0.4, 82.0, 120.0), fixtures::makeOffer(2, "RTX 4090", 1, 0.4, 80.0, 118.0),
            fixtures::makeOffer(3, "RTX 4090", 2, 0.8, 164.0, 240.0), fixtures::makeOffer(4, "RTX 4090", 1, 0.4, 81.0, 122.0)};
}
}  // namespace

TEST_SUITE("DefectFilter") {
    TEST_CASE("healthy offers are not defective") {
        const auto offers = fourGood();
        const auto d = evaluateDefects(offers, DefectThresholds{}, Blacklist{});
        for (const auto& i : d) CHECK_FALSE(i.defective());
    }

    TEST_CASE("deverified alone is not defective; blacklisting decides") {
        auto offers = fourGood();
        offers[1].verification = "deverified";
        CHECK_FALSE(evaluateDefects(offers, DefectThresholds{}, Blacklist{})[1].defective());
        Blacklist b;
        b.machines.insert(*offers[1].machineId);
        CHECK(evaluateDefects(offers, DefectThresholds{}, b)[1].flags == DefectBlacklisted);
    }

    TEST_CASE("reliability threshold is configurable") {
        auto offers = fourGood();
        offers[0].reliability = 0.85;
        CHECK(evaluateDefects(offers, DefectThresholds{}, Blacklist{})[0].flags == DefectLowReliability);
        DefectThresholds t;
        t.minReliability = 0.80;
        CHECK_FALSE(evaluateDefects(offers, t, Blacklist{})[0].defective());
    }

    TEST_CASE("missing or zero required values") {
        auto offers = fourGood();
        offers[2].inetDown.reset();
        offers[2].diskBw = 0.0;
        offers[3].cudaMaxGood.reset();
        const auto d = evaluateDefects(offers, DefectThresholds{}, Blacklist{});
        CHECK((d[2].flags & DefectMissingValues) != 0);
        REQUIRE_FALSE(d[2].reasons.empty());
        CHECK(d[2].reasons.front().find("inet_down") != std::string::npos);
        CHECK(d[2].reasons.front().find("disk_bw") != std::string::npos);
        CHECK((d[3].flags & DefectMissingValues) != 0);
    }

    TEST_CASE("messy fixture: zero inet_up + null dlperf") {
        const auto arr = json::parse(fixtures::kMessyOffers);
        std::vector<Offer> offers{arr[0].get<Offer>(), arr[1].get<Offer>()};
        const auto d = evaluateDefects(offers, DefectThresholds{}, Blacklist{});
        CHECK((d[0].flags & DefectMissingValues) != 0);  // dlperf null
        CHECK(d[1].flags == DefectMissingValues);         // deverified is not a defect; inet_up 0, inet_down missing
    }

    TEST_CASE("performance far below the GPU-model median (per GPU)") {
        auto offers = fourGood();
        offers.push_back(fixtures::makeOffer(5, "RTX 4090", 1, 0.3, 80.0, 60.0));   // DLPerf 50% below median
        offers.push_back(fixtures::makeOffer(6, "RTX 4090", 1, 0.3, 80.0, 90.0));   // ~25% below: fine
        offers.push_back(fixtures::makeOffer(7, "RTX 4090", 4, 1.6, 120.0, 480.0)); // TFLOPS/GPU 30 vs ~81
        const auto d = evaluateDefects(offers, DefectThresholds{}, Blacklist{});
        CHECK(d[4].flags == DefectLowPerformance);
        CHECK_FALSE(d[5].defective());
        CHECK(d[6].flags == DefectLowPerformance);
        CHECK_FALSE(d[2].defective());  // 2x offer: 120 DLPerf per GPU

        DefectThresholds t;
        t.maxBelowMedian = 0.6;  // only flag if > 60% below
        CHECK_FALSE(evaluateDefects(offers, t, Blacklist{})[4].defective());
    }

    TEST_CASE("too few samples of a GPU model: no median judgement") {
        std::vector<Offer> offers{fixtures::makeOffer(1, "H100 SXM", 1, 2.0, 400.0, 300.0),
                                  fixtures::makeOffer(2, "H100 SXM", 1, 2.0, 400.0, 30.0)};
        CHECK_FALSE(evaluateDefects(offers, DefectThresholds{}, Blacklist{})[1].defective());
    }

    TEST_CASE("blacklisted machine or host is not defective") {
        auto offers = fourGood();
        Blacklist b;
        b.machines.insert(*offers[0].machineId);
        b.hosts.insert(*offers[3].hostId);
        const auto d = evaluateDefects(offers, DefectThresholds{}, b);
        CHECK(d[0].flags == DefectBlacklisted);
        CHECK(d[0].blacklisted());
        CHECK_FALSE(d[0].defective());
        CHECK(d[0].reasons.empty());
        CHECK(d[0].blacklistReasons.size() == 1);
        CHECK(d[3].blacklisted());
        CHECK_FALSE(d[1].blacklisted());

        // Blacklisted and defective at the same time.
        offers[0].reliability = 0.1;
        const auto both = evaluateDefects(offers, DefectThresholds{}, b);
        CHECK(both[0].blacklisted());
        CHECK(both[0].defective());
    }

    TEST_CASE("all checks disabled") {
        auto offers = fourGood();
        offers[0].verification = "deverified";
        offers[0].reliability = 0.1;
        offers[0].dlperf.reset();
        DefectThresholds t;
        t.checkReliability = t.checkMissingValues = t.checkLowPerformance = false;
        CHECK_FALSE(evaluateDefects(offers, t, Blacklist{})[0].defective());
    }

    TEST_CASE("thresholds and blacklist JSON round trip") {
        DefectThresholds t;
        t.minReliability = 0.95;
        t.maxBelowMedian = 0.25;
        t.checkLowPerformance = false;
        const DefectThresholds t2 = json(t).get<DefectThresholds>();
        CHECK(t2.minReliability == doctest::Approx(0.95));
        CHECK(t2.maxBelowMedian == doctest::Approx(0.25));
        CHECK_FALSE(t2.checkLowPerformance);

        Blacklist b;
        b.machines = {1, 2};
        b.hosts = {3};
        const Blacklist b2 = json(b).get<Blacklist>();
        CHECK(b2.machines == b.machines);
        CHECK(b2.hosts == b.hosts);
    }
}
