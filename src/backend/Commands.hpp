#pragma once
// Catalogue of vast.ai operations. Each function returns an ApiCall describing
// the REST request, mirroring what the official vastai CLI / SDK sends.

#include "backend/ApiCall.hpp"
#include "logic/SearchFilters.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vast::cmd {

using Id = std::int64_t;

// Account
[[nodiscard]] ApiCall showUser();
[[nodiscard]] ApiCall showIpAddrs();
[[nodiscard]] ApiCall showAuditLogs();
[[nodiscard]] ApiCall showScheduledJobs();
[[nodiscard]] ApiCall deleteScheduledJob(Id id);

// Offers
[[nodiscard]] ApiCall searchOffers(const SearchFilters& filters);

// Instances
[[nodiscard]] ApiCall showInstances();
[[nodiscard]] ApiCall showInstance(Id id);
[[nodiscard]] ApiCall startInstances(const std::vector<Id>& ids);
[[nodiscard]] ApiCall stopInstances(const std::vector<Id>& ids);
[[nodiscard]] ApiCall destroyInstances(const std::vector<Id>& ids);
[[nodiscard]] ApiCall rebootInstance(Id id);
[[nodiscard]] ApiCall recycleInstance(Id id);
[[nodiscard]] ApiCall labelInstance(Id id, const std::string& label);
[[nodiscard]] ApiCall changeBid(Id id, double price);
[[nodiscard]] ApiCall prepayInstance(Id id, double amount);
[[nodiscard]] ApiCall showDeposit(Id id);
[[nodiscard]] ApiCall instanceLogs(Id id, int tail, const std::string& filter, bool daemonLogs);
[[nodiscard]] ApiCall executeCommand(Id id, const std::string& command);
// `vastai copy` between remote locations: "C.<instance>:/path", "V.<volume>:/path", "<connection>:/path",
// "<instance>:/path". Local paths (this PC) are rejected - they need rsync over SSH.
[[nodiscard]] ApiCall copyFiles(const std::string& src, const std::string& dst);
[[nodiscard]] ApiCall cancelCopy(const std::string& dst);

// Port of vastai's parse_vast_url: "[id:]path" or a bare instance id. `id` is null for a plain (local) path,
// a string for "id:path" and a number for a bare id.
struct VastLocation {
    json id;
    std::string path;
};
[[nodiscard]] std::optional<VastLocation> parseVastLocation(const std::string& s, std::string& error);
[[nodiscard]] ApiCall attachSsh(Id instanceId, const std::string& publicKey);
[[nodiscard]] ApiCall detachSsh(Id instanceId, Id sshKeyId);

struct CloudCopyParams {
    std::string src;
    std::string dst;
    Id instanceId = 0;
    std::string connectionId;
    std::string transfer = "Instance To Cloud";  // or "Cloud To Instance"
    bool dryRun = false;
    bool ignoreExisting = false;
};
[[nodiscard]] ApiCall cloudCopy(const CloudCopyParams& p);

struct UpdateInstanceParams {
    std::optional<Id> templateId;
    std::string templateHash;
    std::string image;
    std::string args;
    std::string env;
    std::string onstart;
};
[[nodiscard]] ApiCall updateInstance(Id id, const UpdateInstanceParams& p);

struct SnapshotParams {
    std::string repo;
    std::string registry = "docker.io";
    std::string user;
    std::string password;
    bool pause = true;
};
[[nodiscard]] ApiCall takeSnapshot(Id id, const SnapshotParams& p);

struct CreateInstanceParams {
    Id offerId = 0;
    std::string image;
    double diskGb = 16.0;
    std::string label;
    std::string env;          // docker options, "-e K=V -p 8080:8080"
    std::string onstartCmd;
    std::string templateHash;
    std::string login;
    std::string args;         // only for runtype args
    bool ssh = true;
    bool jupyter = false;
    bool direct = true;
    bool argsMode = false;
    std::optional<double> bidPrice;
    std::optional<Id> linkVolume;
    std::optional<Id> createVolume;
    std::optional<double> volumeSizeGb;
    std::string mountPath;
    std::string volumeLabel;
    bool cancelUnavail = false;
};
[[nodiscard]] ApiCall createInstance(const CreateInstanceParams& p);

// Templates
[[nodiscard]] ApiCall searchTemplates(const std::string& query);
struct TemplateParams {
    std::optional<Id> id;     // required by the REST update endpoint
    std::string name;
    std::string image;
    std::string tag;
    std::string env;
    std::string onstart;
    std::string readme;
    std::string desc;
    std::string searchParams;
    std::string login;
    std::string href;
    std::string repo;
    std::string jupyterDir;
    bool ssh = true;
    bool jupyter = false;
    bool direct = true;
    bool jupyterLab = false;
    bool isPublic = false;
    bool hideReadme = false;
    std::optional<double> diskSpaceGb;
};
[[nodiscard]] ApiCall createTemplate(const TemplateParams& p);
[[nodiscard]] ApiCall updateTemplate(const std::string& hashId, const TemplateParams& p);
[[nodiscard]] ApiCall deleteTemplate(std::optional<Id> id, const std::string& hashId);

// Storage
[[nodiscard]] ApiCall searchVolumes(const std::string& query, double storageGb);
[[nodiscard]] ApiCall showVolumes();
[[nodiscard]] ApiCall createVolume(Id offerId, double sizeGb, const std::string& name);
[[nodiscard]] ApiCall deleteVolume(Id id);
[[nodiscard]] ApiCall cloneVolume(Id source, Id dest, std::optional<double> sizeGb);
[[nodiscard]] ApiCall showConnections();

// Serverless
struct EndpointParams {
    std::string name;
    double minLoad = 0.0;
    double minColdLoad = 0.0;
    double targetUtil = 0.9;
    double coldMult = 2.5;
    int coldWorkers = 5;
    int maxWorkers = 20;
    std::optional<double> maxQueueTime;
    std::optional<double> targetQueueTime;
    std::optional<double> inactivityTimeout;
    std::string state;  // update only: active|suspended|stopped
};
[[nodiscard]] ApiCall showEndpoints();
[[nodiscard]] ApiCall createEndpoint(const EndpointParams& p);
[[nodiscard]] ApiCall updateEndpoint(Id id, const EndpointParams& p);
[[nodiscard]] ApiCall deleteEndpoint(Id id);
[[nodiscard]] ApiCall endpointLogs(Id id, int tail);

struct WorkergroupParams {
    std::string templateHash;
    std::optional<Id> templateId;
    std::string searchParams;
    std::string launchArgs;
    std::optional<double> gpuRam;
    std::string endpointName;
    std::optional<Id> endpointId;
    bool noDefault = false;
};
[[nodiscard]] ApiCall showWorkergroups();
[[nodiscard]] ApiCall createWorkergroup(const WorkergroupParams& p);
[[nodiscard]] ApiCall updateWorkergroup(Id id, const WorkergroupParams& p);
[[nodiscard]] ApiCall deleteWorkergroup(Id id);
[[nodiscard]] ApiCall workergroupLogs(Id id, int tail);
[[nodiscard]] ApiCall updateWorkers(Id id);
[[nodiscard]] ApiCall showDeployments();
[[nodiscard]] ApiCall deleteDeployment(Id id);

// Billing
[[nodiscard]] ApiCall showInvoices(const std::string& startDate, const std::string& endDate);
[[nodiscard]] ApiCall showEarnings(const std::string& startDate, const std::string& endDate, std::optional<Id> machineId);

// Team
[[nodiscard]] ApiCall showMembers();
[[nodiscard]] ApiCall inviteMember(const std::string& email, const std::string& role);
[[nodiscard]] ApiCall removeMember(Id id);
[[nodiscard]] ApiCall showTeamRoles();
[[nodiscard]] ApiCall createTeamRole(const std::string& name, const std::string& permissionsJson);
[[nodiscard]] ApiCall removeTeamRole(const std::string& name);
[[nodiscard]] ApiCall createTeam(const std::string& name);
[[nodiscard]] ApiCall destroyTeam();

// Keys
[[nodiscard]] ApiCall showSshKeys();
[[nodiscard]] ApiCall createSshKey(const std::string& publicKey);
[[nodiscard]] ApiCall updateSshKey(Id id, const std::string& publicKey);
[[nodiscard]] ApiCall deleteSshKey(Id id);
[[nodiscard]] ApiCall showApiKeys();
[[nodiscard]] ApiCall createApiKey(const std::string& name, const std::string& permissionsJson);
[[nodiscard]] ApiCall deleteApiKey(Id id);
[[nodiscard]] ApiCall showEnvVars();
[[nodiscard]] ApiCall createEnvVar(const std::string& name, const std::string& value);
[[nodiscard]] ApiCall updateEnvVar(const std::string& name, const std::string& value);
[[nodiscard]] ApiCall deleteEnvVar(const std::string& name);

// Hosting
struct ListMachineParams {
    Id id = 0;
    std::optional<double> priceGpu;
    std::optional<double> priceDisk;
    std::optional<double> priceInetUp;
    std::optional<double> priceInetDown;
    std::optional<double> priceMinBid;
    std::optional<double> discountRate;
    std::optional<int> minChunk;
    std::string endDate;  // YYYY-MM-DD or epoch
};
[[nodiscard]] ApiCall showMachines();
[[nodiscard]] ApiCall listMachine(const ListMachineParams& p);
[[nodiscard]] ApiCall unlistMachine(Id id);
[[nodiscard]] ApiCall setMinBid(Id id, double price);
[[nodiscard]] ApiCall setDefjob(Id id, double priceGpu, double priceInetUp, double priceInetDown, const std::string& image,
                                const std::string& args);
[[nodiscard]] ApiCall removeDefjob(Id id);
[[nodiscard]] ApiCall scheduleMaint(Id id, double startEpoch, double durationHours, const std::string& category);
[[nodiscard]] ApiCall cancelMaint(Id id);
[[nodiscard]] ApiCall cleanupMachine(Id id);
[[nodiscard]] ApiCall machineReports(Id id);
[[nodiscard]] ApiCall showMaints(const std::vector<Id>& ids);

}  // namespace vast::cmd
