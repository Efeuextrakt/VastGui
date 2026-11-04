#pragma once
// Search filter state for the offer search page. Pure data, no UI dependency.

#include "model/Json.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vast {

enum class RentalType { OnDemand, Interruptible, Reserved };

[[nodiscard]] const char* rentalTypeCliName(RentalType t);   // "on-demand" | "bid" | "reserved"
[[nodiscard]] const char* rentalTypeLabel(RentalType t);

enum class RangeField {
    DphTotal,
    GpuRam,
    TotalVram,
    Tflops,
    Dlperf,
    CpuCores,
    CpuRam,
    DiskSpace,
    DiskBw,
    InetUp,
    InetDown,
    InetUpCost,
    InetDownCost,
    PcieBw,
    PciGen,
    NumGpus,
    DirectPorts,
    ComputeCap,
    TflopsPerDollar,
    DlpPerDollar,
    Count
};

inline constexpr std::size_t kRangeFieldCount = static_cast<std::size_t>(RangeField::Count);

struct RangeMeta {
    RangeField field;
    const char* id;          // persistence key
    const char* label;
    const char* unit;
    const char* queryField;  // vast search field
    double lo;
    double hi;
    double queryScale;       // query value = ui value * queryScale
    int decimals;
    bool logarithmic;
};

[[nodiscard]] std::span<const RangeMeta> rangeMetas();
[[nodiscard]] const RangeMeta& rangeMeta(RangeField f);

struct Range {
    std::optional<double> min;
    std::optional<double> max;
    [[nodiscard]] bool active() const { return min.has_value() || max.has_value(); }
    bool operator==(const Range&) const = default;
};

enum class SortKey {
    Score,
    Price,
    TflopsPerDollar,
    DlpPerDollar,
    Reliability,
    InetDown,
    InetUp,
    DiskBw,
    Duration,
    Vram,
    Cuda,
    Tflops,
    Dlperf,
    NumGpus,
    Count
};

struct SortOption {
    SortKey key;
    const char* id;
    const char* label;
    const char* serverField;  // vast order field
    bool defaultDesc;         // natural "best first" direction
};

[[nodiscard]] std::span<const SortOption> sortOptions();
[[nodiscard]] const SortOption& sortOption(SortKey k);
[[nodiscard]] std::optional<SortKey> sortKeyFromId(std::string_view id);

// Built-in GPU model list, used only until vast.ai's /gpu_types/ list has been loaded once (see GpuCatalog).
[[nodiscard]] std::span<const char* const> knownGpuModels();

struct SearchFilters {
    int gpuCount = 0;  // 0 = any, 1/2/4/8 exact, 9 = 9+
    RentalType rentalType = RentalType::OnDemand;
    std::vector<std::string> gpuModels;
    std::string region;  // "" = Planet Earth, continent name or ISO country code
    SortKey sortKey = SortKey::Score;
    bool sortDesc = true;

    bool secureCloudOnly = false;
    double diskGb = 16.0;
    std::optional<double> minReliability;      // 0..1
    std::optional<double> minDurationDays;
    bool showUnverified = false;
    bool showDeverified = false;  // only effective while showUnverified is set
    bool showIncompatible = false;
    bool showRentedByMe = false;  // offers on machines where the user already has an instance
    bool showUnavailable = false;
    bool staticIpOnly = false;
    std::string minCuda;
    std::string minDriver;
    std::string minUbuntu;
    std::optional<double> templateMinCuda;     // derived from the selected template (image tag, extra_filters)
    std::optional<double> templateDiskGb;      // runtime only: the selected template's recommended disk space
    json templateRequirements = json::object();  // runtime only: template extra_filters + image CUDA, REST query format

    std::array<Range, kRangeFieldCount> ranges{};
    std::string rawQuery;
    int limit = 1000;  // runtime only, taken from Settings::searchLimit (not saved with filters/presets)

    bool hideDefective = true;

    // Disk size the offers are priced for: like the console, at least the template's recommended disk space.
    [[nodiscard]] double effectiveDiskGb() const { return std::max(diskGb, templateDiskGb.value_or(0.0)); }
    [[nodiscard]] Range& range(RangeField f) { return ranges[static_cast<std::size_t>(f)]; }
    [[nodiscard]] const Range& range(RangeField f) const { return ranges[static_cast<std::size_t>(f)]; }
    bool operator==(const SearchFilters&) const = default;
};

void to_json(json& j, const SearchFilters& f);
void from_json(const json& j, SearchFilters& f);

}  // namespace vast
