#include "logic/SearchFilters.hpp"

#include <algorithm>

namespace vast {

const char* rentalTypeCliName(RentalType t) {
    switch (t) {
        case RentalType::Interruptible: return "bid";
        case RentalType::Reserved: return "reserved";
        case RentalType::OnDemand: break;
    }
    return "on-demand";
}

const char* rentalTypeLabel(RentalType t) {
    switch (t) {
        case RentalType::Interruptible: return "Interruptible";
        case RentalType::Reserved: return "Reserved";
        case RentalType::OnDemand: break;
    }
    return "On-Demand";
}

namespace {

constexpr std::array<RangeMeta, kRangeFieldCount> kRangeMetas{{
    {RangeField::DphTotal, "dph_total", "$/hr", "$/hr", "dph_total", 0.0, 40.0, 1.0, 3, true},
    {RangeField::GpuRam, "gpu_ram", "GPU RAM", "GB", "gpu_ram", 0.0, 192.0, 1.0, 0, false},
    {RangeField::TotalVram, "gpu_total_ram", "Total VRAM", "GB", "gpu_total_ram", 0.0, 1536.0, 1.0, 0, true},
    {RangeField::Tflops, "total_flops", "TFLOPS", "TFLOPS", "total_flops", 0.0, 2500.0, 1.0, 1, true},
    {RangeField::Dlperf, "dlperf", "DLPerf", "", "dlperf", 0.0, 2500.0, 1.0, 1, true},
    {RangeField::CpuCores, "cpu_cores_effective", "CPU Cores", "cores", "cpu_cores_effective", 0.0, 256.0, 1.0, 0, false},
    {RangeField::CpuRam, "cpu_ram", "CPU RAM", "GB", "cpu_ram", 0.0, 2048.0, 1.0, 0, true},
    {RangeField::DiskSpace, "disk_space", "Disk Space", "GB", "disk_space", 0.0, 20000.0, 1.0, 0, true},
    {RangeField::DiskBw, "disk_bw", "Disk Bandwidth", "MB/s", "disk_bw", 0.0, 20000.0, 1.0, 0, true},
    {RangeField::InetUp, "inet_up", "Inet Up", "Mbps", "inet_up", 0.0, 20000.0, 1.0, 0, true},
    {RangeField::InetDown, "inet_down", "Inet Down", "Mbps", "inet_down", 0.0, 20000.0, 1.0, 0, true},
    {RangeField::InetUpCost, "inet_up_cost", "Upload Cost", "$/TB", "inet_up_cost", 0.0, 100.0, 0.001, 2, false},
    {RangeField::InetDownCost, "inet_down_cost", "Download Cost", "$/TB", "inet_down_cost", 0.0, 100.0, 0.001, 2, false},
    {RangeField::PcieBw, "pcie_bw", "PCIe Bandwidth", "GB/s", "pcie_bw", 0.0, 64.0, 1.0, 1, false},
    {RangeField::PciGen, "pci_gen", "PCIe Gen", "", "pci_gen", 0.0, 6.0, 1.0, 0, false},
    {RangeField::NumGpus, "num_gpus", "GPU Count", "", "num_gpus", 0.0, 16.0, 1.0, 0, false},
    {RangeField::DirectPorts, "direct_port_count", "Direct Ports", "", "direct_port_count", 0.0, 1000.0, 1.0, 0, true},
    {RangeField::ComputeCap, "compute_cap", "Compute Capability", "", "compute_cap", 3.0, 12.0, 100.0, 1, false},
    {RangeField::TflopsPerDollar, "tflops_per_dollar", "TFLOPS/$/hr", "", "flops_per_dphtotal", 0.0, 2000.0, 1.0, 0, true},
    {RangeField::DlpPerDollar, "dlp_per_dollar", "DLP/$/hr", "", "dlperf_per_dphtotal", 0.0, 2000.0, 1.0, 0, true},
}};

constexpr bool metasOrdered() {
    for (std::size_t i = 0; i < kRangeMetas.size(); ++i) {
        if (static_cast<std::size_t>(kRangeMetas[i].field) != i) return false;
    }
    return true;
}
static_assert(metasOrdered(), "kRangeMetas must be ordered by RangeField");

constexpr std::array<SortOption, static_cast<std::size_t>(SortKey::Count)> kSortOptions{{
    {SortKey::Score, "score", "Auto Sort (score)", "score", true},
    {SortKey::Price, "price", "Price ($/hr)", "dph_total", false},
    {SortKey::TflopsPerDollar, "tflops_per_dollar", "TFLOPS/$/hr", "flops_per_dphtotal", true},
    {SortKey::DlpPerDollar, "dlp_per_dollar", "DLP/$/hr", "dlperf_per_dphtotal", true},
    {SortKey::Reliability, "reliability", "Reliability", "reliability", true},
    {SortKey::InetDown, "inet_down", "Inet Down", "inet_down", true},
    {SortKey::InetUp, "inet_up", "Inet Up", "inet_up", true},
    {SortKey::DiskBw, "disk_bw", "Disk Bandwidth", "disk_bw", true},
    {SortKey::Duration, "duration", "Max Duration", "duration", true},
    {SortKey::Vram, "vram", "VRAM", "gpu_ram", true},
    {SortKey::Cuda, "cuda", "CUDA Version", "cuda_max_good", true},
    {SortKey::Tflops, "tflops", "TFLOPS", "total_flops", true},
    {SortKey::Dlperf, "dlperf", "DLPerf", "dlperf", true},
    {SortKey::NumGpus, "num_gpus", "GPU Count", "num_gpus", true},
}};

constexpr bool sortOrdered() {
    for (std::size_t i = 0; i < kSortOptions.size(); ++i) {
        if (static_cast<std::size_t>(kSortOptions[i].key) != i || kSortOptions[i].id == nullptr) return false;
    }
    return true;
}
static_assert(sortOrdered(), "kSortOptions must be ordered by SortKey");

constexpr const char* kGpuModels[] = {
    "RTX 5090",       "RTX 5090D",      "RTX 5080",       "RTX 5070 Ti",  "RTX 5070",       "RTX 5060 Ti",
    "RTX 4090",       "RTX 4090D",      "RTX 4080S",      "RTX 4080",     "RTX 4070S Ti",   "RTX 4070S",
    "RTX 4070 Ti",    "RTX 4070",       "RTX 4060 Ti",    "RTX 4060",     "RTX 3090 Ti",
    "RTX 3090",       "RTX 3080 Ti",    "RTX 3080",       "RTX 3070 Ti",  "RTX 3070",
    "RTX 3060 Ti",    "RTX 3060",       "RTX 2080 Ti",    "RTX PRO 6000 WS", "RTX PRO 6000 S",
    "RTX PRO 4000",   "RTX 6000Ada",    "RTX 5000Ada",    "RTX 4000Ada",  "RTX A6000",
    "RTX A5000",      "RTX A4000",      "A100 SXM4",      "A100 PCIE",    "A100X",
    "A800 PCIE",      "H100 SXM",       "H100 PCIE",      "H100 NVL",     "H200",
    "H200 NVL",       "GH200 SXM",      "B200",           "L40S",         "L40",
    "L4",             "A40",            "A10",            "Tesla V100",   "Tesla T4",
    "Q RTX 8000",     "InstinctMI300X",
};

}  // namespace

std::span<const RangeMeta> rangeMetas() { return kRangeMetas; }

const RangeMeta& rangeMeta(RangeField f) { return kRangeMetas[static_cast<std::size_t>(f)]; }

std::span<const SortOption> sortOptions() { return kSortOptions; }

const SortOption& sortOption(SortKey k) { return kSortOptions[static_cast<std::size_t>(k)]; }

std::optional<SortKey> sortKeyFromId(std::string_view id) {
    for (const auto& o : kSortOptions) {
        if (id == o.id) return o.key;
    }
    return std::nullopt;
}

std::span<const char* const> knownGpuModels() { return kGpuModels; }

namespace {

void putOpt(json& j, const char* key, const std::optional<double>& v) {
    if (v) j[key] = *v;
}

std::optional<double> getOpt(const json& j, const char* key) { return jsonutil::optDouble(j, key); }

}  // namespace

void to_json(json& j, const SearchFilters& f) {
    j = json::object();
    j["gpu_count"] = f.gpuCount;
    j["rental_type"] = rentalTypeCliName(f.rentalType);
    j["gpu_models"] = f.gpuModels;
    j["region"] = f.region;
    j["sort"] = sortOption(f.sortKey).id;
    j["sort_desc"] = f.sortDesc;
    j["secure_cloud"] = f.secureCloudOnly;
    j["disk_gb"] = f.diskGb;
    putOpt(j, "min_reliability", f.minReliability);
    putOpt(j, "min_duration_days", f.minDurationDays);
    j["show_unverified"] = f.showUnverified;
    j["show_deverified"] = f.showDeverified;
    j["show_incompatible"] = f.showIncompatible;
    j["show_rented_by_me"] = f.showRentedByMe;
    j["show_unavailable"] = f.showUnavailable;
    j["static_ip"] = f.staticIpOnly;
    j["min_cuda"] = f.minCuda;
    j["min_driver"] = f.minDriver;
    j["min_ubuntu"] = f.minUbuntu;
    j["raw_query"] = f.rawQuery;
    j["hide_defective"] = f.hideDefective;
    json ranges = json::object();
    for (const auto& m : kRangeMetas) {
        const Range& r = f.range(m.field);
        if (!r.active()) continue;
        json rj = json::object();
        putOpt(rj, "min", r.min);
        putOpt(rj, "max", r.max);
        ranges[m.id] = rj;
    }
    j["ranges"] = ranges;
}

void from_json(const json& j, SearchFilters& f) {
    using namespace jsonutil;
    f = SearchFilters{};
    if (!j.is_object()) return;
    f.gpuCount = static_cast<int>(optInt(j, "gpu_count").value_or(0));
    const std::string rt = str(j, "rental_type", "on-demand");
    f.rentalType = rt == "bid" ? RentalType::Interruptible : rt == "reserved" ? RentalType::Reserved : RentalType::OnDemand;
    if (const json* g = find(j, "gpu_models"); g && g->is_array()) {
        for (const auto& s : *g) {
            if (s.is_string()) f.gpuModels.push_back(s.get<std::string>());
        }
    }
    f.region = str(j, "region");
    const std::string sortId = str(j, "sort", "score");
    if (auto k = sortKeyFromId(sortId)) {
        f.sortKey = *k;
        f.sortDesc = optBool(j, "sort_desc").value_or(sortOption(f.sortKey).defaultDesc);
    } else if (sortId == "price_per_tflops" || sortId == "price_per_dlp") {
        // Removed price-per-performance sorts: "cheapest per TFLOP" == "most TFLOPS per dollar".
        f.sortKey = sortId == "price_per_tflops" ? SortKey::TflopsPerDollar : SortKey::DlpPerDollar;
        f.sortDesc = !optBool(j, "sort_desc").value_or(false);
    }
    f.secureCloudOnly = optBool(j, "secure_cloud").value_or(false);
    f.diskGb = std::clamp(num(j, "disk_gb", 16.0), 1.0, 100000.0);
    f.minReliability = getOpt(j, "min_reliability");
    f.minDurationDays = getOpt(j, "min_duration_days");
    f.showUnverified = optBool(j, "show_unverified").value_or(false);
    f.showDeverified = optBool(j, "show_deverified").value_or(false);
    f.showIncompatible = optBool(j, "show_incompatible").value_or(false);
    f.showRentedByMe = optBool(j, "show_rented_by_me").value_or(false);
    f.showUnavailable = optBool(j, "show_unavailable").value_or(false);
    f.staticIpOnly = optBool(j, "static_ip").value_or(false);
    f.minCuda = str(j, "min_cuda");
    f.minDriver = str(j, "min_driver");
    f.minUbuntu = str(j, "min_ubuntu");
    f.rawQuery = str(j, "raw_query");
    f.hideDefective = optBool(j, "hide_defective").value_or(true);
    if (const json* r = find(j, "ranges"); r && r->is_object()) {
        for (const auto& m : kRangeMetas) {
            if (const json* rj = find(*r, m.id)) {
                Range& dst = f.range(m.field);
                dst.min = getOpt(*rj, "min");
                dst.max = getOpt(*rj, "max");
            }
        }
    }
}

}  // namespace vast
