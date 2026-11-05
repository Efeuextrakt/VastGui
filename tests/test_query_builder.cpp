#include "doctest/doctest.h"
#include "logic/QueryBuilder.hpp"
#include "logic/SearchFilters.hpp"

#include <algorithm>

using namespace vast;

namespace {
bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }
}  // namespace

TEST_SUITE("QueryBuilder") {
    TEST_CASE("default filters only constrain the container disk size") {
        SearchFilters f;
        CHECK(QueryBuilder::buildQueryString(f) == "disk_space>=16");
        CHECK(QueryBuilder::orderString(f) == "score-");
    }

    TEST_CASE("GPU count toggles") {
        SearchFilters f;
        f.gpuCount = 4;
        CHECK(contains(QueryBuilder::buildQueryString(f), "num_gpus=4"));
        f.gpuCount = 9;
        CHECK(contains(QueryBuilder::buildQueryString(f), "num_gpus>=9"));
    }

    TEST_CASE("GPU models use underscores and list syntax") {
        SearchFilters f;
        f.gpuModels = {"RTX 4090"};
        CHECK(contains(QueryBuilder::buildQueryString(f), "gpu_name=RTX_4090"));
        f.gpuModels = {"RTX 4090", "H100 SXM"};
        CHECK(contains(QueryBuilder::buildQueryString(f), "gpu_name in [RTX_4090,H100_SXM]"));
    }

    TEST_CASE("regions map to geolocation") {
        SearchFilters f;
        f.region = "Oceania";
        CHECK(contains(QueryBuilder::buildQueryString(f), "geolocation in [AU,NZ]"));
        f.region = "DE";
        CHECK(contains(QueryBuilder::buildQueryString(f), "geolocation=DE"));
    }

    TEST_CASE("machine options") {
        SearchFilters f;
        f.secureCloudOnly = true;
        f.showUnverified = true;
        f.showUnavailable = true;
        f.staticIpOnly = true;
        f.minReliability = 0.95;
        f.minDurationDays = 7;
        const auto q = QueryBuilder::buildQueryString(f);
        CHECK(contains(q, "datacenter=true"));
        CHECK(contains(q, "verified=any"));
        CHECK(contains(q, "rentable=any"));
        CHECK(contains(q, "static_ip=true"));
        CHECK(contains(q, "reliability>=0.95"));
        CHECK(contains(q, "duration>=7"));
    }

    TEST_CASE("CUDA requirement merges text filter and template, unless incompatible machines are shown") {
        SearchFilters f;
        f.minCuda = "12.1";
        f.templateMinCuda = 12.4;
        CHECK(contains(QueryBuilder::buildQueryString(f), "cuda_max_good>=12.4"));
        f.showIncompatible = true;
        CHECK(contains(QueryBuilder::buildQueryString(f), "cuda_max_good>=12.1"));
    }

    TEST_CASE("driver version is normalised to X.Y.Z") {
        SearchFilters f;
        f.minDriver = "535";
        CHECK(contains(QueryBuilder::buildQueryString(f), "driver_version>=535.0.0"));
        CHECK(QueryBuilder::numericDriverVersion("535.104.05") == 535104005LL);
        CHECK_FALSE(QueryBuilder::numericDriverVersion("535.104").has_value());
    }

    TEST_CASE("range sliders apply unit scaling; per-dollar ranges are server-side") {
        SearchFilters f;
        f.range(RangeField::DphTotal).max = 1.5;
        f.range(RangeField::GpuRam).min = 24;
        f.range(RangeField::InetUpCost).max = 5;   // $/TB -> $/GB
        f.range(RangeField::ComputeCap).min = 8.6; // -> 860
        f.range(RangeField::TflopsPerDollar).min = 150;
        f.range(RangeField::DlpPerDollar).min = 200;
        const auto q = QueryBuilder::buildQueryString(f);
        CHECK(contains(q, "dph_total<=1.5"));
        CHECK(contains(q, "gpu_ram>=24"));
        CHECK(contains(q, "inet_up_cost<=0.005"));
        CHECK(contains(q, "compute_cap>=860"));
        CHECK(contains(q, "flops_per_dphtotal>=150"));
        CHECK(contains(q, "dlperf_per_dphtotal>=200"));
        CHECK(QueryBuilder::parseQuery(q).ok());
    }

    TEST_CASE("disk range minimum merges with the container size") {
        SearchFilters f;
        f.diskGb = 50;
        f.range(RangeField::DiskSpace).min = 100;
        f.range(RangeField::DiskSpace).max = 500;
        const auto q = QueryBuilder::buildQueryString(f);
        CHECK(contains(q, "disk_space>=100"));
        CHECK(contains(q, "disk_space<=500"));
        CHECK_FALSE(contains(q, "disk_space>=50"));
    }

    TEST_CASE("raw query is appended last") {
        SearchFilters f;
        f.gpuCount = 1;
        f.rawQuery = "  bw_nvlink>0 ";
        const auto q = QueryBuilder::buildQueryString(f);
        CHECK(q.size() >= 11);
        CHECK(q.substr(q.size() - 11) == "bw_nvlink>0");
    }

    TEST_CASE("sort orders map to server fields") {
        SearchFilters f;
        f.sortKey = SortKey::Price;
        f.sortDesc = false;
        CHECK(QueryBuilder::orderString(f) == "dph_total");
        f.sortKey = SortKey::TflopsPerDollar;
        f.sortDesc = true;
        CHECK(QueryBuilder::orderString(f) == "flops_per_dphtotal-");
        f.sortKey = SortKey::DlpPerDollar;
        CHECK(QueryBuilder::orderString(f) == "dlperf_per_dphtotal-");
    }

    TEST_CASE("legacy price-per-performance sorts migrate to per-dollar sorts") {
        SearchFilters a = json{{"sort", "price_per_tflops"}, {"sort_desc", false}}.get<SearchFilters>();
        CHECK(a.sortKey == SortKey::TflopsPerDollar);
        CHECK(a.sortDesc);  // cheapest per TFLOP first == most TFLOPS per dollar first
        SearchFilters b = json{{"sort", "price_per_dlp"}, {"sort_desc", true}}.get<SearchFilters>();
        CHECK(b.sortKey == SortKey::DlpPerDollar);
        CHECK_FALSE(b.sortDesc);
    }

    TEST_CASE("parseQuery mirrors vastai parse_query") {
        auto r = QueryBuilder::parseQuery("num_gpus=1 gpu_name=RTX_4090 gpu_ram>=24 duration>=1 reliability > 0.9");
        REQUIRE(r.ok());
        CHECK(r.query["num_gpus"]["eq"] == "1");
        CHECK(r.query["gpu_name"]["eq"] == "RTX 4090");
        CHECK(r.query["gpu_ram"]["gte"].get<double>() == doctest::Approx(24000.0));
        CHECK(r.query["duration"]["gte"].get<double>() == doctest::Approx(86400.0));
        CHECK(r.query["reliability"]["gt"] == "0.9");
        CHECK(r.warnings.empty());
    }

    TEST_CASE("parseQuery lists, aliases, booleans, wildcard and driver versions") {
        json base = {{"verified", {{"eq", true}}}, {"rentable", {{"eq", true}}}};
        auto r = QueryBuilder::parseQuery(
            "gpu_name in [RTX_4090,RTX_5090] dph<1.5 static_ip=true verified=any driver_version>=535.104.05", base);
        REQUIRE(r.ok());
        CHECK(r.query["gpu_name"]["in"] == json::array({"RTX 4090", "RTX 5090"}));
        CHECK(r.query["dph_total"]["lt"] == "1.5");
        CHECK(r.query["static_ip"]["eq"] == true);
        CHECK_FALSE(r.query.contains("verified"));
        CHECK(r.query["rentable"]["eq"] == true);
        CHECK(r.query["driver_version"]["gte"] == 535104005LL);
    }

    TEST_CASE("parseQuery errors and warnings") {
        CHECK_FALSE(QueryBuilder::parseQuery("num_gpus").ok());
        CHECK_FALSE(QueryBuilder::parseQuery("num_gpus=1 &&").ok());
        CHECK_FALSE(QueryBuilder::parseQuery("gpu_ram>=lots").ok());
        CHECK_FALSE(QueryBuilder::parseQuery("num_gpus>any").ok());
        const auto w = QueryBuilder::parseQuery("not_a_field=3");
        CHECK(w.ok());
        CHECK(w.warnings.size() == 1);
    }

    TEST_CASE("REST body contains CLI defaults, order, type and storage") {
        SearchFilters f;
        f.rentalType = RentalType::Interruptible;
        const json b = QueryBuilder::buildRestBody(f);
        CHECK(b["verified"]["eq"] == true);
        CHECK(b["external"]["eq"] == false);
        CHECK(b["rentable"]["eq"] == true);
        CHECK(b["cpu_arch"]["in"] == json::array({"amd64", "arm64"}));
        CHECK(b["order"] == json::array({json::array({"score", "desc"})}));
        CHECK(b["type"] == "bid");
        CHECK(b["limit"] == 1000);
        CHECK(b["allocated_storage"].get<double>() == doctest::Approx(16.0));
        CHECK(b["disk_space"]["gte"] == "16");

        // Like the console, offers are priced for at least the template's recommended disk.
        f.templateDiskGb = 32.0;
        CHECK(QueryBuilder::buildRestBody(f)["allocated_storage"].get<double>() == doctest::Approx(32.0));
        CHECK(QueryBuilder::buildRestBody(f)["disk_space"]["gte"] == "32");
        f.diskGb = 50.0;
        CHECK(QueryBuilder::buildRestBody(f)["allocated_storage"].get<double>() == doctest::Approx(50.0));
        f.diskGb = 16.0;
        f.templateDiskGb.reset();

        f.showUnverified = true;
        CHECK_FALSE(QueryBuilder::buildRestBody(f).contains("verified"));
        f.rawQuery = "gpu_ram>=oops";
        CHECK_THROWS_AS((void)QueryBuilder::buildRestBody(f), std::invalid_argument);
    }

    TEST_CASE("shell quoting for docker options") {
        CHECK(QueryBuilder::quoteArgPosix("simple") == "simple");
        CHECK(QueryBuilder::quoteArgPosix("a b") == "'a b'");
        CHECK(QueryBuilder::quoteArgPosix("it's") == "'it'\\''s'");
    }

    TEST_CASE("CUDA version from image tag") {
        CHECK(QueryBuilder::cudaFromImageTag("pytorch/pytorch:2.4.0-cuda12.4-cudnn9-runtime").value_or(0) == doctest::Approx(12.4));
        CHECK(QueryBuilder::cudaFromImageTag("nvidia/cuda:12.1.0-runtime-ubuntu22.04").value_or(0) == doctest::Approx(12.1));
        CHECK(QueryBuilder::cudaFromImageTag("vastai/pytorch:@vastai-automatic-tag") == std::nullopt);
    }

    TEST_CASE("SearchFilters JSON round trip") {
        SearchFilters f;
        f.gpuCount = 2;
        f.gpuModels = {"A100 SXM4"};
        f.region = "Europe";
        f.rentalType = RentalType::Reserved;
        f.sortKey = SortKey::DlpPerDollar;
        f.sortDesc = false;
        f.minReliability = 0.97;
        f.range(RangeField::Tflops).min = 50;
        f.range(RangeField::DlpPerDollar).min = 150;
        f.rawQuery = "bw_nvlink>0";
        f.machineIds = "12345 678";
        f.hostIds = "999";
        f.showDefective = true;
        const json j = f;
        const SearchFilters back = j.get<SearchFilters>();
        CHECK(back == f);

        // Older files: hide_defective (default true) instead of show_defective; invalid GPU counts fall back to ANY.
        CHECK_FALSE(json{{"hide_defective", true}}.get<SearchFilters>().showDefective);
        CHECK(json{{"hide_defective", false}}.get<SearchFilters>().showDefective);
        CHECK_FALSE(json::object().get<SearchFilters>().showDefective);
        CHECK(json{{"gpu_count", 3}}.get<SearchFilters>().gpuCount == 0);
        CHECK(json{{"gpu_count", 8}}.get<SearchFilters>().gpuCount == 8);
    }

    TEST_CASE("machine and host ID filters") {
        const auto l = QueryBuilder::parseIdList(" 12345, 678;#91011  12345 abc m:5 ");
        CHECK(l.ids == std::vector<long long>{12345, 678, 91011});
        CHECK(l.invalid == std::vector<std::string>{"abc", "m:5"});
        CHECK(QueryBuilder::parseIdList("").ids.empty());

        SearchFilters f;
        f.machineIds = "51760";
        f.hostIds = "331860, 371998";
        const std::string q = QueryBuilder::buildQueryString(f);
        CHECK(q.find("machine_id=51760") != std::string::npos);
        CHECK(q.find("host_id in [331860,371998]") != std::string::npos);
        const json b = QueryBuilder::buildRestBody(f);
        CHECK(b["machine_id"]["eq"] == "51760");  // the vastai query parser sends strings; the server accepts them
        CHECK(b["host_id"]["in"] == json::array({"331860", "371998"}));
        f.machineIds = "abc";  // nothing valid: no constraint
        CHECK_FALSE(QueryBuilder::buildRestBody(f).contains("machine_id"));
    }
}
