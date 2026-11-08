#include "logic/QueryBuilder.hpp"

#include "logic/Regions.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <regex>
#include <stdexcept>

namespace vast {

namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::string stripChars(std::string_view s, std::string_view chars) {
    const auto b = s.find_first_not_of(chars);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(chars);
    return std::string(s.substr(b, e - b + 1));
}

void replaceAll(std::string& s, char from, char to) { std::replace(s.begin(), s.end(), from, to); }

const std::map<std::string, std::string, std::less<>>& offerAliases() {
    static const std::map<std::string, std::string, std::less<>> m{
        {"cuda_vers", "cuda_max_good"},
        {"display_active", "gpu_display_active"},
        {"dlperf_usd", "dlperf_per_dphtotal"},
        {"dph", "dph_total"},
        {"flops_usd", "flops_per_dphtotal"},
    };
    return m;
}

const std::map<std::string, double, std::less<>>& offerMultipliers() {
    static const std::map<std::string, double, std::less<>> m{
        {"cpu_ram", 1000.0},
        {"gpu_ram", 1000.0},
        {"gpu_total_ram", 1000.0},
        {"duration", 24.0 * 60.0 * 60.0},
    };
    return m;
}

bool isKnownOfferField(std::string_view f) {
    static constexpr const char* kFields[] = {
        "bw_nvlink", "compute_cap", "cpu_arch", "cpu_cores", "cpu_cores_effective", "cpu_ghz", "cpu_ram",
        "cuda_max_good", "datacenter", "direct_port_count", "driver_version", "disk_bw", "disk_space", "dlperf",
        "dlperf_per_dphtotal", "dph_total", "duration", "external", "flops_per_dphtotal", "gpu_arch",
        "gpu_display_active", "gpu_frac", "gpu_mem_bw", "gpu_name", "gpu_ram", "gpu_total_ram", "gpu_max_power",
        "gpu_max_temp", "has_avx", "host_id", "id", "inet_down", "inet_down_cost", "inet_up", "inet_up_cost",
        "machine_id", "min_bid", "mobo_name", "num_gpus", "pci_gen", "pcie_bw", "reliability", "rentable",
        "rented", "storage_cost", "static_ip", "total_flops", "ubuntu_version", "verification", "verified",
        "vms_enabled", "geolocation", "cluster_id"};
    return std::any_of(std::begin(kFields), std::end(kFields), [&](const char* k) { return f == k; });
}

std::optional<std::string> opName(std::string_view op) {
    static const std::map<std::string, std::string, std::less<>> m{
        {">=", "gte"},  {">", "gt"},       {"gt", "gt"},       {"gte", "gte"},       {"<=", "lte"},
        {"<", "lt"},    {"lt", "lt"},      {"lte", "lte"},     {"!=", "neq"},        {"==", "eq"},
        {"=", "eq"},    {"eq", "eq"},      {"neq", "neq"},     {"noteq", "neq"},     {"not eq", "neq"},
        {"notin", "notin"}, {"not in", "notin"}, {"nin", "notin"}, {"in", "in"},
    };
    const auto it = m.find(op);
    if (it == m.end()) return std::nullopt;
    return it->second;
}

std::string serverOrderField(std::string_view field) {
    const auto& aliases = offerAliases();
    if (auto it = aliases.find(field); it != aliases.end()) return it->second;
    return std::string(field);
}

}  // namespace

std::string QueryBuilder::gpuNameToken(std::string_view name) {
    std::string s = trim(name);
    replaceAll(s, ' ', '_');
    return s;
}

std::string QueryBuilder::formatNumber(double v) {
    if (v == static_cast<double>(static_cast<long long>(v)) && std::abs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    return std::format("{}", v);
}

std::optional<double> QueryBuilder::parseVersionNumber(std::string_view v) {
    const std::string s = trim(v);
    if (s.empty()) return std::nullopt;
    return jsonutil::parseDouble(s);
}

std::string QueryBuilder::normalizeDriverVersion(std::string_view v) {
    std::string s = trim(v);
    if (s.empty()) return s;
    const auto dots = std::count(s.begin(), s.end(), '.');
    for (auto i = dots; i < 2; ++i) s += ".0";
    return s;
}

std::optional<long long> QueryBuilder::numericDriverVersion(std::string_view v) {
    const std::string s = trim(v);
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find('.', start);
        parts.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (parts.size() != 3) return std::nullopt;
    long long result = 0;
    for (const auto& p : parts) {
        if (p.empty() || p.size() > 3 || !std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        result = result * 1000 + std::stoll(p);
    }
    return result;
}

std::optional<double> QueryBuilder::cudaFromImageTag(std::string_view image) {
    static const std::regex re(R"(cuda[-_:]?v?(\d+)\.(\d+))", std::regex::icase);
    std::cmatch m;
    const std::string s(image);
    if (std::regex_search(s.c_str(), m, re)) {
        return std::stod(m[1].str() + "." + m[2].str());
    }
    return std::nullopt;
}

std::string QueryBuilder::buildQueryString(const SearchFilters& f) {
    std::vector<std::string> parts;
    auto add = [&](std::string s) { parts.push_back(std::move(s)); };

    if (f.gpuCount >= 9) {
        add("num_gpus>=9");
    } else if (f.gpuCount > 0) {
        add(std::format("num_gpus={}", f.gpuCount));
    }

    if (f.gpuModels.size() == 1) {
        add("gpu_name=" + gpuNameToken(f.gpuModels.front()));
    } else if (f.gpuModels.size() > 1) {
        std::string list;
        for (const auto& g : f.gpuModels) {
            if (!list.empty()) list += ',';
            list += gpuNameToken(g);
        }
        add("gpu_name in [" + list + "]");
    }

    if (!f.region.empty()) {
        if (isContinent(f.region)) {
            std::string list;
            for (const auto& c : countriesInContinent(f.region)) {
                if (!list.empty()) list += ',';
                list += c;
            }
            add("geolocation in [" + list + "]");
        } else {
            add("geolocation=" + f.region);
        }
    }

    if (f.secureCloudOnly) add("datacenter=true");

    // Specific machines / hosts (asked from vast.ai, so they are found even outside the random sample).
    auto idFilter = [&](const char* field, const std::string& text) {
        const IdList l = parseIdList(text);
        if (l.ids.size() == 1) {
            add(std::format("{}={}", field, l.ids.front()));
        } else if (l.ids.size() > 1) {
            std::string list;
            for (const long long id : l.ids) list += (list.empty() ? "" : ",") + std::to_string(id);
            add(std::format("{} in [{}]", field, list));
        }
    };
    idFilter("machine_id", f.machineIds);
    idFilter("host_id", f.hostIds);

    double diskMin = f.effectiveDiskGb();
    if (const auto& r = f.range(RangeField::DiskSpace); r.min) diskMin = std::max(diskMin, *r.min);
    if (diskMin > 0) add("disk_space>=" + formatNumber(diskMin));

    if (f.minReliability) add("reliability>=" + formatNumber(*f.minReliability));
    if (f.minDurationDays) add("duration>=" + formatNumber(*f.minDurationDays));
    if (f.showUnverified) add("verified=any");
    if (f.showUnavailable) add("rentable=any");
    if (f.staticIpOnly) add("static_ip=true");

    std::optional<double> cuda = parseVersionNumber(f.minCuda);
    if (!f.showIncompatible && f.templateMinCuda) cuda = std::max(cuda.value_or(0.0), *f.templateMinCuda);
    if (cuda && *cuda > 0) add("cuda_max_good>=" + formatNumber(*cuda));

    if (const auto drv = normalizeDriverVersion(f.minDriver); !drv.empty()) add("driver_version>=" + drv);
    if (const auto ub = trim(f.minUbuntu); !ub.empty()) add("ubuntu_version>=" + ub);

    for (const auto& m : rangeMetas()) {
        if (!m.queryField) continue;
        const Range& r = f.range(m.field);
        // The disk minimum is already merged into the container size constraint above.
        if (r.min && m.field != RangeField::DiskSpace) {
            add(std::format("{}>={}", m.queryField, formatNumber(*r.min * m.queryScale)));
        }
        if (r.max) add(std::format("{}<={}", m.queryField, formatNumber(*r.max * m.queryScale)));
    }

    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += ' ';
        out += p;
    }
    return out;
}

QueryBuilder::IdList QueryBuilder::parseIdList(std::string_view text) {
    IdList out;
    std::string token;
    auto flush = [&] {
        std::string t = token;
        token.clear();
        if (!t.empty() && t.front() == '#') t.erase(0, 1);
        if (t.empty()) return;
        const bool digits = t.size() <= 18 && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!digits) {
            out.invalid.push_back(std::move(t));
            return;
        }
        const long long id = std::stoll(t);
        if (std::find(out.ids.begin(), out.ids.end(), id) == out.ids.end()) out.ids.push_back(id);
    };
    for (const char c : text) {
        if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\n' || c == '\r') {
            flush();
        } else {
            token.push_back(c);
        }
    }
    flush();
    return out;
}

std::string QueryBuilder::orderString(const SearchFilters& f) {
    return std::string(sortOption(f.sortKey).serverField) + (f.sortDesc ? "-" : "");
}

json QueryBuilder::buildRestBody(const SearchFilters& f) {
    json base = {{"verified", {{"eq", true}}}, {"external", {{"eq", false}}}, {"rentable", {{"eq", true}}}};
    QueryParseResult parsed = parseQuery(buildQueryString(f), std::move(base));
    if (!parsed.ok()) throw std::invalid_argument(parsed.error);
    json q = std::move(parsed.query);
    if (!q.contains("cpu_arch")) q["cpu_arch"] = {{"in", json::array({"amd64", "arm64"})}};

    const std::string order = orderString(f);
    const bool desc = !order.empty() && order.back() == '-';
    const std::string field = serverOrderField(desc ? order.substr(0, order.size() - 1) : order);
    q["order"] = json::array({json::array({field, desc ? "desc" : "asc"})});
    q["type"] = rentalTypeCliName(f.rentalType);
    q["limit"] = f.limit;
    q["allocated_storage"] = f.effectiveDiskGb();
    return q;
}

QueryParseResult QueryBuilder::parseQuery(std::string_view queryIn, json base) {
    QueryParseResult out;
    out.query = base.is_object() ? std::move(base) : json::object();
    const std::string query = trim(queryIn);
    if (query.empty()) return out;

    static const std::regex re(
        R"re(([a-zA-Z0-9_]+)( *[=><!]+| +(?:[lg]te?|nin|neq|eq|not ?eq|not ?in|in) )?( *)(\[[^\]]+\]|"[^"]+"|[^ ]+)?( *))re");

    std::string joined;
    json& res = out.query;
    const auto& aliases = offerAliases();
    const auto& mults = offerMultipliers();

    for (auto it = std::sregex_iterator(query.begin(), query.end(), re); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        for (std::size_t g = 1; g <= 5; ++g) joined += m[g].str();

        std::string field = m[1].str();
        const std::string op = trim(m[2].str());
        std::string value = stripChars(m[4].str(), ",[]");

        if (auto a = aliases.find(field); a != aliases.end()) {
            const std::string old = field;
            field = a->second;
            if (res.contains(old)) {
                res[field] = res[old];
                res.erase(old);
            }
        }
        if (!res.contains(field) || !res[field].is_object()) res[field] = json::object();

        if (!isKnownOfferField(field)) out.warnings.push_back("Unrecognized field: " + field);
        const auto name = opName(op);
        if (!name) {
            out.error = "Unknown operator '" + op + "' for field '" + field + "'. Did you forget to quote your query?";
            return out;
        }
        if (value.empty()) {
            out.error = "Value cannot be blank for field '" + field + "'";
            return out;
        }

        if (*name == "in" || *name == "notin") {
            json list = json::array();
            std::size_t start = 0;
            while (start <= value.size()) {
                auto pos = value.find(',', start);
                std::string item = trim(value.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
                if (!item.empty()) {
                    replaceAll(item, '_', ' ');
                    list.push_back(stripChars(item, "\""));
                }
                if (pos == std::string::npos) break;
                start = pos + 1;
            }
            if (mults.contains(field)) {
                out.error = "List operators are not supported for field '" + field + "'";
                return out;
            }
            res[field][*name] = list;
            continue;
        }

        if (value == "?" || value == "*" || value == "any") {
            if (*name != "eq") {
                out.error = "Wildcard only makes sense with equals.";
                return out;
            }
            res.erase(field);
            continue;
        }

        if (field == "driver_version" && value.find('.') != std::string::npos) {
            if (auto n = numericDriverVersion(value)) {
                res[field][*name] = *n;
            } else {
                out.error = "Invalid driver version '" + value + "', expected X.X.X";
                return out;
            }
            continue;
        }

        replaceAll(value, '_', ' ');
        value = stripChars(value, "\"");

        if (auto mm = mults.find(field); mm != mults.end()) {
            auto d = jsonutil::parseDouble(value);
            if (!d) {
                out.error = "Numeric value expected for field '" + field + "'";
                return out;
            }
            res[field][*name] = *d * mm->second;
        } else if (value == "true" || value == "True") {
            res[field][*name] = true;
        } else if (value == "false" || value == "False") {
            res[field][*name] = false;
        } else if (value == "None" || value == "null") {
            res[field][*name] = nullptr;
        } else {
            res[field][*name] = value;
        }
    }

    if (joined != query) {
        out.error = "Unconsumed text in query. Did you forget to quote your query? Parsed: '" + joined + "'";
    }
    return out;
}

std::string QueryBuilder::quoteArgPosix(std::string_view arg) {
    if (arg.empty()) return "''";
    const bool safe = std::all_of(arg.begin(), arg.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || std::string_view("_-./:=@,+%").find(c) != std::string_view::npos;
    });
    if (safe) return std::string(arg);
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += '\'';
    return out;
}

}  // namespace vast
