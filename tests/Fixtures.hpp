#pragma once
// Mock vast.ai offer JSON used by the unit tests (trimmed real-world shapes).

#include "model/Json.hpp"
#include "model/Offer.hpp"

#include <string_view>
#include <vector>

namespace fixtures {

// A healthy RTX 4090 offer as returned by POST /api/v0/bundles/ (subset of fields).
inline constexpr std::string_view kHealthyOffer = R"json({
  "id": 57147106, "machine_id": 61353, "host_id": 256524, "num_gpus": 1, "gpu_name": "RTX 4090",
  "gpu_ram": 24564, "gpu_total_ram": 24564, "total_flops": 81.4, "dlperf": 123.65,
  "dph_total": 0.4759, "dph_base": 0.4667, "min_bid": 0.5333, "storage_cost": 0.6667,
  "inet_up_cost": 0.0390625, "inet_down_cost": 0.0390625, "reliability2": 0.9886,
  "duration": 15551785.1, "cuda_max_good": 13.2, "compute_cap": 890, "disk_bw": 5207.0,
  "disk_space": 587.8, "inet_up": 3773.8, "inet_down": 5835.5, "pcie_bw": 11.4, "pci_gen": 4.0,
  "gpu_lanes": 8, "cpu_cores": 256, "cpu_cores_effective": 19.7, "cpu_ram": 64483,
  "direct_port_count": 76, "geolocation": "China, CN", "verification": "verified",
  "cpu_name": "AMD EPYC 7B13 64-Core Processor", "mobo_name": "ROME2D32GM-2T",
  "disk_name": "GLOWAY YCQ4TNVMe", "driver_version": "595.104.02", "static_ip": false,
  "rentable": true, "rented": false, "hosting_type": 0,
  "search": {"gpuCostPerHour": 0.4667, "diskHour": 0.0093, "totalHour": 0.4759}
})json";

// Offers with numbers encoded as strings, nulls and missing fields.
inline constexpr std::string_view kMessyOffers = R"json([
  {"id": 1, "machine_id": "11", "host_id": 101, "num_gpus": "2", "gpu_name": "RTX 3090",
   "dph_total": "0.40", "total_flops": "71.2", "dlperf": null, "reliability": 0.97,
   "verification": "unverified", "inet_up": 100, "inet_down": 200, "disk_bw": 1000, "cuda_max_good": 12.2},
  {"id": 2, "machine_id": 12, "host_id": 102, "num_gpus": 1, "gpu_name": "RTX 3090",
   "dph_total": 0.0, "total_flops": 35.6, "dlperf": 40.0, "reliability2": 0.99,
   "verification": "deverified", "inet_up": 0, "disk_bw": 900, "cuda_max_good": 12.2}
])json";

inline vast::Offer offerFrom(std::string_view text) { return vast::json::parse(text).get<vast::Offer>(); }

// Builds a synthetic healthy offer with the given performance numbers.
inline vast::Offer makeOffer(std::int64_t id, const char* gpu, int gpus, double dph, double flops, double dlperf,
                             double reliability = 0.99, const char* verification = "verified") {
    vast::json j = vast::json::parse(kHealthyOffer);
    j["id"] = id;
    j["machine_id"] = 1000 + id;
    j["host_id"] = 2000 + id;
    j["gpu_name"] = gpu;
    j["num_gpus"] = gpus;
    j["dph_total"] = dph;
    j["total_flops"] = flops;
    j["dlperf"] = dlperf;
    j["reliability2"] = reliability;
    j["verification"] = verification;
    return j.get<vast::Offer>();
}

}  // namespace fixtures
