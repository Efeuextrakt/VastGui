#pragma once

#include "backend/ApiCall.hpp"

#include <stop_token>
#include <string>
#include <string_view>

namespace vast {

// Talks to https://console.vast.ai/api/v0 directly (WinHTTP / libcurl).
class RestBackend final {
public:
    explicit RestBackend(std::string apiKey, std::string serverUrl = "https://console.vast.ai");

    [[nodiscard]] ApiResult execute(const ApiCall& call, std::stop_token stop);

    // URL with /api/v0 prefix and quote_plus-encoded query (JSON values dumped). Exposed for tests.
    [[nodiscard]] static std::string buildUrl(const std::string& server, const RestSpec& spec);
    // The API key may only travel over https:// (plain http only to localhost); `why` explains a refusal.
    [[nodiscard]] static bool serverUrlAllowed(std::string_view url, std::string& why);

private:
    std::string apiKey_;
    std::string server_;
};

}  // namespace vast
