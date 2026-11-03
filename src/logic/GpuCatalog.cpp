#include "logic/GpuCatalog.hpp"

#include "logic/Format.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <map>
#include <regex>
#include <set>
#include <utility>

namespace vast {

namespace {

constexpr int kOtherOrder = 90;
constexpr const char* kOtherName = "Other";

struct Placement {
    int order = kOtherOrder;
    std::string group = kOtherName;
    std::vector<int> key;  // ascending; ties are broken by the name
};

int toInt(const std::string& s) {
    int v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// Form factor / variant of a data center card: NVL, SXM, (none), PCIe, X.
int formRank(std::string suffix) {
    suffix = fmt::toLower(fmt::trim(suffix));
    if (suffix == "nvl") return 0;
    if (suffix == "sxm4" || suffix == "sxm" || suffix == "g") return 1;
    if (suffix.empty()) return 2;
    if (suffix == "pcie") return 3;
    if (suffix == "x") return 4;
    return 5;
}

Placement classify(const std::string& n) {
    using std::regex;
    constexpr auto icase = std::regex::icase;
    std::smatch m;
    Placement p;
    auto put = [&](int order, std::string group, std::vector<int> key) {
        p.order = order;
        p.group = std::move(group);
        p.key = std::move(key);
        return p;
    };

    // GeForce RTX 20..50: model number desc, then Ti SUPER, Ti, SUPER, base, D, Laptop.
    static const regex rtx(R"(^RTX ([2-5])0([5-9])0( ?S)?( Ti)?( ?D)?( Laptop)?$)", icase);
    if (std::regex_match(n, m, rtx)) {
        const int series = toInt(m[1].str());
        const int model = series * 1000 + toInt(m[2].str()) * 10;
        int variant = 3;
        if (m[3].matched && m[4].matched) variant = 0;
        else if (m[4].matched) variant = 1;
        else if (m[3].matched) variant = 2;
        else if (m[5].matched) variant = 4;
        if (m[6].matched) variant = 5;
        return put(5 - series, std::format("RTX {}000 Series", series), {-model, variant});
    }
    static const regex gtx(R"(^GTX (\d{3,4})( ?S)?( Ti)?$)", icase);
    if (std::regex_match(n, m, gtx)) {
        const int variant = m[3].matched ? 0 : m[2].matched ? 1 : 2;
        return put(10, "GTX", {-toInt(m[1].str()), variant});
    }
    static const regex titan(R"(^Titan\b)", icase);
    if (std::regex_search(n, titan)) {
        static const std::map<std::string, int> rank = {{"titan rtx", 0}, {"titan v", 1},          {"titan v ceo", 2},
                                                        {"titan xp", 3},  {"titan x", 4},          {"titan x (maxwell)", 5}};
        const auto it = rank.find(fmt::toLower(n));
        return put(11, "Titan", {it == rank.end() ? 9 : it->second});
    }
    static const regex pro(R"(^RTX PRO (\d{4})(?: (.*))?$)", icase);
    static const regex proD(R"(^RTX (\d{4})D$)", icase);
    if (std::regex_match(n, m, pro)) {
        return put(20, "RTX PRO (Blackwell)", {-toInt(m[1].str()), m[2].matched ? 2 : 0});
    }
    if (std::regex_match(n, m, proD) && toInt(m[1].str()) >= 6000) {
        return put(20, "RTX PRO (Blackwell)", {-toInt(m[1].str()), 1});
    }
    static const regex ada(R"(^RTX (\d{4}) Ada$)", icase);
    if (std::regex_match(n, m, ada)) return put(21, "RTX Ada", {-toInt(m[1].str())});
    static const regex ampere(R"(^RTX A(\d{4})$)", icase);
    if (std::regex_match(n, m, ampere)) return put(22, "RTX A (Ampere)", {-toInt(m[1].str())});
    static const regex quadro(R"(^Quadro (RTX )?([A-Z]*)(\d+)$)", icase);
    if (std::regex_match(n, m, quadro)) {
        const std::string gen = fmt::toLower(m[2].str());
        const int sub = m[1].matched ? 0 : gen == "gv" ? 1 : gen == "gp" ? 2 : gen == "p" ? 3 : gen == "k" ? 4 : 5;
        return put(23, "Quadro", {sub, -toInt(m[3].str())});
    }

    // Data center, by generation: Blackwell, Hopper, Ampere, Ada, Tesla.
    static const regex blackwell(R"(^(G?)B(\d+)$)", icase);
    static const regex hopper(R"(^(G?)H(\d+)(?: (.*))?$)", icase);
    static const regex amp(R"(^A(\d+)(.*)$)", icase);
    static const regex lada(R"(^L(\d+)(S?)$)", icase);
    static const regex tesla(R"(^Tesla ([VTPMK])(\d+)(.*)$)", icase);
    static const regex t4g(R"(^T4G$)", icase);
    if (std::regex_match(n, m, blackwell)) return put(30, "Data Center", {0, -toInt(m[2].str()), m[1].length() ? 0 : 1});
    if (std::regex_match(n, m, hopper)) {
        return put(30, "Data Center", {1, -toInt(m[2].str()), m[1].length() ? 0 : 1, formRank(m[3].str())});
    }
    if (std::regex_match(n, m, amp)) {
        const int num = toInt(m[1].str());
        return put(30, "Data Center", {2, num == 100 ? 0 : num == 800 ? 1 : 2, -num, formRank(m[2].str())});
    }
    if (std::regex_match(n, m, lada)) return put(30, "Data Center", {3, -toInt(m[1].str()), m[2].length() ? 0 : 1});
    if (std::regex_match(n, m, tesla) || std::regex_match(n, t4g)) {
        const bool isT4g = !m[1].matched;
        const std::string arch = isT4g ? "T" : m[1].str();
        static const std::string kArch = "VTPMK";
        const int num = isT4g ? 4 : toInt(m[2].str());
        const std::string suffix = isT4g ? "G" : fmt::trim(m[3].str());
        const int variant = suffix == "S" ? 0 : suffix.empty() ? 1 : 2;
        return put(30, "Data Center",
                   {4, static_cast<int>(kArch.find(static_cast<char>(std::toupper(static_cast<unsigned char>(arch[0]))))),
                    -num, variant});
    }

    static const regex instinct(R"(^Instinct MI(\d+)(X?)$)", icase);
    if (std::regex_match(n, m, instinct)) return put(40, "AMD Instinct", {-toInt(m[1].str()), m[2].length() ? 0 : 1});
    static const regex rx(R"(^RX (\d{4})(?: (XTX|XT|GRE))?$)", icase);
    static const regex radeonPro(R"(^Pro [WV](\d+)$)", icase);
    static const regex radeon(R"(^Radeon\b)", icase);
    if (std::regex_match(n, m, rx)) {
        const std::string v = fmt::toLower(m[2].str());
        return put(41, "AMD Radeon", {0, -toInt(m[1].str()), v == "xtx" ? 0 : v == "xt" ? 1 : v == "gre" ? 2 : 3});
    }
    if (std::regex_match(n, m, radeonPro)) return put(41, "AMD Radeon", {1, -toInt(m[1].str())});
    if (std::regex_search(n, radeon)) return put(41, "AMD Radeon", {2});
    return p;
}

}  // namespace

std::string normaliseGpuName(std::string_view name) {
    std::string n;
    for (const char c : name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!n.empty() && n.back() != ' ') n += ' ';
        } else {
            n += c;
        }
    }
    while (!n.empty() && n.back() == ' ') n.pop_back();

    using std::regex;
    constexpr auto icase = std::regex::icase;
    static const std::vector<std::pair<regex, std::string>> rules = {
        {regex(R"(^InstinctMI)", icase), "Instinct MI"},
        {regex(R"(^Q RTX )", icase), "Quadro RTX "},
        {regex(R"(^GeForce (\d))", icase), "GTX $1"},
        {regex(R"((\d)Ada\b)", icase), "$1 Ada"},
        {regex(R"(^GTX TITAN X$)", icase), "Titan X (Maxwell)"},
        {regex(R"(^TITAN\b)", icase), "Titan"},
        {regex(R"(\blaptop\b)", icase), "Laptop"},
        {regex(R"(^A10g$)", icase), "A10G"},
    };
    for (const auto& [re, replacement] : rules) n = std::regex_replace(n, re, replacement);
    return n;
}

std::vector<std::string> gpuNamesFromTypes(const json& gpuTypes) {
    std::vector<std::string> out;
    for (const auto& t : jsonutil::extractArray(gpuTypes, {"gpu_types"})) {
        if (!t.is_object()) continue;
        if (fmt::toLower(jsonutil::str(t, "status", "active")) != "active") continue;
        std::string name = fmt::trim(jsonutil::str(t, "canonical_name"));
        if (!name.empty()) out.push_back(std::move(name));
    }
    return out;
}

std::vector<GpuGroup> buildGpuCatalog(std::span<const std::string> names) {
    struct Entry {
        std::string name;
        std::string sortName;
        Placement place;
    };
    std::map<std::pair<int, std::string>, std::vector<Entry>> groups;
    std::set<std::string> seen;
    for (const auto& raw : names) {
        std::string name = fmt::trim(raw);
        if (name.empty() || !seen.insert(name).second) continue;
        const std::string norm = normaliseGpuName(name);
        Placement place = classify(norm);
        groups[{place.order, place.group}].push_back({std::move(name), fmt::toLower(norm), std::move(place)});
    }

    // A family with a single model is "singled out": it goes to Other instead of getting its own header.
    std::vector<Entry> other;
    for (auto it = groups.begin(); it != groups.end();) {
        if (it->first.first != kOtherOrder && it->second.size() == 1) {
            other.push_back(std::move(it->second.front()));
            it = groups.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& e : other) {
        e.place = Placement{};
        groups[{kOtherOrder, kOtherName}].push_back(std::move(e));
    }

    std::vector<GpuGroup> out;
    for (auto& [id, entries] : groups) {
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.place.key != b.place.key) return a.place.key < b.place.key;
            return a.sortName < b.sortName;
        });
        GpuGroup g{id.second, {}};
        for (auto& e : entries) g.models.push_back(std::move(e.name));
        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace vast
