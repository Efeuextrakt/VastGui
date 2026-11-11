#include "logic/ApiKeyPermissions.hpp"

#include <array>
#include <set>

namespace vast::apiperm {

namespace {

constexpr std::array<Category, 7> kCategories{{
    {"user", "User", "Manage and/or view account identity and access, and user/account info."},
    {"admin", "Admin", "Admin permissions."},
    {"machine", "Machines",
     "For Hosting accounts ONLY. Manage and/or view machines (host-side resources), e.g. listing/inspecting machines."},
    {"instance", "Instances", "Manage and/or view running instances."},
    {"billing", "Billing/Earning", "Manage billing/financial actions and/or view financial activity."},
    {"team", "Team", "Manage and/or view team members and roles."},
    {"misc", "Miscellaneous",
     "Platform utilities and discovery endpoints. Includes helper actions like data copy/cancel-copy operations and GPU "
     "search endpoints."},
}};

bool isMeta(std::string_view key) { return key == "overrides" || key == "constraints"; }

// All keys a category can use, including the old/new admin variants.
std::set<std::string> categoryKeys(std::string_view cat) {
    if (cat == "misc") return {"misc"};
    const std::string c(cat);
    return {c + "_read", c + "_write", c + "_read_new", c + "_write_new"};
}

json keysFor(std::string_view cat, Level level) {
    json out = json::object();
    if (level == Level::NoAccess) return out;
    if (cat == "misc") {
        out["misc"] = json::object();
        return out;
    }
    out[readKey(cat)] = json::object();
    if (level == Level::ReadWrite) out[writeKey(cat)] = json::object();
    return out;
}

const json& tfaConstraint() {
    static const json c = {{"tfa", {{"eq", true}}}};
    return c;
}

bool hasTfa(const json& obj) {
    if (!obj.is_object()) return false;
    const auto c = obj.find("constraints");
    if (c == obj.end() || !c->is_object()) return false;
    const auto t = c->find("tfa");
    if (t == c->end() || !t->is_object()) return false;
    const auto eq = t->find("eq");
    return eq != t->end() && eq->is_boolean() && eq->get<bool>();
}

// Removes "tfa" from an object's constraints (and the constraints when they become empty).
void dropTfa(json& obj) {
    if (!obj.is_object()) return;
    const auto c = obj.find("constraints");
    if (c == obj.end() || !c->is_object()) return;
    c->erase("tfa");
    if (c->empty()) obj.erase("constraints");
}

}  // namespace

std::span<const Category> categories() { return kCategories; }

std::string readKey(std::string_view cat) {
    if (cat == "misc") return "misc";
    return std::string(cat) + (cat == "admin" ? "_read_new" : "_read");
}

std::string writeKey(std::string_view cat) {
    if (cat == "misc") return "misc";
    return std::string(cat) + (cat == "admin" ? "_write_new" : "_write");
}

Level levelOf(const json& api, std::string_view cat) {
    if (!api.is_object()) return Level::NoAccess;
    if (cat == "misc") return api.contains("misc") ? Level::ReadWrite : Level::NoAccess;
    if (api.contains(writeKey(cat))) return Level::ReadWrite;
    if (api.contains(readKey(cat))) return Level::Read;
    return Level::NoAccess;
}

json withLevel(const json& api, std::string_view cat, Level level) {
    const json wanted = keysFor(cat, level);
    const std::set<std::string> own = categoryKeys(cat);
    json out = json::object();
    if (api.is_object()) {
        for (const auto& [k, v] : api.items()) {
            // Keys of this category stay only when still wanted (with their constraints); others are untouched.
            if (wanted.contains(k) || !own.contains(k)) out[k] = v;
        }
    }
    for (const auto& [k, v] : wanted.items()) {
        if (!out.contains(k)) out[k] = v;
    }
    return out;
}

std::map<std::string, Level> grantable(const json& rights) {
    std::map<std::string, Level> out;
    const json* api = rights.is_object() ? jsonutil::find(rights, "api") : nullptr;
    if (!api || !api->is_object()) return out;
    for (const auto& [k, v] : api->items()) {
        (void)v;
        if (k == "misc") {
            out["misc"] = Level::ReadWrite;
            continue;
        }
        std::string base = k;
        bool write = false;
        for (const char* suffix : {"_read_new", "_write_new", "_read", "_write"}) {
            const std::string_view s(suffix);
            if (base.size() > s.size() && base.compare(base.size() - s.size(), s.size(), s) == 0) {
                write = s.find("write") != std::string_view::npos;
                base.resize(base.size() - s.size());
                break;
            }
        }
        if (base == k) continue;  // not a permission key (e.g. "overrides")
        Level& l = out[base];
        if (write || l == Level::ReadWrite) {
            l = Level::ReadWrite;
        } else {
            l = Level::Read;
        }
    }
    return out;
}

json defaultPermissions(const json& rights) {
    json api = json::object();
    if (const json* a = rights.is_object() ? jsonutil::find(rights, "api") : nullptr; a && a->is_object()) {
        for (const auto& [k, v] : a->items()) {
            if (k != "admin_read" && k != "admin_write") api[k] = v;
        }
    }
    return json{{"api", api}};
}

json onlyKnownKeys(const json& api) {
    std::set<std::string> known{"overrides", "constraints"};
    for (const Category& c : kCategories) {
        const auto keys = categoryKeys(c.key);
        known.insert(keys.begin(), keys.end());
    }
    json out = json::object();
    if (!api.is_object()) return out;
    for (const auto& [k, v] : api.items()) {
        if (known.contains(k)) out[k] = v;
    }
    return out;
}

bool hasAnyPermission(const json& api) {
    if (!api.is_object()) return false;
    for (const auto& [k, v] : api.items()) {
        (void)v;
        if (!isMeta(k)) return true;
    }
    return false;
}

bool tfa(const json& api, std::string_view key) {
    if (!api.is_object()) return false;
    const auto it = api.find(std::string(key));
    return it != api.end() && hasTfa(*it);
}

json withTfa(const json& api, std::string_view key, bool on) {
    json out = api.is_object() ? api : json::object();
    const auto it = out.find(std::string(key));
    if (it == out.end() || !it->is_object()) return out;  // only granted permissions can require 2FA
    if (on) {
        (*it)["constraints"]["tfa"] = tfaConstraint()["tfa"];
    } else {
        dropTfa(*it);
    }
    return out;
}

bool tfaAll(const json& api) { return hasTfa(api); }

json withTfaAll(const json& api, bool on) {
    json out = api.is_object() ? api : json::object();
    // The key-wide constraint replaces the per-permission ones.
    for (auto& [k, v] : out.items()) {
        if (!isMeta(k)) dropTfa(v);
    }
    if (on) {
        out["constraints"] = tfaConstraint();
    } else {
        out.erase("constraints");
    }
    return out;
}

}  // namespace vast::apiperm
