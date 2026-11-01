#pragma once

#include <string>

namespace vast {

enum class ApiKeySource { None, Environment, Settings };

struct ResolvedApiKey {
    std::string key;
    ApiKeySource source = ApiKeySource::None;
    std::string location;  // env var name or file (never the key)
};

[[nodiscard]] const char* apiKeySourceLabel(ApiKeySource s);
// Lookup order: VAST_API_KEY env var, then the (encrypted) key from VastGui.json.
[[nodiscard]] ResolvedApiKey resolveApiKey(const std::string& settingsKey);

}  // namespace vast
