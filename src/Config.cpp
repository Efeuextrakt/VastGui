#include "Config.hpp"

#include "Platform.hpp"
#include "SecretBox.hpp"

#include <algorithm>
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

void from_json(const json& j, Settings& s) {
    using namespace jsonutil;
    if (!j.is_object()) return;
    s.apiKey = str(j, "api_key");  // legacy plaintext, migrated by loadSettings
    s.apiKeySealed = str(j, "api_key_enc");
    s.serverUrl = str(j, "server_url", "https://console.vast.ai");
    if (s.serverUrl.empty()) s.serverUrl = "https://console.vast.ai";
    s.refreshIntervalSec = std::clamp(static_cast<int>(optInt(j, "refresh_interval_sec").value_or(30)), 5, 3600);
    s.autoRefresh = optBool(j, "auto_refresh").value_or(true);
    s.searchRefreshSec = std::clamp(static_cast<int>(optInt(j, "search_refresh_sec").value_or(0)), 0, 3600);
    s.searchLimit = std::clamp(static_cast<int>(optInt(j, "search_limit").value_or(1000)), kMinSearchLimit, kMaxSearchLimit);
    s.searchRequests = std::max(1, static_cast<int>(optInt(j, "search_requests").value_or(kDefaultSearchRequests)));
    s.logToFile = optBool(j, "log_to_file").value_or(false);
    s.theme = str(j, "theme", "dark");
    if (const json* a = find(j, "accent"); a && a->is_array() && a->size() == 3) {
        for (std::size_t i = 0; i < 3; ++i) {
            if ((*a)[i].is_number()) s.accent[i] = std::clamp((*a)[i].get<float>(), 0.0f, 1.0f);
        }
    }
    s.fontSize = std::clamp(static_cast<float>(num(j, "font_size", 15.0)), 10.0f, 32.0f);
    s.uiScale = std::clamp(static_cast<float>(num(j, "ui_scale", 1.0)), 0.5f, 3.0f);
    if (const json* d = find(j, "defects")) s.defects = d->get<DefectThresholds>();
    if (const json* c = find(j, "currency")) s.currency = c->get<CurrencyFormat>();
    s.defaultSort = str(j, "default_sort", "score");
    s.defaultSortDesc = optBool(j, "default_sort_desc").value_or(true);
    if (const json* f = find(j, "filters")) s.filters = f->get<SearchFilters>();
    if (const json* p = find(j, "presets"); p && p->is_object()) {
        for (const auto& [name, f] : p->items()) s.presets[name] = f.get<SearchFilters>();
    }
    if (const json* b = find(j, "blacklist")) s.blacklist = b->get<Blacklist>();
    if (const json* t = find(j, "template"); t && t->is_object()) s.currentTemplate = t->get<Template>();
    s.cardView = optBool(j, "card_view").value_or(true);
    s.templateCardView = optBool(j, "template_card_view").value_or(true);
    s.templateSort = str(j, "template_sort") == "date" ? "date" : "name";
    s.templateSortDesc = optBool(j, "template_sort_desc").value_or(false);
    s.instanceCardView = optBool(j, "instance_card_view").value_or(true);
    s.instanceSort = str(j, "instance_sort", "label");
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
    try {
        s = j.get<Settings>();
    } catch (const std::exception& e) {
        error = std::string("VastGui.json could not be read: ") + e.what();
        return Settings{};
    }
    if (!s.apiKeySealed.empty()) {
        std::string why;
        if (auto plain = secretbox::decrypt(s.apiKeySealed, paths.keyFile, why)) {
            s.apiKey = std::move(*plain);
            s.apiKeySealed.clear();
        } else {
            s.apiKey.clear();
            error = "The API key stored in VastGui.json could not be decrypted (" + why +
                    "). Please enter it again in Settings.";
        }
    } else if (!s.apiKey.empty()) {
        std::string why;
        if (!saveSettings(paths, s, &why)) {
            error = "The API key in VastGui.json is stored in plaintext and could not be encrypted: " + why;
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
