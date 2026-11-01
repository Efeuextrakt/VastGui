#pragma once

#include "model/Json.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace vast {

// One row of `vastai search offers --raw` (POST /api/v0/bundles/).
struct Offer {
    std::int64_t id = 0;
    std::optional<std::int64_t> machineId;
    std::optional<std::int64_t> hostId;
    int numGpus = 0;
    std::string gpuName;

    std::optional<double> gpuRamMb;
    std::optional<double> gpuTotalRamMb;
    std::optional<double> totalFlops;
    std::optional<double> dlperf;
    std::optional<double> dphTotal;
    std::optional<double> dphBase;
    std::optional<double> minBid;
    std::optional<double> storageCostGbMonth;
    std::optional<double> storageTotalCostHr;
    std::optional<double> inetUpCostGb;
    std::optional<double> inetDownCostGb;
    std::optional<double> reliability;
    std::optional<double> durationSec;
    std::optional<double> cudaMaxGood;
    std::optional<double> computeCap;
    std::optional<double> diskBw;
    std::optional<double> diskSpace;
    std::optional<double> inetUp;
    std::optional<double> inetDown;
    std::optional<double> pcieBw;
    std::optional<double> pciGen;
    std::optional<double> gpuLanes;
    std::optional<double> gpuMemBw;
    std::optional<double> cpuCores;
    std::optional<double> cpuCoresEffective;
    std::optional<double> cpuRamMb;
    std::optional<double> cpuGhz;
    std::optional<double> directPortCount;
    std::optional<double> score;
    std::optional<double> gpuCostPerHour;
    std::optional<double> diskCostPerHour;

    std::string cpuName;
    std::string moboName;
    std::string diskName;
    std::string geolocation;
    std::string verification;
    std::string driverVersion;
    std::string osVersion;
    std::string cpuArch;
    std::string publicIp;
    bool staticIp = false;
    bool rentable = true;
    bool rented = false;
    bool datacenter = false;

    json raw;

    [[nodiscard]] std::string countryCode() const {
        const auto pos = geolocation.rfind(", ");
        if (pos != std::string::npos) return geolocation.substr(pos + 2);
        return geolocation.size() == 2 ? geolocation : std::string{};
    }
    [[nodiscard]] std::optional<double> gpuRamGb() const {
        if (!gpuRamMb) return std::nullopt;
        return *gpuRamMb / 1024.0;
    }
    [[nodiscard]] std::optional<double> totalVramGb() const {
        if (!gpuRamMb) return std::nullopt;
        return *gpuRamMb * (numGpus > 0 ? numGpus : 1) / 1024.0;
    }
    [[nodiscard]] std::optional<double> cpuRamGb() const {
        if (!cpuRamMb) return std::nullopt;
        return *cpuRamMb / 1024.0;
    }
    [[nodiscard]] std::optional<double> durationDays() const {
        if (!durationSec) return std::nullopt;
        return *durationSec / 86400.0;
    }
};

inline void from_json(const json& j, Offer& o) {
    using namespace jsonutil;
    o.id = optInt(j, "id").value_or(0);
    o.machineId = optInt(j, "machine_id");
    o.hostId = optInt(j, "host_id");
    o.numGpus = static_cast<int>(optInt(j, "num_gpus").value_or(0));
    o.gpuName = str(j, "gpu_name");
    o.gpuRamMb = optDouble(j, "gpu_ram");
    o.gpuTotalRamMb = optDouble(j, "gpu_total_ram");
    o.totalFlops = optDouble(j, "total_flops");
    o.dlperf = optDouble(j, "dlperf");
    o.dphTotal = optDouble(j, "dph_total");
    o.dphBase = optDouble(j, "dph_base");
    o.minBid = optDouble(j, "min_bid");
    o.storageCostGbMonth = optDouble(j, "storage_cost");
    o.storageTotalCostHr = optDouble(j, "storage_total_cost");
    o.inetUpCostGb = optDouble(j, "inet_up_cost");
    o.inetDownCostGb = optDouble(j, "inet_down_cost");
    o.reliability = optDouble(j, "reliability2");
    if (!o.reliability) o.reliability = optDouble(j, "reliability");
    o.durationSec = optDouble(j, "duration");
    o.cudaMaxGood = optDouble(j, "cuda_max_good");
    o.computeCap = optDouble(j, "compute_cap");
    o.diskBw = optDouble(j, "disk_bw");
    o.diskSpace = optDouble(j, "disk_space");
    o.inetUp = optDouble(j, "inet_up");
    o.inetDown = optDouble(j, "inet_down");
    o.pcieBw = optDouble(j, "pcie_bw");
    o.pciGen = optDouble(j, "pci_gen");
    o.gpuLanes = optDouble(j, "gpu_lanes");
    o.gpuMemBw = optDouble(j, "gpu_mem_bw");
    o.cpuCores = optDouble(j, "cpu_cores");
    o.cpuCoresEffective = optDouble(j, "cpu_cores_effective");
    o.cpuRamMb = optDouble(j, "cpu_ram");
    o.cpuGhz = optDouble(j, "cpu_ghz");
    o.directPortCount = optDouble(j, "direct_port_count");
    o.score = optDouble(j, "score");
    if (const json* s = find(j, "search")) {
        o.gpuCostPerHour = optDouble(*s, "gpuCostPerHour");
        o.diskCostPerHour = optDouble(*s, "diskHour");
    }
    o.cpuName = str(j, "cpu_name");
    o.moboName = str(j, "mobo_name");
    o.diskName = str(j, "disk_name");
    o.geolocation = str(j, "geolocation");
    o.verification = str(j, "verification");
    if (o.verification.empty()) {
        if (auto v = optBool(j, "verified")) o.verification = *v ? "verified" : "unverified";
    }
    o.driverVersion = str(j, "driver_version");
    o.osVersion = str(j, "os_version");
    o.cpuArch = str(j, "cpu_arch");
    o.publicIp = str(j, "public_ipaddr");
    o.staticIp = optBool(j, "static_ip").value_or(false);
    o.rentable = optBool(j, "rentable").value_or(true);
    o.rented = optBool(j, "rented").value_or(false);
    o.datacenter = optInt(j, "hosting_type").value_or(0) == 1 || optBool(j, "datacenter").value_or(false);
    o.raw = j;
}

}  // namespace vast
