#pragma once
// GPU model catalogue for the Search page's GPU dropdown. No UI dependency.
//
// The model list comes from vast.ai (GET /api/v0/gpu_types/?status=active, the same source the web console uses);
// the names are kept exactly as vast.ai spells them (they are the search values), but are grouped and sorted by
// their normalised form (family, model number, variant) so related cards sit together, e.g. RTX 4090 Ti,
// RTX 4090, RTX 4090D.

#include "model/Json.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vast {

struct GpuGroup {
    std::string name;                 // "RTX 4000 Series"
    std::vector<std::string> models;  // vast.ai names, best first
};

// Canonical names of the active entries of a /gpu_types/ response (array, or object with "gpu_types").
[[nodiscard]] std::vector<std::string> gpuNamesFromTypes(const json& gpuTypes);

// Groups and sorts the names (duplicates removed). Names that fit no family, and families with a single model,
// end up in the last group, "Other".
[[nodiscard]] std::vector<GpuGroup> buildGpuCatalog(std::span<const std::string> names);

// Name used for grouping/sorting only, e.g. "InstinctMI250X" -> "Instinct MI250X", "Q RTX 8000" -> "Quadro RTX 8000".
[[nodiscard]] std::string normaliseGpuName(std::string_view name);

}  // namespace vast
