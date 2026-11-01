#include "backend/ApiKey.hpp"

#include "Platform.hpp"
#include "logic/Format.hpp"

namespace vast {

const char* apiKeySourceLabel(ApiKeySource s) {
    switch (s) {
        case ApiKeySource::Environment: return "VAST_API_KEY environment variable";
        case ApiKeySource::Settings: return "VastGui.json";
        case ApiKeySource::None: break;
    }
    return "not configured";
}

ResolvedApiKey resolveApiKey(const std::string& settingsKey) {
    if (auto env = platform::getEnv("VAST_API_KEY")) {
        const std::string k = fmt::trim(*env);
        if (!k.empty()) return {k, ApiKeySource::Environment, "VAST_API_KEY"};
    }
    if (const std::string k = fmt::trim(settingsKey); !k.empty()) return {k, ApiKeySource::Settings, "VastGui.json"};
    return {};
}

}  // namespace vast
