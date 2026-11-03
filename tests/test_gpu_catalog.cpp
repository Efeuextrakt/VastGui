#include "doctest/doctest.h"
#include "logic/GpuCatalog.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace vast;

namespace {

const GpuGroup* groupOf(const std::vector<GpuGroup>& cat, const std::string& model) {
    for (const auto& g : cat) {
        if (std::find(g.models.begin(), g.models.end(), model) != g.models.end()) return &g;
    }
    return nullptr;
}

std::vector<std::string> groupNames(const std::vector<GpuGroup>& cat) {
    std::vector<std::string> out;
    for (const auto& g : cat) out.push_back(g.name);
    return out;
}

}  // namespace

TEST_SUITE("GpuCatalog") {
    TEST_CASE("names come from active /gpu_types/ entries") {
        const json resp = json::parse(R"({"success": true, "gpu_types": [
            {"canonical_name": "RTX 4090D", "status": "active"},
            {"canonical_name": "RTX 9999", "status": "retired"},
            {"canonical_name": "  H200  "},
            {"canonical_name": ""},
            {"id": 7}
        ]})");
        CHECK(gpuNamesFromTypes(resp) == std::vector<std::string>{"RTX 4090D", "H200"});
        CHECK(gpuNamesFromTypes(resp["gpu_types"]).size() == 2);  // a bare array works too
        CHECK(gpuNamesFromTypes(json::object()).empty());
    }

    TEST_CASE("normalised names are only used for grouping") {
        CHECK(normaliseGpuName("InstinctMI250X") == "Instinct MI250X");
        CHECK(normaliseGpuName("Q RTX 8000") == "Quadro RTX 8000");
        CHECK(normaliseGpuName("RTX 6000Ada") == "RTX 6000 Ada");
        CHECK(normaliseGpuName("GeForce 970") == "GTX 970");
        CHECK(normaliseGpuName("RTX 4060  laptop") == "RTX 4060 Laptop");
        CHECK(normaliseGpuName("RTX 4090D") == "RTX 4090D");  // vast spelling kept
        CHECK(normaliseGpuName("RTX 4080S") == "RTX 4080S");
    }

    TEST_CASE("variants sit next to their model, best first, with vast.ai's spelling") {
        const std::vector<std::string> names = {"RTX 4070",  "RTX 4090D", "RTX 4080S", "RTX 4090 Ti", "RTX 4070S Ti",
                                                "RTX 4090",  "RTX 4080",  "RTX 4070 Ti", "RTX 4070S",  "RTX 4060 laptop",
                                                "RTX 4060"};
        const auto cat = buildGpuCatalog(names);
        REQUIRE(cat.size() == 1);
        CHECK(cat[0].name == "RTX 4000 Series");
        CHECK(cat[0].models == std::vector<std::string>{"RTX 4090 Ti", "RTX 4090", "RTX 4090D", "RTX 4080S", "RTX 4080",
                                                        "RTX 4070S Ti", "RTX 4070 Ti", "RTX 4070S", "RTX 4070", "RTX 4060",
                                                        "RTX 4060 laptop"});
    }

    TEST_CASE("families are grouped and ordered; singled-out cards go to Other") {
        const std::vector<std::string> names = {
            "H100 SXM", "A100 SXM4", "B200",  "L40S",    "Tesla V100", "RTX 5090",  "RTX 5090D", "RTX 3090",
            "RTX 3080", "RTX A6000", "RTX A4000", "InstinctMI250X", "InstinctMI100", "CMP 50HX", "P106-100",
            "RTX 2080 Ti",  // only RTX 20 card -> Other
            "RTX 5090",     // duplicate
        };
        const auto cat = buildGpuCatalog(names);
        CHECK(groupNames(cat) == std::vector<std::string>{"RTX 5000 Series", "RTX 3000 Series", "RTX A (Ampere)",
                                                          "Data Center", "AMD Instinct", "Other"});
        CHECK(groupOf(cat, "RTX 2080 Ti")->name == "Other");
        CHECK(groupOf(cat, "CMP 50HX")->name == "Other");
        CHECK(groupOf(cat, "RTX 5090D")->name == "RTX 5000 Series");
        const GpuGroup* dc = groupOf(cat, "H100 SXM");
        REQUIRE(dc != nullptr);
        CHECK(dc->models == std::vector<std::string>{"B200", "H100 SXM", "A100 SXM4", "L40S", "Tesla V100"});
        std::size_t total = 0;
        for (const auto& g : cat) total += g.models.size();
        CHECK(total == names.size() - 1);  // duplicate removed, nothing lost
    }

    TEST_CASE("unknown names do not crash and land in Other") {
        const auto cat = buildGpuCatalog(std::vector<std::string>{"Some Future GPU", "  ", "XYZ 1"});
        REQUIRE(cat.size() == 1);
        CHECK(cat[0].name == "Other");
        CHECK(cat[0].models.size() == 2);
        CHECK(buildGpuCatalog(std::vector<std::string>{}).empty());
    }
}
