#pragma once

#include "model/Json.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace vast {

// One row of `vastai show machines --raw` (host side).
struct Machine {
    std::int64_t id = 0;
    std::string hostname;
    std::string gpuName;
    int numGpus = 0;
    bool listed = false;
    std::string verification;
    std::optional<double> reliability;
    std::optional<double> listedGpuCost;
    std::optional<double> minBidPrice;
    std::optional<double> listedStorageCost;
    std::optional<double> listedInetUpCost;
    std::optional<double> listedInetDownCost;
    std::optional<double> endDate;
    std::optional<double> earnDay;
    std::optional<double> rentalsRunning;
    std::optional<double> numReports;
    std::optional<double> diskSpace;
    std::string gpuOccupancy;
    json raw;
};

inline void from_json(const json& j, Machine& m) {
    using namespace jsonutil;
    m.id = optInt(j, "id").value_or(optInt(j, "machine_id").value_or(0));
    m.hostname = str(j, "hostname");
    m.gpuName = str(j, "gpu_name");
    m.numGpus = static_cast<int>(optInt(j, "num_gpus").value_or(0));
    m.listed = optBool(j, "listed").value_or(false);
    m.verification = str(j, "verification");
    m.reliability = optDouble(j, "reliability2");
    if (!m.reliability) m.reliability = optDouble(j, "reliability");
    m.listedGpuCost = optDouble(j, "listed_gpu_cost");
    m.minBidPrice = optDouble(j, "min_bid_price");
    m.listedStorageCost = optDouble(j, "listed_storage_cost");
    m.listedInetUpCost = optDouble(j, "listed_inet_up_cost");
    m.listedInetDownCost = optDouble(j, "listed_inet_down_cost");
    m.endDate = optDouble(j, "end_date");
    m.earnDay = optDouble(j, "earn_day");
    m.rentalsRunning = optDouble(j, "current_rentals_running");
    m.numReports = optDouble(j, "num_reports");
    m.diskSpace = optDouble(j, "disk_space");
    m.gpuOccupancy = str(j, "gpu_occupancy");
    m.raw = j;
}

// One row of `vastai show volumes --raw`.
struct Volume {
    std::int64_t id = 0;
    std::string name;
    std::string status;
    std::optional<double> sizeGb;
    std::optional<std::int64_t> machineId;
    std::optional<double> dph;
    std::string geolocation;
    std::string instances;
    json raw;
};

inline void from_json(const json& j, Volume& v) {
    using namespace jsonutil;
    v.id = optInt(j, "id").value_or(0);
    v.name = str(j, "name", str(j, "label"));
    v.status = str(j, "status", str(j, "actual_status"));
    v.sizeGb = optDouble(j, "disk_space");
    if (!v.sizeGb) v.sizeGb = optDouble(j, "size");
    v.machineId = optInt(j, "machine_id");
    v.dph = optDouble(j, "dph_total");
    if (!v.dph) v.dph = optDouble(j, "storage_cost");
    v.geolocation = str(j, "geolocation");
    if (const json* i = find(j, "instances")) v.instances = i->is_string() ? i->get<std::string>() : i->dump();
    v.raw = j;
}

struct SshKey {
    std::int64_t id = 0;
    std::string key;
    std::optional<double> createdAt;
    json raw;
};

inline void from_json(const json& j, SshKey& k) {
    using namespace jsonutil;
    k.id = optInt(j, "id").value_or(0);
    k.key = str(j, "public_key", str(j, "ssh_key", str(j, "key")));
    k.createdAt = optDouble(j, "created_at");
    k.raw = j;
}

}  // namespace vast
