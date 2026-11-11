#include "backend/Commands.hpp"

#include "logic/DockerOptions.hpp"
#include "logic/Format.hpp"
#include "logic/QueryBuilder.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace vast::cmd {

namespace {

std::string ids(Id id) { return std::to_string(id); }

ApiCall make(std::string title, RestSpec rest, int timeoutSec = 90) {
    ApiCall c;
    c.title = std::move(title);
    c.rest = std::move(rest);
    c.timeout = std::chrono::seconds(timeoutSec);
    return c;
}

// A call that fails immediately with `error` (no request is sent).
ApiCall failing(std::string title, std::string error) {
    ApiCall c;
    c.title = std::move(title);
    c.error = std::move(error);
    return c;
}

RestSpec rest(HttpMethod m, std::string path, std::optional<json> body = std::nullopt, json query = json::object()) {
    RestSpec s;
    s.method = m;
    s.path = std::move(path);
    s.body = std::move(body);
    s.query = std::move(query);
    return s;
}

RestSpec extract(RestSpec s, std::string key) {
    s.transform = RestTransform::ExtractKey;
    s.extractKey = std::move(key);
    return s;
}

// Port of vastai.utils.parse_env: docker options string -> env dict.
json envToJson(const std::string& env) {
    json result = json::object();
    const auto tokens = splitShellArgs(env);
    std::string prev;
    for (const auto& e : tokens) {
        if (prev.empty()) {
            if (e == "-e" || e == "-p" || e == "-h" || e == "-v" || e == "-n") prev = e;
            continue;
        }
        if (prev == "-p") {
            if (e.find_first_not_of("0123456789:tcp/udp") == std::string::npos) result["-p " + e] = "1";
        } else if (prev == "-e") {
            const auto eq = e.find('=');
            if (eq != std::string::npos) {
                std::string val = e.substr(eq + 1);
                while (!val.empty() && (val.front() == '\'' || val.front() == '"')) val.erase(val.begin());
                while (!val.empty() && (val.back() == '\'' || val.back() == '"')) val.pop_back();
                result[e.substr(0, eq)] = val;
            }
        } else if (prev == "-v") {
            result["-v " + e] = "1";
        } else if (prev == "-n") {
            result["-n " + e] = "1";
        } else {
            result[prev] = e;
        }
        prev.clear();
    }
    return result;
}

std::string runtypeFor(bool ssh, bool jupyter, bool direct, bool argsMode) {
    if (argsMode) return "args";
    if (jupyter) return direct ? "jupyter_direc ssh_direc ssh_proxy" : "jupyter_proxy ssh_proxy";
    if (ssh) return direct ? "ssh_direc ssh_proxy" : "ssh_proxy";
    return "ssh";
}

json nullable(const std::string& s) { return s.empty() ? json(nullptr) : json(s); }

template <class T>
json nullable(const std::optional<T>& v) {
    return v ? json(*v) : json(nullptr);
}

}  // namespace

std::optional<VastLocation> parseVastLocation(const std::string& s, std::string& error) {
    VastLocation loc;
    loc.path = s;
    if (const auto colon = s.find(':'); colon != std::string::npos) {
        if (s.find(':', colon + 1) != std::string::npos) {
            error = "invalid location '" + s + "' (expected [id:]path)";
            return std::nullopt;
        }
        loc.id = s.substr(0, colon);
        loc.path = s.substr(colon + 1);
    } else if (!s.empty() && s.size() < 19 && s.find_first_not_of("0123456789") == std::string::npos) {
        loc.id = std::stoll(s);
        loc.path = "/";
    }
    // Same rule as vastai's unix-path regex: no empty path, no "//".
    if (loc.path != "/" && (loc.path.empty() || loc.path.find("//") != std::string::npos ||
                            loc.path.find('\0') != std::string::npos)) {
        error = "path '" + loc.path + "' in '" + s + "' is not a valid Unix path";
        return std::nullopt;
    }
    return loc;
}

// ---- Account ---------------------------------------------------------------

ApiCall showUser() {
    RestSpec s = rest(HttpMethod::Get, "/users/current");
    s.transform = RestTransform::StripApiKey;
    return make("Show user", s, 30);
}

ApiCall showIpAddrs() {
    return make("Show IP addresses",
                extract(rest(HttpMethod::Get, "/users/me/ipaddrs", std::nullopt, {{"owner", "me"}}), "results"));
}

ApiCall showAuditLogs() { return make("Show audit logs", rest(HttpMethod::Get, "/audit_logs/")); }

ApiCall showScheduledJobs() { return make("Show scheduled jobs", rest(HttpMethod::Get, "/commands/schedule_job/")); }

ApiCall deleteScheduledJob(Id id) {
    return make("Delete scheduled job", rest(HttpMethod::Delete, "/commands/schedule_job/" + ids(id) + "/"));
}

// ---- Offers ----------------------------------------------------------------

ApiCall searchOffers(const SearchFilters& filters) {
    try {
        return make("Search offers",
                    extract(rest(HttpMethod::Post, "/bundles/", QueryBuilder::buildRestBody(filters)), "offers"), 60);
    } catch (const std::exception& e) {
        return failing("Search offers", std::string("invalid search query: ") + e.what());
    }
}

ApiCall showGpuTypes() {
    // The GPU model list of the web console's GPU selector; public, works without an API key.
    return make("Load GPU models", extract(rest(HttpMethod::Get, "/gpu_types/", std::nullopt, {{"status", "active"}}), "gpu_types"),
                30);
}

// ---- Instances -------------------------------------------------------------

ApiCall showInstances() {
    return make("Show instances",
                extract(rest(HttpMethod::Get, "/instances/", std::nullopt, {{"owner", "me"}}), "instances"), 60);
}

ApiCall showInstance(Id id) {
    return make("Show instance",
                extract(rest(HttpMethod::Get, "/instances/" + ids(id) + "/", std::nullopt, {{"owner", "me"}}), "instances"));
}

namespace {
ApiCall setState(const char* verb, const char* state, const std::vector<Id>& list) {
    std::string title = std::string(verb) + " instance" + (list.size() == 1 ? "" : "s");
    title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    if (list.size() == 1) {
        return make(title, rest(HttpMethod::Put, "/instances/" + ids(list[0]) + "/", json{{"state", state}}));
    }
    return make(title, rest(HttpMethod::Put, "/instances/", json{{"state", state}, {"ids", list}}));
}
}  // namespace

ApiCall startInstances(const std::vector<Id>& list) { return setState("start", "running", list); }

ApiCall stopInstances(const std::vector<Id>& list) { return setState("stop", "stopped", list); }

ApiCall destroyInstances(const std::vector<Id>& list) {
    if (list.size() == 1) {
        return make("Destroy instance", rest(HttpMethod::Delete, "/instances/" + ids(list[0]) + "/", json::object()));
    }
    return make("Destroy instances", rest(HttpMethod::Delete, "/instances/", json{{"instance_ids", list}}));
}

ApiCall rebootInstance(Id id) {
    return make("Reboot instance", rest(HttpMethod::Put, "/instances/reboot/" + ids(id) + "/", json::object()));
}

ApiCall recycleInstance(Id id) {
    return make("Recycle instance", rest(HttpMethod::Put, "/instances/recycle/" + ids(id) + "/", json::object()));
}

ApiCall labelInstance(Id id, const std::string& label) {
    return make("Label instance", rest(HttpMethod::Put, "/instances/" + ids(id) + "/", json{{"label", label}}));
}

ApiCall changeBid(Id id, double price) {
    return make("Change bid",
                rest(HttpMethod::Put, "/instances/bid_price/" + ids(id) + "/", json{{"client_id", "me"}, {"price", price}}));
}

ApiCall prepayInstance(Id id, double amount) {
    return make("Prepay instance", rest(HttpMethod::Put, "/instances/prepay/" + ids(id) + "/", json{{"amount", amount}}));
}

ApiCall showDeposit(Id id) {
    return make("Show deposit", rest(HttpMethod::Get, "/instances/balance/" + ids(id) + "/", std::nullopt, {{"owner", "me"}}));
}

ApiCall instanceLogs(Id id, int tail, const std::string& filter, bool daemonLogs) {
    json body = json::object();
    if (tail > 0) body["tail"] = std::to_string(tail);
    if (!filter.empty()) body["filter"] = filter;
    if (daemonLogs) body["daemon_logs"] = "true";
    RestSpec s = rest(HttpMethod::Put, "/instances/request_logs/" + ids(id) + "/", body);
    s.transform = RestTransform::PollResultUrl;
    return make("Instance logs", s, 60);
}

ApiCall executeCommand(Id id, const std::string& command) {
    RestSpec s = rest(HttpMethod::Put, "/instances/command/" + ids(id) + "/", json{{"command", command}});
    s.transform = RestTransform::PollResultUrl;
    return make("Execute command", s, 120);
}

// Port of `vastai copy` for remote locations (PUT /commands/copy_direct/). Copies to or from this PC are done by
// the CLI with a local rsync over SSH, which VastGui does not run.
ApiCall copyFiles(const std::string& src, const std::string& dst) {
    std::string error;
    const auto s = parseVastLocation(src, error);
    const auto d = s ? parseVastLocation(dst, error) : std::nullopt;
    if (!s || !d) return failing("Copy files", error);
    auto isLocal = [](const VastLocation& l) { return l.id.is_null() || l.id == "local"; };
    if (isLocal(*s) || isLocal(*d)) {
        return failing("Copy files",
                       "copies to or from this PC need rsync over SSH, which VastGui does not run - use the instance's "
                       "SSH / SCP command instead. Remote locations look like C.<instance>:/path, V.<volume>:/path or "
                       "<connection>:/path.");
    }
    return make("Copy files",
                rest(HttpMethod::Put, "/commands/copy_direct/",
                     json{{"client_id", "me"},
                          {"src_id", s->id},
                          {"dst_id", d->id},
                          {"src_path", s->path},
                          {"dst_path", d->path}}),
                120);
}

ApiCall cancelCopy(const std::string& dst) {
    return make("Cancel copy", rest(HttpMethod::Delete, "/commands/copy_direct/", json{{"client_id", "me"}, {"dst_id", dst}}));
}

ApiCall attachSsh(Id instanceId, const std::string& publicKey) {
    return make("Attach SSH key",
                rest(HttpMethod::Post, "/instances/" + ids(instanceId) + "/ssh/", json{{"ssh_key", publicKey}}));
}

ApiCall detachSsh(Id instanceId, Id sshKeyId) {
    return make("Detach SSH key", rest(HttpMethod::Delete, "/instances/" + ids(instanceId) + "/ssh/" + ids(sshKeyId) + "/"));
}

// Port of `vastai cloud copy` (POST /commands/rclone/).
ApiCall cloudCopy(const CloudCopyParams& p) {
    json flags = json::array();
    if (p.dryRun) flags.push_back("--dry-run");
    if (p.ignoreExisting) flags.push_back("--ignore-existing");
    return make("Cloud copy",
                rest(HttpMethod::Post, "/commands/rclone/",
                     json{{"src", p.src},
                          {"dst", p.dst},
                          {"instance_id", ids(p.instanceId)},
                          {"selected", p.connectionId},
                          {"transfer", p.transfer},
                          {"flags", flags}}),
                120);
}

ApiCall updateInstance(Id id, const UpdateInstanceParams& p) {
    json body{{"id", id}};
    if (p.templateId) body["template_id"] = *p.templateId;
    if (!p.templateHash.empty()) body["template_hash_id"] = p.templateHash;
    if (!p.image.empty()) body["image"] = p.image;
    if (!p.env.empty()) body["env"] = envToJson(p.env);
    if (!p.onstart.empty()) body["onstart"] = p.onstart;
    if (!p.args.empty()) body["args"] = p.args;
    return make("Update instance", rest(HttpMethod::Put, "/instances/update_template/" + ids(id) + "/", body));
}

ApiCall takeSnapshot(Id id, const SnapshotParams& p) {
    json body{{"id", id},
              {"container_registry", p.registry},
              {"personal_repo", p.repo},
              {"docker_login_user", nullable(p.user)},
              {"docker_login_pass", nullable(p.password)},
              {"pause", p.pause ? "true" : "false"}};
    return make("Take snapshot", rest(HttpMethod::Post, "/instances/take_snapshot/" + ids(id) + "/", body), 300);
}

ApiCall createInstance(const CreateInstanceParams& p) {
    const bool fromTemplate = !p.templateHash.empty();
    json body{{"client_id", "me"},
              {"image", nullable(p.image)},
              {"env", envToJson(p.env)},
              {"price", nullable(p.bidPrice)},
              {"disk", p.diskGb},
              {"label", nullable(p.label)},
              {"extra", nullptr},
              {"onstart", nullable(p.onstartCmd)},
              {"image_login", nullable(p.login)},
              {"python_utf8", false},
              {"lang_utf8", false},
              {"use_jupyter_lab", false},
              {"jupyter_dir", nullptr},
              {"force", false},
              {"cancel_unavail", p.cancelUnavail},
              {"template_hash_id", nullable(p.templateHash)},
              {"user", nullptr}};
    if (!fromTemplate) body["runtype"] = runtypeFor(p.ssh, p.jupyter, p.direct, p.argsMode);
    if (p.argsMode && !p.args.empty()) body["args"] = splitShellArgs(p.args);
    if (p.createVolume || p.linkVolume) {
        json vi{{"mount_path", p.mountPath.empty() ? "/data" : p.mountPath},
                {"create_new", p.createVolume.has_value()},
                {"volume_id", p.createVolume ? *p.createVolume : *p.linkVolume}};
        if (!p.volumeLabel.empty()) vi["name"] = p.volumeLabel;
        if (p.createVolume) vi["size"] = p.volumeSizeGb.value_or(15.0);
        body["volume_info"] = vi;
    }
    return make("Create instance", rest(HttpMethod::Put, "/asks/" + ids(p.offerId) + "/", body), 120);
}

// ---- Templates -------------------------------------------------------------

ApiCall searchTemplates(const std::string& query) {
    json filters = json::object();
    if (!fmt::trim(query).empty()) {
        // Template search accepts the same query syntax; unknown fields only warn.
        auto parsed = QueryBuilder::parseQuery(query);
        if (parsed.ok()) filters = parsed.query;
    }
    RestSpec s = rest(HttpMethod::Get, "/template/", std::nullopt,
                      {{"select_cols", json::array({"*"})}, {"select_filters", filters}});
    return make("Search templates", extract(s, "templates"), 60);
}

namespace {

json templateBody(const TemplateParams& p) {
    json extraFilters = json::object();
    if (!fmt::trim(p.searchParams).empty()) {
        json base = {{"verified", {{"eq", true}}}, {"external", {{"eq", false}}}, {"rentable", {{"eq", true}}}};
        auto parsed = QueryBuilder::parseQuery(p.searchParams, base);
        if (parsed.ok()) extraFilters = parsed.query;
    }
    return json{{"name", nullable(p.name)},
                {"image", nullable(p.image)},
                {"tag", nullable(p.tag)},
                {"href", nullable(p.href)},
                {"repo", nullable(p.repo)},
                {"env", nullable(p.env)},
                {"onstart", nullable(p.onstart)},
                {"jup_direct", p.jupyter && p.direct},
                {"ssh_direct", p.ssh && p.direct},
                {"use_jupyter_lab", p.jupyterLab},
                {"runtype", p.jupyter ? "jupyter" : (p.ssh ? "ssh" : "args")},
                {"use_ssh", p.ssh || p.jupyter},
                {"jupyter_dir", nullable(p.jupyterDir)},
                {"docker_login_repo", p.login.empty() ? json(nullptr) : json(fmt::split(p.login, ' ').front())},
                {"extra_filters", extraFilters},
                {"recommended_disk_space", nullable(p.diskSpaceGb)},
                {"readme", nullable(p.readme)},
                {"readme_visible", !p.hideReadme},
                {"desc", nullable(p.desc)},
                {"private", !p.isPublic}};
}

}  // namespace

ApiCall createTemplate(const TemplateParams& p) {
    return make("Create template", rest(HttpMethod::Post, "/template/", templateBody(p)));
}

ApiCall updateTemplate(const std::string& hashId, const TemplateParams& p) {
    if (!p.id) return failing("Update template", "the template has no numeric id - reload the template list and try again");
    json body = templateBody(p);
    body["id"] = *p.id;
    body["hash_id"] = hashId;
    return make("Update template", rest(HttpMethod::Put, "/api/v1/template/", body));
}

ApiCall deleteTemplate(std::optional<Id> id, const std::string& hashId) {
    if (!hashId.empty()) return make("Delete template", rest(HttpMethod::Delete, "/template/", json{{"hash_id", hashId}}));
    return make("Delete template", rest(HttpMethod::Delete, "/template/", json{{"template_id", id.value_or(0)}}));
}

// ---- Storage ---------------------------------------------------------------

ApiCall searchVolumes(const std::string& query, double storageGb) {
    json q = {{"verified", {{"eq", true}}}, {"external", {{"eq", false}}}, {"disk_space", {{"gte", 1}}}};
    if (!fmt::trim(query).empty()) {
        auto parsed = QueryBuilder::parseQuery(query, q);
        if (parsed.ok()) q = parsed.query;
    }
    q["order"] = json::array({json::array({"score", "desc"})});
    q["allocated_storage"] = storageGb;
    return make("Search volumes", extract(rest(HttpMethod::Post, "/volumes/search/", q), "offers"), 60);
}

ApiCall showVolumes() {
    return make("Show volumes",
                extract(rest(HttpMethod::Get, "/volumes", std::nullopt, {{"owner", "me"}, {"type", "all"}}), "volumes"));
}

ApiCall createVolume(Id offerId, double sizeGb, const std::string& name) {
    json body{{"size", static_cast<long long>(sizeGb)}, {"id", offerId}};
    if (!name.empty()) body["name"] = name;
    return make("Create volume", rest(HttpMethod::Put, "/volumes/", body));
}

ApiCall deleteVolume(Id id) {
    return make("Delete volume", rest(HttpMethod::Delete, "/volumes/", std::nullopt, {{"id", id}}));
}

ApiCall cloneVolume(Id source, Id dest, std::optional<double> sizeGb) {
    json body{{"src_id", source}, {"dst_id", dest}};
    if (sizeGb) body["size"] = *sizeGb;
    return make("Clone volume", rest(HttpMethod::Post, "/volumes/copy/", body), 300);
}

ApiCall showConnections() {
    return make("Show cloud connections", rest(HttpMethod::Get, "/users/cloud_integrations/"));
}

// ---- Serverless ------------------------------------------------------------

ApiCall showEndpoints() {
    RestSpec s = extract(rest(HttpMethod::Get, "/endptjobs/", json{{"client_id", "me"}}), "results");
    s.injectApiKey = true;
    return make("Show endpoints", s);
}

namespace {
json endpointBody(const EndpointParams& p) {
    return json{{"client_id", "me"},
                {"min_load", p.minLoad},
                {"min_cold_load", p.minColdLoad},
                {"target_util", p.targetUtil},
                {"cold_mult", p.coldMult},
                {"cold_workers", p.coldWorkers},
                {"max_workers", p.maxWorkers},
                {"endpoint_name", nullable(p.name)},
                {"max_queue_time", nullable(p.maxQueueTime)},
                {"target_queue_time", nullable(p.targetQueueTime)},
                {"inactivity_timeout", nullable(p.inactivityTimeout)},
                {"autoscaler_instance", "prod"}};
}
}  // namespace

ApiCall createEndpoint(const EndpointParams& p) {
    return make("Create endpoint", rest(HttpMethod::Post, "/endptjobs/", endpointBody(p)));
}

ApiCall updateEndpoint(Id id, const EndpointParams& p) {
    json body = endpointBody(p);
    body["endptjob_id"] = id;
    if (!p.state.empty()) body["endpoint_state"] = p.state;
    return make("Update endpoint", rest(HttpMethod::Put, "/endptjobs/" + ids(id) + "/", body));
}

ApiCall deleteEndpoint(Id id) {
    return make("Delete endpoint",
                rest(HttpMethod::Delete, "/endptjobs/" + ids(id) + "/", json{{"client_id", "me"}, {"endptjob_id", id}}));
}

ApiCall endpointLogs(Id id, int tail) {
    json body{{"id", id}};
    if (tail > 0) body["tail"] = tail;
    RestSpec s = rest(HttpMethod::Post, "https://run.vast.ai/get_endpoint_logs/", body);
    s.injectApiKey = true;
    return make("Endpoint logs", s);
}

ApiCall showWorkergroups() {
    RestSpec s = extract(rest(HttpMethod::Get, "/autojobs/", json{{"client_id", "me"}}), "results");
    s.injectApiKey = true;
    return make("Show worker groups", s);
}

namespace {
json workergroupBody(const WorkergroupParams& p) {
    std::string search = p.searchParams + (p.noDefault ? "" : " verified=True rentable=True rented=False");
    return json{{"client_id", "me"},
                {"template_hash", nullable(p.templateHash)},
                {"template_id", nullable(p.templateId)},
                {"search_params", fmt::trim(search)},
                {"launch_args", nullable(p.launchArgs)},
                {"gpu_ram", nullable(p.gpuRam)},
                {"endpoint_name", nullable(p.endpointName)},
                {"endpoint_id", nullable(p.endpointId)}};
}
}  // namespace

ApiCall createWorkergroup(const WorkergroupParams& p) {
    json body = workergroupBody(p);
    body["autoscaler_instance"] = "prod";
    return make("Create worker group", rest(HttpMethod::Post, "/autojobs/", body));
}

ApiCall updateWorkergroup(Id id, const WorkergroupParams& p) {
    json body = workergroupBody(p);
    body["autojob_id"] = id;
    return make("Update worker group", rest(HttpMethod::Put, "/autojobs/" + ids(id) + "/", body));
}

ApiCall deleteWorkergroup(Id id) {
    return make("Delete worker group",
                rest(HttpMethod::Delete, "/autojobs/" + ids(id) + "/", json{{"client_id", "me"}, {"autojob_id", id}}));
}

ApiCall workergroupLogs(Id id, int tail) {
    json body{{"id", id}};
    if (tail > 0) body["tail"] = tail;
    RestSpec s = rest(HttpMethod::Post, "https://run.vast.ai/get_autogroup_logs/", body);
    s.injectApiKey = true;
    return make("Worker group logs", s);
}

// Port of `vastai update workers` (rolling update of a worker group).
ApiCall updateWorkers(Id id) {
    RestSpec s = rest(HttpMethod::Post, "https://run.vast.ai/update_workers/", json{{"workergroup_id", id}});
    s.injectApiKey = true;
    return make("Update workers", s);
}

ApiCall showDeployments() { return make("Show deployments", rest(HttpMethod::Get, "/deployments")); }

ApiCall deleteDeployment(Id id) {
    return make("Delete deployment", rest(HttpMethod::Delete, "/deployment/" + ids(id) + "/"));
}

// ---- Billing ---------------------------------------------------------------

ApiCall showInvoices(const std::string& startDate, const std::string& endDate) {
    json query{{"owner", "me"}, {"inc_charges", true}};
    if (!startDate.empty()) {
        if (auto e = fmt::epochFromDate(startDate)) query["sdate"] = *e;
    }
    if (!endDate.empty()) {
        if (auto e = fmt::epochFromDate(endDate)) query["edate"] = *e + 86399.0;
    }
    if (!query.contains("sdate")) query["sdate"] = fmt::nowEpoch() - 86400.0;
    if (!query.contains("edate")) query["edate"] = fmt::nowEpoch();
    return make("Show invoices", extract(rest(HttpMethod::Get, "/users/me/invoices", std::nullopt, query), "invoices"), 120);
}

ApiCall showEarnings(const std::string& startDate, const std::string& endDate, std::optional<Id> machineId) {
    const double day = 86400.0;
    json query{{"owner", "me"}};
    double sday = fmt::nowEpoch() / day - 1.0;
    double eday = fmt::nowEpoch() / day - 1.0;
    if (!startDate.empty()) {
        if (auto e = fmt::epochFromDate(startDate)) sday = *e / day;
    }
    if (!endDate.empty()) {
        if (auto e = fmt::epochFromDate(endDate)) eday = *e / day;
    }
    query["sday"] = sday;
    query["eday"] = eday;
    query["machid"] = nullable(machineId);
    return make("Show earnings", rest(HttpMethod::Get, "/users/me/machine-earnings", std::nullopt, query), 120);
}

// ---- Team ------------------------------------------------------------------

ApiCall showMembers() { return make("Show members", rest(HttpMethod::Get, "/team/members/")); }

ApiCall inviteMember(const std::string& email, const std::string& role) {
    return make("Invite member", rest(HttpMethod::Post, "/team/invite/", json::object(), {{"email", email}, {"role", role}}));
}

ApiCall removeMember(Id id) { return make("Remove member", rest(HttpMethod::Delete, "/team/members/" + ids(id) + "/")); }

ApiCall showTeamRoles() { return make("Show team roles", rest(HttpMethod::Get, "/team/roles-full/")); }

ApiCall createTeamRole(const std::string& name, const std::string& permissionsJson) {
    json perms = json::parse(permissionsJson, nullptr, false);
    if (perms.is_discarded()) perms = json::object();
    return make("Create team role", rest(HttpMethod::Post, "/team/roles/", json{{"name", name}, {"permissions", perms}}));
}

ApiCall removeTeamRole(const std::string& name) {
    return make("Remove team role", rest(HttpMethod::Delete, "/team/roles/" + name + "/"));
}

ApiCall createTeam(const std::string& name) {
    return make("Create team", rest(HttpMethod::Post, "/team/", json{{"team_name", name}}));
}

ApiCall destroyTeam() { return make("Destroy team", rest(HttpMethod::Delete, "/team/")); }

// ---- Keys ------------------------------------------------------------------

ApiCall showSshKeys() { return make("Show SSH keys", rest(HttpMethod::Get, "/ssh/")); }

ApiCall createSshKey(const std::string& publicKey) {
    return make("Create SSH key", rest(HttpMethod::Post, "/ssh/", json{{"ssh_key", publicKey}}));
}

ApiCall updateSshKey(Id id, const std::string& publicKey) {
    return make("Update SSH key", rest(HttpMethod::Put, "/ssh/" + ids(id) + "/", json{{"id", id}, {"ssh_key", publicKey}}));
}

ApiCall deleteSshKey(Id id) { return make("Delete SSH key", rest(HttpMethod::Delete, "/ssh/" + ids(id) + "/")); }

ApiCall showApiKeys() {
    ApiCall c = make("Show API keys", rest(HttpMethod::Get, "/auth/apikeys/"));
    c.sensitiveOutput = true;
    return c;
}

ApiCall createApiKey(const std::string& name, const json& permissions) {
    ApiCall c = make("Create API key", rest(HttpMethod::Post, "/auth/apikeys/",
                                            json{{"name", name},
                                                 {"permissions", permissions.is_object() ? permissions : json::object()},
                                                 {"key_params", nullptr}}));
    c.sensitiveOutput = true;
    return c;
}

ApiCall deleteApiKey(Id id) { return make("Delete API key", rest(HttpMethod::Delete, "/auth/apikeys/" + ids(id) + "/")); }

ApiCall showEnvVars() {
    ApiCall c = make("Show env vars", extract(rest(HttpMethod::Get, "/secrets/"), "secrets"));
    c.sensitiveOutput = true;
    return c;
}

ApiCall createEnvVar(const std::string& name, const std::string& value) {
    ApiCall c = make("Create env var", rest(HttpMethod::Post, "/secrets/", json{{"key", name}, {"value", value}}));
    c.sensitiveOutput = true;
    return c;
}

ApiCall updateEnvVar(const std::string& name, const std::string& value) {
    ApiCall c = make("Update env var", rest(HttpMethod::Put, "/secrets/", json{{"key", name}, {"value", value}}));
    c.sensitiveOutput = true;
    return c;
}

ApiCall deleteEnvVar(const std::string& name) {
    return make("Delete env var", rest(HttpMethod::Delete, "/secrets/", json{{"key", name}}));
}

// ---- Hosting ---------------------------------------------------------------

ApiCall showMachines() {
    return make("Show machines", extract(rest(HttpMethod::Get, "/machines", std::nullopt, {{"owner", "me"}}), "machines"));
}

ApiCall listMachine(const ListMachineParams& p) {
    std::optional<double> end;
    if (!p.endDate.empty()) {
        end = fmt::epochFromDate(p.endDate);
        if (!end) end = jsonutil::parseDouble(p.endDate);
    }
    json body{{"machine", p.id},
              {"price_gpu", nullable(p.priceGpu)},
              {"price_disk", nullable(p.priceDisk)},
              {"price_inetu", nullable(p.priceInetUp)},
              {"price_inetd", nullable(p.priceInetDown)},
              {"price_min_bid", nullable(p.priceMinBid)},
              {"min_chunk", nullable(p.minChunk)},
              {"end_date", nullable(end)},
              {"credit_discount_max", nullable(p.discountRate)},
              {"duration", nullptr},
              {"vol_size", nullptr},
              {"vol_price", nullptr}};
    return make("List machine", rest(HttpMethod::Put, "/machines/create_asks/", body));
}

ApiCall unlistMachine(Id id) { return make("Unlist machine", rest(HttpMethod::Delete, "/machines/" + ids(id) + "/asks/")); }

ApiCall setMinBid(Id id, double price) {
    return make("Set min bid",
                rest(HttpMethod::Put, "/machines/" + ids(id) + "/minbid/", json{{"client_id", "me"}, {"price", price}}));
}

ApiCall setDefjob(Id id, double priceGpu, double priceInetUp, double priceInetDown, const std::string& image,
                  const std::string& args) {
    json body{{"machine", id},
              {"price_gpu", priceGpu},
              {"price_inetu", priceInetUp},
              {"price_inetd", priceInetDown},
              {"image", image},
              {"args", splitShellArgs(args)}};
    return make("Set default job", rest(HttpMethod::Put, "/machines/create_bids/", body));
}

ApiCall removeDefjob(Id id) {
    return make("Remove default job", rest(HttpMethod::Delete, "/machines/" + ids(id) + "/defjob/"));
}

ApiCall scheduleMaint(Id id, double startEpoch, double durationHours, const std::string& category) {
    return make("Schedule maintenance",
                rest(HttpMethod::Put, "/machines/" + ids(id) + "/dnotify/",
                     json{{"client_id", "me"},
                          {"sdate", startEpoch},
                          {"duration", durationHours},
                          {"maintenance_category", category}}));
}

ApiCall cancelMaint(Id id) {
    return make("Cancel maintenance", rest(HttpMethod::Put, "/machines/" + ids(id) + "/cancel_maint/",
                                           json{{"client_id", "me"}, {"machine_id", id}}));
}

ApiCall cleanupMachine(Id id) {
    return make("Cleanup machine", rest(HttpMethod::Put, "/machines/" + ids(id) + "/cleanup/", json::object()));
}

ApiCall machineReports(Id id) {
    return make("Machine reports", rest(HttpMethod::Get, "/machines/" + ids(id) + "/reports/", json{{"machine_id", id}}));
}

ApiCall showMaints(const std::vector<Id>& list) {
    return make("Show maintenances",
                rest(HttpMethod::Get, "/machines/maintenances", std::nullopt, {{"owner", "me"}, {"machine_ids", list}}));
}

}  // namespace vast::cmd
