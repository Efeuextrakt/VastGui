#pragma once
// Persistent application settings stored in VastGui.json.

#include "logic/DefectFilter.hpp"
#include "logic/Format.hpp"
#include "logic/SearchFilters.hpp"
#include "model/Json.hpp"
#include "model/Template.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace vast {

struct ConfigPaths {
    std::filesystem::path dir;
    std::filesystem::path settingsFile;  // VastGui.json
    std::filesystem::path iniFile;       // imgui.ini
    std::filesystem::path logFile;       // VastGui.log
    // Encryption key for secrets in VastGui.json. Always in the per-user config dir, also in portable mode.
    std::filesystem::path keyFile;
    bool portable = false;               // files live next to the executable
};

// Portable mode when VastGui.json already exists next to the exe, otherwise %APPDATA%\VastGui.
[[nodiscard]] ConfigPaths resolveConfigPaths();

inline constexpr std::array<float, 3> kDefaultAccent{0.231f, 0.482f, 0.965f};
inline constexpr int kMinSearchLimit = 64;
inline constexpr int kMaxSearchLimit = 10000;
inline constexpr int kDefaultSearchRequests = 5;  // recommended 3-10

struct Settings {
    // Backend / account
    // Optional; the VAST_API_KEY env var takes precedence. Stored encrypted as "api_key_enc" (see SecretBox).
    std::string apiKey;
    // Encrypted key that could not be decrypted (key file missing / from another PC); written back unchanged so
    // it is not lost, until the user saves a new key.
    std::string apiKeySealed;
    std::string serverUrl = "https://console.vast.ai";
    int refreshIntervalSec = 30;
    bool autoRefresh = true;
    int searchRefreshSec = 0;  // 0 = manual
    // vast.ai returns a random sample of the matching offers; larger limits and several merged
    // requests make the result far more complete and stable (see docs/FEATURES.md "Search completeness").
    int searchLimit = 1000;
    int searchRequests = kDefaultSearchRequests;
    bool logToFile = false;

    // Appearance
    std::string theme = "dark";
    std::array<float, 3> accent = kDefaultAccent;
    float fontSize = 15.0f;
    float uiScale = 1.0f;

    // Search defaults / state
    DefectThresholds defects;
    CurrencyFormat currency;
    std::string defaultSort = "score";
    bool defaultSortDesc = true;
    SearchFilters filters;
    std::map<std::string, SearchFilters> presets;
    Blacklist blacklist;
    Template currentTemplate = Template::defaultTemplate();
    bool cardView = true;
    bool templateCardView = true;
    std::string templateSort = "name";  // template cards: "name" or "date"
    bool templateSortDesc = false;
    bool instanceCardView = true;
    std::string instanceSort = "label";  // instance cards: label, id, status, gpu, price, uptime
    bool instanceSortDesc = false;
    std::vector<bool> offerColumns;
    std::vector<bool> instanceColumns;

    // Misc UI state
    bool sidebarCollapsed = false;
    bool showActivityLog = true;
    float apiKeysSplit = 0.58f;  // Keys > API keys: list width / page width
    // GPU model names from vast.ai's /gpu_types/ (cached for offline starts; refreshed once per start).
    std::vector<std::string> gpuModels;
};

void to_json(json& j, const Settings& s);
void from_json(const json& j, Settings& s);
// Like from_json, but lists the values that were invalid and replaced by defaults (missing keys are not problems).
void readSettings(const json& j, Settings& s, std::vector<std::string>* problems);

// Missing/corrupt files yield defaults; `error` describes what went wrong. A legacy plaintext "api_key" is
// re-saved encrypted right away.
[[nodiscard]] Settings loadSettings(const ConfigPaths& paths, std::string& error);
// Never writes the API key in plaintext: if it cannot be encrypted nothing is written and `error` is set.
bool saveSettings(const ConfigPaths& paths, const Settings& s, std::string* error = nullptr);

}  // namespace vast
