#pragma once

#include "model/Json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace vast {

// Status as the vast.ai web console shows it (label in lower case, e.g. "outbid", "scheduling", "inactive").
struct InstanceStatus {
    std::string label;
    std::string help;     // explanation shown as tooltip
    bool outbid = false;
};

[[nodiscard]] inline double instanceNowEpoch() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// One row of `vastai show instances --raw`.
struct Instance {
    std::int64_t id = 0;
    std::string label;
    std::string actualStatus;
    std::string intendedStatus;
    std::string curState;
    std::string nextState;
    std::string statusMsg;
    std::string gpuName;
    int numGpus = 0;
    std::string image;
    std::string imageRuntype;
    std::string sshHost;
    std::optional<int> sshPort;
    std::string publicIp;
    std::string jupyterToken;
    std::string templateName;
    std::string templateHashId;
    std::optional<std::int64_t> templateId;
    std::string geolocation;
    std::optional<std::int64_t> machineId;
    std::optional<std::int64_t> hostId;
    std::optional<double> dphTotal;
    std::optional<double> totalFlops;
    std::optional<double> dlperf;
    std::optional<double> creditDiscountMax;  // host's max reserved discount (0..1)
    std::optional<double> creditDiscount;     // active reserved discount (0..1)
    std::optional<double> creditBalance;      // prepaid credit left on this instance
    std::optional<double> endDate;            // offer end (epoch s); reservations cannot run past it
    std::optional<double> startDate;
    std::optional<double> gpuUtil;
    std::optional<double> gpuTemp;
    std::optional<double> vmemUsage;
    std::optional<double> gpuRamMb;
    std::optional<double> diskUsage;
    std::optional<double> diskSpace;
    std::optional<double> diskUtil;
    std::optional<double> memUsage;
    std::optional<double> memLimit;
    std::optional<double> cpuUtil;
    std::optional<double> inetUp;
    std::optional<double> inetDown;
    std::optional<double> minBid;
    std::optional<double> hostRunTime;    // used by the console's status logic (scheduling / outbid)
    std::optional<double> clientRunTime;
    std::optional<double> duration;       // seconds left in the contract; < 1 means expired
    std::optional<int> directPortStart;
    std::optional<int> directPortEnd;
    bool isBid = false;
    json ports = json::object();
    json raw;

    [[nodiscard]] const std::string& status() const {
        return actualStatus.empty() ? (curState.empty() ? intendedStatus : curState) : actualStatus;
    }

    // Port of the status logic of the vast.ai web console (rowMeta() in cloud.vast.ai's instance list), so VastGui
    // shows "outbid", "scheduling", "inactive", ... exactly when the console does. `user` is the `show user` JSON
    // (balance < 0 => "insufficient credits"); pass null when unknown.
    [[nodiscard]] InstanceStatus consoleStatus(double now, const json& user = nullptr) const {
        static constexpr const char* kOffline =
            "The machine hosting this instance is not connected to vast.ai. You are not billed for GPU time while it "
            "is offline; charges resume when it comes back. Stop the instance to avoid that.";
        static constexpr const char* kStopping = "Stopping the instance (usually 15-45 s).";
        static constexpr const char* kScheduling =
            "Waiting to be scheduled: the GPUs are in use and the instance starts when they are free again, which "
            "can take hours to weeks. You can copy its data to a running instance.";
        static constexpr const char* kStarting = "Starting - this should not take long.";
        static constexpr const char* kInactive = "Intentionally inactive and not scheduled to run.";
        static constexpr const char* kInactiveExpired =
            "The contract expired and the instance is not scheduled to run; it can be restarted if the host extends "
            "the availability.";
        static constexpr const char* kError = "Something went wrong - contact vast.ai support.";
        static constexpr const char* kDockerError =
            "The docker image has not started loading - most likely an invalid image tag.";
        static constexpr const char* kLoading = "Loading the image (seconds if cached, up to hours if not).";
        static constexpr const char* kUnknown = "Unknown state.";
        static constexpr const char* kCredits = "Insufficient credits to run this instance - add credits.";
        static constexpr const char* kOutbid =
            "Outbid: this interruptible instance is not running because of a higher bid or an on-demand rental.";

        const std::string& a = actualStatus;
        const std::string& cur = curState;
        const std::string& next = nextState;
        const bool expired = endDate && *endDate < now + 1.1;
        const double host = hostRunTime.value_or(0.0);   // JS null compares like 0
        const double client = clientRunTime.value_or(0.0);
        const bool outbidByPrice = isBid && dphTotal && minBid.value_or(0.0) * 1.0001 > *dphTotal;

        std::string w;
        std::string help;
        auto set = [&](const char* label, std::string tip) {
            w = label;
            help = std::move(tip);
        };
        auto inactive = [&] { expired ? set("unavailable", kInactiveExpired) : set("inactive", kInactive); };

        if (a == "running") {
            if (next == "stopped") set("stopping", kStopping);
        } else if (a.empty()) {
            if (cur == "stopped") set("initializing", kStarting);
            else if (cur == "running") set("creating", kStarting);
            else set("unknown", kUnknown);
        } else if (a == "stopped" || a == "exited" || a == "offline") {
            if (next != "running" || a == "offline") {
                const bool off = a == "offline";
                if (outbidByPrice) {
                    set("outbid", off ? kOffline : kOutbid);
                } else if (cur == "stopped" && host < 1e3) {
                    off ? set("offline", kOffline) : set("scheduling", kScheduling);
                } else if (cur == "running" && host > 1e3 && off) {
                    set("offline", kOffline);
                } else if (off) {
                    set("offline", kOffline);
                } else {
                    inactive();
                }
            } else if (cur == "running") {
                set("starting", kStarting);
            } else {
                set("inactive", kInactive);
            }
            if (user.is_object() && jsonutil::optDouble(user, "balance").value_or(0.0) < 0 &&
                !jsonutil::optBool(user, "billing_creditonly").value_or(false)) {
                set("insufficient credits", kCredits);
            }
        } else if (a == "created") {
            set("starting", kStarting);
            if (cur == "stopped") set("scheduling", kScheduling);
            if (next == "stopped") {
                if (outbidByPrice) set("outbid", kOutbid);
                else inactive();
            }
        } else if (a == "missing") {
            set("error", kError);
        } else if (a == "loading") {
            set("loading", kLoading);
            std::string lower = statusMsg;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (statusMsg.empty()) {
                const double minutes = startDate ? (now - *startDate) / 60.0 : 0.0;
                minutes > 5 ? set("error", kDockerError) : set("creating", kStarting);
            } else if (lower.find("error") != std::string::npos && intendedStatus == "stopped" && cur == "stopped") {
                set("error", statusMsg + " Destroy this instance and try again with a new machine.");
            }
        } else if (a == "unknown") {
            set("unknown", kUnknown);
        } else if (a == "rebooting") {
            set("rebooting", "Rebooting the instance.");
        } else {
            set(a.c_str(), "");
        }

        if (isBid) {
            if (next != "running") {
                if (outbidByPrice || (cur == "stopped" && host < 1e3)) set("outbid", kOutbid);
                else inactive();
            }
            if (next == "stopped") {
                if (client > 1e3) set("outbid", kOutbid);
                else if (a == "created") set("scheduling", kScheduling);
                else if (host > 1e3) inactive();
            }
        }
        if (duration && *duration < 1) set("expired", kInactiveExpired);

        InstanceStatus st;
        st.label = w.empty() ? (a.empty() ? std::string("provisioning") : a) : w;
        st.help = help;
        st.outbid = st.label == "outbid";
        return st;
    }

    [[nodiscard]] bool isOutbid(double now = instanceNowEpoch()) const { return consoleStatus(now).outbid; }

    // Status label shown in the UI (same as the vast.ai console).
    [[nodiscard]] std::string displayStatus(double now = instanceNowEpoch()) const { return consoleStatus(now).label; }

    [[nodiscard]] std::optional<int> hostPort(const std::string& containerPort) const {
        const json* p = jsonutil::find(ports, containerPort);
        if (!p || !p->is_array() || p->empty()) return std::nullopt;
        if (auto hp = jsonutil::optInt((*p)[0], "HostPort")) return static_cast<int>(*hp);
        return std::nullopt;
    }

    // Mirrors `vastai ssh-url`: direct port mapping first, ssh proxy otherwise.
    [[nodiscard]] std::optional<std::pair<std::string, int>> sshEndpoint() const {
        if (auto p = hostPort("22/tcp"); p && !publicIp.empty()) return std::pair{publicIp, *p};
        if (sshHost.empty() || !sshPort) return std::nullopt;
        const bool jupyter = imageRuntype.find("jupyter") != std::string::npos;
        return std::pair{sshHost, *sshPort + (jupyter ? 1 : 0)};
    }

    [[nodiscard]] std::string sshCommand() const {
        auto ep = sshEndpoint();
        if (!ep) return {};
        return std::format("ssh -p {} root@{} -L 8080:localhost:8080", ep->second, ep->first);
    }

    [[nodiscard]] std::string sshUrl() const {
        auto ep = sshEndpoint();
        return ep ? std::format("ssh://root@{}:{}", ep->first, ep->second) : std::string{};
    }

    [[nodiscard]] std::string scpUrl() const {
        auto ep = sshEndpoint();
        return ep ? std::format("scp://root@{}:{}", ep->first, ep->second) : std::string{};
    }

    [[nodiscard]] std::optional<std::string> jupyterUrl() const {
        if (jupyterToken.empty()) return std::nullopt;
        if (auto p = hostPort("8080/tcp"); p && !publicIp.empty()) {
            return std::format("https://{}:{}/?token={}", publicIp, *p, jupyterToken);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string portsSummary() const {
        std::string out;
        if (!ports.is_object()) return out;
        for (const auto& [k, v] : ports.items()) {
            if (!v.is_array() || v.empty()) continue;
            if (!out.empty()) out += ", ";
            out += std::format("{}->{}", jsonutil::str(v[0], "HostPort"), k);
        }
        return out;
    }

    [[nodiscard]] std::optional<double> uptimeHours(double nowEpoch) const {
        if (!startDate || *startDate <= 0) return std::nullopt;
        return (nowEpoch - *startDate) / 3600.0;
    }
};

inline void from_json(const json& j, Instance& o) {
    using namespace jsonutil;
    o.id = optInt(j, "id").value_or(0);
    o.label = str(j, "label");
    o.actualStatus = str(j, "actual_status");
    o.intendedStatus = str(j, "intended_status");
    o.curState = str(j, "cur_state");
    o.nextState = str(j, "next_state");
    o.statusMsg = str(j, "status_msg");
    o.gpuName = str(j, "gpu_name");
    o.numGpus = static_cast<int>(optInt(j, "num_gpus").value_or(0));
    o.image = str(j, "image_uuid");
    o.imageRuntype = str(j, "image_runtype");
    o.sshHost = str(j, "ssh_host");
    if (auto p = optInt(j, "ssh_port")) o.sshPort = static_cast<int>(*p);
    o.publicIp = str(j, "public_ipaddr");
    while (!o.publicIp.empty() && (o.publicIp.back() == '\n' || o.publicIp.back() == ' ')) o.publicIp.pop_back();
    o.jupyterToken = str(j, "jupyter_token");
    o.templateName = str(j, "template_name");
    o.templateHashId = str(j, "template_hash_id");
    o.templateId = optInt(j, "template_id");
    o.geolocation = str(j, "geolocation");
    o.machineId = optInt(j, "machine_id");
    o.hostId = optInt(j, "host_id");
    o.dphTotal = optDouble(j, "dph_total");
    o.totalFlops = optDouble(j, "total_flops");
    o.dlperf = optDouble(j, "dlperf");
    o.creditDiscountMax = optDouble(j, "credit_discount_max");
    o.creditDiscount = optDouble(j, "credit_discount");
    o.creditBalance = optDouble(j, "credit_balance");
    o.endDate = optDouble(j, "end_date");
    o.startDate = optDouble(j, "start_date");
    o.gpuUtil = optDouble(j, "gpu_util");
    o.gpuTemp = optDouble(j, "gpu_temp");
    o.vmemUsage = optDouble(j, "vmem_usage");
    o.gpuRamMb = optDouble(j, "gpu_ram");
    o.diskUsage = optDouble(j, "disk_usage");
    o.diskSpace = optDouble(j, "disk_space");
    o.diskUtil = optDouble(j, "disk_util");
    o.memUsage = optDouble(j, "mem_usage");
    o.memLimit = optDouble(j, "mem_limit");
    o.cpuUtil = optDouble(j, "cpu_util");
    o.inetUp = optDouble(j, "inet_up");
    o.inetDown = optDouble(j, "inet_down");
    o.minBid = optDouble(j, "min_bid");
    o.hostRunTime = optDouble(j, "host_run_time");
    o.clientRunTime = optDouble(j, "client_run_time");
    o.duration = optDouble(j, "duration");
    if (auto p = optInt(j, "direct_port_start")) o.directPortStart = static_cast<int>(*p);
    if (auto p = optInt(j, "direct_port_end")) o.directPortEnd = static_cast<int>(*p);
    o.isBid = optBool(j, "is_bid").value_or(false);
    if (const json* p = find(j, "ports"); p && p->is_object()) o.ports = *p;
    o.raw = j;
}

}  // namespace vast
