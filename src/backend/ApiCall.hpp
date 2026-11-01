#pragma once
// Every GUI action is described once as an ApiCall: the vast.ai REST request (mirroring what the official vastai
// CLI / SDK sends) plus how its response is presented. RestBackend executes it.

#include "model/Json.hpp"

#include <any>
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace vast {

enum class HttpMethod { Get, Post, Put, Delete };

[[nodiscard]] inline const char* httpMethodName(HttpMethod m) {
    switch (m) {
        case HttpMethod::Post: return "POST";
        case HttpMethod::Put: return "PUT";
        case HttpMethod::Delete: return "DELETE";
        case HttpMethod::Get: break;
    }
    return "GET";
}

// How the REST response is mapped onto the shape the views expect.
enum class RestTransform {
    None,           // response JSON as-is
    ExtractKey,     // response[extractKey]
    PollResultUrl,  // response.result_url -> fetch text (logs, execute)
    StripApiKey,    // drop "api_key" from the response (show user)
};

struct RestSpec {
    HttpMethod method = HttpMethod::Get;
    std::string path;                  // "/instances/" (=> /api/v0), "/api/v1/..." or absolute "https://..."
    json query = json::object();       // values are JSON-encoded unless strings
    std::optional<json> body;
    RestTransform transform = RestTransform::None;
    std::string extractKey;
    bool injectApiKey = false;         // some endpoints expect "api_key" inside the JSON body
};

struct ApiResult;

struct ApiCall {
    std::string title;                    // "Search offers"
    RestSpec rest;
    // Set when the request could not be built (e.g. an invalid search query): the call fails with this message
    // without sending anything.
    std::string error;
    std::chrono::seconds timeout{90};
    bool quiet = false;                   // don't echo into the activity log (e.g. background polling)
    bool sensitiveOutput = false;         // log the request but never the response (contains secrets)
    // Optional post-processing on the worker thread after a successful call (e.g. converting large JSON into
    // model structs); its result is delivered in ApiResult::prepared, keeping heavy work off the UI thread.
    std::function<std::any(ApiResult&)> prepare;
};

struct ApiResult {
    bool ok = false;
    int code = 0;          // HTTP status
    json data;             // parsed JSON (null when not JSON)
    std::string text;      // raw response body
    std::string error;     // human readable error
    double elapsedMs = 0;
    bool cancelled = false;
    std::any prepared;     // result of ApiCall::prepare
};

}  // namespace vast
