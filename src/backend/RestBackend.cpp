#include "backend/RestBackend.hpp"

#include "backend/Http.hpp"
#include "logic/Format.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <regex>
#include <thread>

namespace vast {

RestBackend::RestBackend(std::string apiKey, std::string serverUrl)
    : apiKey_(std::move(apiKey)), server_(std::move(serverUrl)) {
    while (!server_.empty() && server_.back() == '/') server_.pop_back();
}

bool RestBackend::serverUrlAllowed(std::string_view url, std::string& why) {
    std::string lower(url);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.starts_with("https://")) return true;
    if (lower.starts_with("http://")) {
        std::string_view hostport = std::string_view(lower).substr(7);
        hostport = hostport.substr(0, hostport.find_first_of("/?#"));
        if (const auto at = hostport.rfind('@'); at != std::string_view::npos) hostport = hostport.substr(at + 1);
        std::string_view host = hostport;
        if (hostport.starts_with('[')) {
            const auto rb = hostport.find(']');
            host = hostport.substr(0, rb == std::string_view::npos ? std::string_view::npos : rb + 1);
        } else {
            host = hostport.substr(0, hostport.find(':'));
        }
        if (host == "localhost" || host == "127.0.0.1" || host == "[::1]") return true;
        why = "Refusing to send the API key over unencrypted http:// - the server URL must use https://";
        return false;
    }
    why = "The server URL must start with https://";
    return false;
}

std::string RestBackend::buildUrl(const std::string& server, const RestSpec& spec) {
    static const std::regex versioned(R"(^/api/v\d+/)");
    std::string path = spec.path;
    std::string url;
    if (path.starts_with("http://") || path.starts_with("https://")) {
        url = path;
    } else {
        if (!std::regex_search(path, versioned)) path = "/api/v0" + path;
        url = server + path;
    }
    if (spec.query.is_object() && !spec.query.empty()) {
        std::string qs;
        for (const auto& [k, v] : spec.query.items()) {
            if (!qs.empty()) qs += '&';
            qs += k + "=" + urlEncode(v.is_string() ? v.get<std::string>() : v.dump());
        }
        url += "?" + qs;
    }
    return url;
}

namespace {

bool sleepInterruptible(std::chrono::milliseconds d, const std::stop_token& stop) {
    const auto until = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < until) {
        if (stop.stop_requested()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return true;
}

std::string errorMessage(const HttpResponse& r) {
    if (r.status == 0) return r.error.empty() ? "network error" : r.error;
    const json j = json::parse(r.body, nullptr, false);
    std::string msg;
    if (!j.is_discarded() && j.is_object()) {
        msg = jsonutil::str(j, "msg");
        if (msg.empty()) msg = jsonutil::str(j, "error_description");
        if (msg.empty() && j.contains("error") && j["error"].is_string()) msg = j["error"].get<std::string>();
    }
    if (msg.empty()) msg = r.body.substr(0, 300);
    if (r.status == 401 && msg.empty()) msg = "Unauthorized - check your API key";
    return "HTTP " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg);
}

}  // namespace

ApiResult RestBackend::execute(const ApiCall& call, std::stop_token stop) {
    ApiResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&](ApiResult& r) -> ApiResult {
        r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!apiKey_.empty()) {
            r.text = fmt::maskSecret(std::move(r.text), apiKey_);
            r.error = fmt::maskSecret(std::move(r.error), apiKey_);
        }
        return std::move(r);
    };

    if (!call.error.empty()) {
        res.error = call.error;
        return finish(res);
    }
    const RestSpec& spec = call.rest;

    HttpRequest req;
    req.method = httpMethodName(spec.method);
    req.url = buildUrl(server_, spec);
    if (std::string why; !serverUrlAllowed(req.url, why)) {
        res.error = why;
        return finish(res);
    }
    req.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(call.timeout);
    req.headers.emplace_back("Accept", "application/json");
    if (!apiKey_.empty()) req.headers.emplace_back("Authorization", "Bearer " + apiKey_);
    if (spec.body) {
        json body = *spec.body;
        if (spec.injectApiKey && body.is_object()) body["api_key"] = apiKey_;
        req.body = body.dump();
        req.headers.emplace_back("Content-Type", "application/json");
    } else if (spec.method != HttpMethod::Get) {
        req.body = "{}";
        req.headers.emplace_back("Content-Type", "application/json");
    }

    HttpResponse resp;
    auto backoff = std::chrono::milliseconds(150);
    for (int attempt = 0; attempt < 3; ++attempt) {
        resp = httpRequest(req, stop);
        if (stop.stop_requested()) {
            res.cancelled = true;
            res.error = "cancelled";
            return finish(res);
        }
        // 429 means the request was rejected unprocessed; 5xx gateway errors may hide a request that did go
        // through, so only GETs are retried there (a repeated PUT /asks/ could rent a second instance).
        const bool gatewayError = resp.status == 502 || resp.status == 503 || resp.status == 504;
        const bool retryable = resp.status == 429 || (gatewayError && spec.method == HttpMethod::Get);
        if (!retryable || attempt == 2) break;
        auto wait = backoff;
        if (auto ra = jsonutil::parseDouble(resp.retryAfter); ra && *ra <= 60) {
            wait = std::chrono::milliseconds(static_cast<long long>(*ra * 1000));
        }
        if (resp.body.find("search_quota_exceeded") != std::string::npos) break;
        if (!sleepInterruptible(wait, stop)) break;
        backoff = backoff * 3 / 2;
    }

    res.code = resp.status;
    res.text = resp.body;
    if (resp.status < 200 || resp.status >= 300) {
        res.error = errorMessage(resp);
        return finish(res);
    }
    res.data = json::parse(resp.body, nullptr, false);
    if (res.data.is_discarded()) res.data = nullptr;

    switch (spec.transform) {
        case RestTransform::ExtractKey:
            if (res.data.is_object() && res.data.contains(spec.extractKey)) {
                json inner = res.data[spec.extractKey];
                res.data = std::move(inner);
            }
            break;
        case RestTransform::StripApiKey:
            if (res.data.is_object()) res.data.erase("api_key");
            res.text = res.data.dump(1);
            break;
        case RestTransform::PollResultUrl: {
            const std::string url = res.data.is_object() ? jsonutil::str(res.data, "result_url") : std::string{};
            if (url.empty()) break;
            HttpRequest poll;
            poll.url = url;
            poll.timeout = std::chrono::milliseconds(20000);
            const auto deadline = std::chrono::steady_clock::now() + call.timeout;
            while (std::chrono::steady_clock::now() < deadline) {
                if (!sleepInterruptible(std::chrono::milliseconds(800), stop)) {
                    res.cancelled = true;
                    res.error = "cancelled";
                    return finish(res);
                }
                HttpResponse pr = httpRequest(poll, stop);
                if (pr.status == 200) {
                    res.text = pr.body;
                    res.data = nullptr;
                    res.ok = true;
                    return finish(res);
                }
            }
            res.error = "timed out waiting for result";
            return finish(res);
        }
        case RestTransform::None: break;
    }
    if (auto ok = jsonutil::optBool(res.data, "success"); ok && !*ok) {
        res.error = jsonutil::str(res.data, "msg",
                                  jsonutil::str(res.data, "error_msg", jsonutil::str(res.data, "error", "request failed")));
        return finish(res);
    }
    res.ok = true;
    return finish(res);
}

}  // namespace vast
