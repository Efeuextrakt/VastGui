#include "Config.hpp"

#include "Platform.hpp"
#include "SecretBox.hpp"

#include <algorithm>
#include <format>
#include <system_error>

namespace vast {

ConfigPaths resolveConfigPaths() {
    ConfigPaths p;
    std::error_code ec;
    const auto exeDir = platform::executableDir();
    if (std::filesystem::exists(exeDir / "VastGui.json", ec)) {
        p.dir = exeDir;
        p.portable = true;
    } else {
        p.dir = platform::appDataDir();
    }
    std::filesystem::create_directories(p.dir, ec);
    p.settingsFile = p.dir / "VastGui.json";
    p.iniFile = p.dir / "imgui.ini";
    p.logFile = p.dir / "VastGui.log";
    p.keyFile = platform::appDataDir() / "secret.key";
    return p;
}

namespace {

json boolsToJson(const std::vector<bool>& v) {
    json a = json::array();
    for (bool b : v) a.push_back(b);
    return a;
}

std::vector<bool> boolsFromJson(const json& j, std::string_view key) {
    std::vector<bool> out;
    if (const json* a = jsonutil::find(j, key); a && a->is_array()) {
        for (const auto& v : *a) out.push_back(v.is_boolean() ? v.get<bool>() : true);
    }
    return out;
}

}  // namespace

void to_json(json& j, const Settings& s) {
    j = json::object();
    j["version"] = 1;
    // The API key is added (encrypted) by saveSettings, never serialized in plaintext.
    j["server_url"] = s.serverUrl;
    j["refresh_interval_sec"] = s.refreshIntervalSec;
    j["auto_refresh"] = s.autoRefresh;
    j["search_refresh_sec"] = s.searchRefreshSec;
    j["search_limit"] = s.searchLimit;
    j["search_requests"] = s.searchRequests;
    j["log_to_file"] = s.logToFile;
    j["theme"] = s.theme;
    j["accent"] = s.accent;
    j["font_size"] = s.fontSize;
    j["ui_scale"] = s.uiScale;
    j["defects"] = s.defects;
    j["currency"] = s.currency;
    j["default_sort"] = s.defaultSort;
    j["default_sort_desc"] = s.defaultSortDesc;
    j["filters"] = s.filters;
    json presets = json::object();
    for (const auto& [name, f] : s.presets) presets[name] = f;
    j["presets"] = presets;
    j["blacklist"] = s.blacklist;
    j["template"] = s.currentTemplate;
    j["card_view"] = s.cardView;
    j["template_card_view"] = s.templateCardView;
    j["template_sort"] = s.templateSort;
    j["template_sort_desc"] = s.templateSortDesc;
    j["instance_card_view"] = s.instanceCardView;
    j["instance_sort"] = s.instanceSort;
    j["instance_sort_desc"] = s.instanceSortDesc;
    j["offer_columns"] = boolsToJson(s.offerColumns);
    j["instance_columns"] = boolsToJson(s.instanceColumns);
    j["sidebar_collapsed"] = s.sidebarCollapsed;
    j["show_activity_log"] = s.showActivityLog;
    j["api_keys_split"] = s.apiKeysSplit;
    j["gpu_models"] = s.gpuModels;
}

void from_json(const json& j, Settings& s) { readSettings(j, s, nullptr); }

void readSettings(const json& j, Settings& s, std::vector<std::string>* problems) {
    using namespace jsonutil;
    s = Settings{};
    auto problem = [&](std::string what) {
        if (problems) problems->push_back(std::move(what));
    };
    if (!j.is_object()) {
        problem("the file does not contain a settings object");
        return;
    }
    // Every value falls back to its default on its own: a bad or outdated entry never resets the others.
    auto oneOf = [&](const char* key, std::initializer_list<std::string_view> allowed, std::string def) {
        const std::string v = str(j, key, def);
        if (std::find(allowed.begin(), allowed.end(), v) != allowed.end()) return v;
        problem(std::format("{} \"{}\" is unknown", key, v));
        return def;
    };
    auto section = [&](const char* key, auto&& read) {
        const json* v = find(j, key);
        if (!v) return;
        if (!v->is_object()) {
            problem(std::format("{} is not an object", key));
            return;
        }
        try {
            read(*v);
        } catch (const std::exception& e) {
            problem(std::format("{} could not be read ({})", key, e.what()));
        }
    };

    s.apiKey = str(j, "api_key");  // legacy plaintext, migrated by loadSettings
    s.apiKeySealed = str(j, "api_key_enc");
    s.serverUrl = str(j, "server_url", Settings{}.serverUrl);
    if (!s.serverUrl.starts_with("https://") && !s.serverUrl.starts_with("http://")) {
        if (!s.serverUrl.empty()) problem(std::format("server_url \"{}\" is not an http(s) URL", s.serverUrl));
        s.serverUrl = Settings{}.serverUrl;
    }
    s.refreshIntervalSec = std::clamp(static_cast<int>(optInt(j, "refresh_interval_sec").value_or(30)), 5, 3600);
    s.autoRefresh = optBool(j, "auto_refresh").value_or(true);
    s.searchRefreshSec = std::clamp(static_cast<int>(optInt(j, "search_refresh_sec").value_or(0)), 0, 3600);
    s.searchLimit = std::clamp(static_cast<int>(optInt(j, "search_limit").value_or(1000)), kMinSearchLimit, kMaxSearchLimit);
    s.searchRequests = std::clamp(static_cast<int>(optInt(j, "search_requests").value_or(kDefaultSearchRequests)), 1, 50);
    s.logToFile = optBool(j, "log_to_file").value_or(false);
    s.theme = oneOf("theme", {"dark", "light"}, "dark");
    if (const json* a = find(j, "accent"); a && a->is_array() && a->size() == 3) {
        for (std::size_t i = 0; i < 3; ++i) {
            if ((*a)[i].is_number()) s.accent[i] = std::clamp((*a)[i].get<float>(), 0.0f, 1.0f);
        }
    }
    s.fontSize = std::clamp(static_cast<float>(num(j, "font_size", 15.0)), 10.0f, 32.0f);
    s.uiScale = std::clamp(static_cast<float>(num(j, "ui_scale", 1.0)), 0.5f, 3.0f);
    section("defects", [&](const json& v) { s.defects = v.get<DefectThresholds>(); });
    section("currency", [&](const json& v) { s.currency = v.get<CurrencyFormat>(); });
    s.defaultSort = str(j, "default_sort", "score");
    if (!sortKeyFromId(s.defaultSort)) {
        problem(std::format("default_sort \"{}\" is unknown", s.defaultSort));
        s.defaultSort = "score";
    }
    s.defaultSortDesc = optBool(j, "default_sort_desc").value_or(true);
    section("filters", [&](const json& v) { s.filters = v.get<SearchFilters>(); });
    section("presets", [&](const json& v) {
        for (const auto& [name, f] : v.items()) {
            try {
                s.presets[name] = f.get<SearchFilters>();
            } catch (const std::exception&) {
                problem(std::format("preset \"{}\" could not be read", name));
            }
        }
    });
    section("blacklist", [&](const json& v) { s.blacklist = v.get<Blacklist>(); });
    section("template", [&](const json& v) {
        Template t = v.get<Template>();
        if (t.image.empty()) {
            problem("template has no image");
        } else {
            s.currentTemplate = std::move(t);
        }
    });
    s.cardView = optBool(j, "card_view").value_or(true);
    s.templateCardView = optBool(j, "template_card_view").value_or(true);
    s.templateSort = oneOf("template_sort", {"name", "date"}, "name");
    s.templateSortDesc = optBool(j, "template_sort_desc").value_or(false);
    s.instanceCardView = optBool(j, "instance_card_view").value_or(true);
    s.instanceSort = oneOf("instance_sort", {"label", "id", "status", "gpu", "price", "uptime"}, "label");
    s.instanceSortDesc = optBool(j, "instance_sort_desc").value_or(false);
    s.offerColumns = boolsFromJson(j, "offer_columns");
    s.instanceColumns = boolsFromJson(j, "instance_columns");
    s.sidebarCollapsed = optBool(j, "sidebar_collapsed").value_or(false);
    s.showActivityLog = optBool(j, "show_activity_log").value_or(true);
    s.apiKeysSplit = static_cast<float>(std::clamp(optDouble(j, "api_keys_split").value_or(0.58), 0.15, 0.85));
    if (const json* g = find(j, "gpu_models"); g && g->is_array()) {
        for (const auto& name : *g) {
            if (name.is_string() && !name.get<std::string>().empty()) s.gpuModels.push_back(name.get<std::string>());
        }
    }
}

Settings loadSettings(const ConfigPaths& paths, std::string& error) {
    error.clear();
    const auto text = platform::readTextFile(paths.settingsFile);
    if (!text) return Settings{};
    const json j = json::parse(*text, nullptr, false);
    if (j.is_discarded()) {
        error = "VastGui.json is not valid JSON - using defaults (the file will be overwritten on save)";
        std::error_code ec;
        auto backup = paths.settingsFile;
        backup += ".bad";
        std::filesystem::copy_file(paths.settingsFile, backup, std::filesystem::copy_options::overwrite_existing, ec);
        return Settings{};
    }
    Settings s;
    std::vector<std::string> problems;
    try {
        readSettings(j, s, &problems);
    } catch (const std::exception& e) {
        error = std::string("VastGui.json could not be read: ") + e.what() + " - using defaults";
        return Settings{};
    }
    if (!problems.empty()) {
        error = "VastGui.json: invalid or outdated values were reset to their defaults: " + fmt::join(problems, "; ");
    }
    if (!s.apiKeySealed.empty()) {
        std::string why;
        if (auto plain = secretbox::decrypt(s.apiKeySealed, paths.keyFile, why)) {
            s.apiKey = std::move(*plain);
            s.apiKeySealed.clear();
        } else {
            s.apiKey.clear();
            error += std::string(error.empty() ? "" : "\n") + "The API key stored in VastGui.json could not be decrypted (" + why +
                    "). Please enter it again in Settings.";
        }
    } else if (!s.apiKey.empty()) {
        std::string why;
        if (!saveSettings(paths, s, &why)) {
            error += std::string(error.empty() ? "" : "\n") + "The API key in VastGui.json is stored in plaintext and could not be encrypted: " + why;
        }
    }
    return s;
}

bool saveSettings(const ConfigPaths& paths, const Settings& s, std::string* error) {
    json j = s;
    if (!s.apiKey.empty()) {
        std::string why;
        auto sealed = secretbox::encrypt(s.apiKey, paths.keyFile, why);
        if (!sealed) {
            if (error) *error = "the API key could not be encrypted: " + why;
            return false;
        }
        j["api_key_enc"] = std::move(*sealed);
    } else if (!s.apiKeySealed.empty()) {
        j["api_key_enc"] = s.apiKeySealed;
    }
    if (!platform::writeTextFileAtomic(paths.settingsFile, j.dump(2), true)) {
        if (error) *error = "could not write " + platform::pathToUtf8(paths.settingsFile);
        return false;
    }
    return true;
}

}  // namespace vast
