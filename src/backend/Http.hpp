#pragma once

#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vast {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::chrono::milliseconds timeout{60000};
};

struct HttpResponse {
    int status = 0;  // 0 => transport error
    std::string body;
    std::string contentType;
    std::string retryAfter;
    std::string error;
};

// Blocking HTTPS request (WinHTTP on Windows, libcurl elsewhere). Redirects are followed, but the Authorization
// header is only ever sent to the original scheme/host/port and https -> http downgrades are refused.
[[nodiscard]] HttpResponse httpRequest(const HttpRequest& req, std::stop_token stop = {});
[[nodiscard]] std::string urlEncode(std::string_view s);  // like Python's quote_plus

// Redirect helpers (exposed for tests).
[[nodiscard]] std::string resolveRedirectUrl(std::string_view base, std::string_view location);
[[nodiscard]] bool sameOrigin(std::string_view a, std::string_view b);  // scheme, host and port

struct RedirectStep {
    std::string url;
    std::string method;
    bool withBody = false;
    std::string error;  // non-empty: do not follow
};
// Next hop for a 3xx response: refuses https -> http and resending a request body (which may hold the API key
// or other secrets) to another origin; 303 and 301/302 after POST continue as GET without body.
[[nodiscard]] RedirectStep planRedirect(std::string_view originalUrl, std::string_view currentUrl,
                                        std::string_view location, int status, std::string_view method, bool withBody);

inline constexpr std::size_t kMaxHttpBody = std::size_t{512} << 20;

}  // namespace vast
