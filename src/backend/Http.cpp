#include "backend/Http.hpp"

#include <cctype>
#include <cstdio>
#include <memory>
#include <optional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include "Platform.hpp"
#else
#include <curl/curl.h>
#include <strings.h>
#endif

namespace vast {

std::string urlEncode(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

namespace {

struct HttpUrlParts {
    std::string scheme;     // lower-case
    std::string authority;  // as written (without userinfo)
    std::string host;       // lower-case
    std::string port;       // explicit or scheme default
    std::string rest;       // path + query (+ fragment), at least "/"
};

std::string httpLower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

std::optional<HttpUrlParts> httpSplitUrl(std::string_view u) {
    const auto sep = u.find("://");
    if (sep == std::string_view::npos || sep == 0) return std::nullopt;
    HttpUrlParts p;
    p.scheme = httpLower(u.substr(0, sep));
    const std::string_view after = u.substr(sep + 3);
    const auto end = after.find_first_of("/?#");
    std::string_view auth = after.substr(0, end);
    p.rest = end == std::string_view::npos ? "/" : std::string(after.substr(end));
    if (const auto at = auth.rfind('@'); at != std::string_view::npos) auth = auth.substr(at + 1);
    p.authority = std::string(auth);
    std::string_view host = auth, port;
    if (auth.starts_with('[')) {
        const auto rb = auth.find(']');
        if (rb == std::string_view::npos) return std::nullopt;
        host = auth.substr(0, rb + 1);
        if (rb + 1 < auth.size() && auth[rb + 1] == ':') port = auth.substr(rb + 2);
    } else if (const auto colon = auth.rfind(':'); colon != std::string_view::npos) {
        host = auth.substr(0, colon);
        port = auth.substr(colon + 1);
    }
    p.host = httpLower(host);
    p.port = !port.empty() ? std::string(port) : p.scheme == "https" ? "443" : p.scheme == "http" ? "80" : "";
    return p;
}

}  // namespace

std::string resolveRedirectUrl(std::string_view base, std::string_view location) {
    if (location.find("://") != std::string_view::npos) return std::string(location);
    const auto b = httpSplitUrl(base);
    if (!b) return std::string(location);
    if (location.starts_with("//")) return b->scheme + ":" + std::string(location);
    const std::string origin = b->scheme + "://" + b->authority;
    if (location.starts_with('/')) return origin + std::string(location);
    std::string dir = b->rest.substr(0, b->rest.find_first_of("?#"));
    dir = dir.substr(0, dir.rfind('/') + 1);
    if (location.starts_with('?')) return origin + b->rest.substr(0, b->rest.find_first_of("?#")) + std::string(location);
    return origin + dir + std::string(location);
}

bool sameOrigin(std::string_view a, std::string_view b) {
    const auto x = httpSplitUrl(a);
    const auto y = httpSplitUrl(b);
    return x && y && x->scheme == y->scheme && x->host == y->host && x->port == y->port;
}

RedirectStep planRedirect(std::string_view originalUrl, std::string_view currentUrl, std::string_view location,
                          int status, std::string_view method, bool withBody) {
    RedirectStep s;
    s.url = resolveRedirectUrl(currentUrl, location);
    s.method = std::string(method);
    s.withBody = withBody;
    const auto next = httpSplitUrl(s.url);
    const auto cur = httpSplitUrl(currentUrl);
    if (!next || (next->scheme != "https" && next->scheme != "http")) {
        s.error = "refused redirect to unsupported URL " + s.url;
        return s;
    }
    if (cur && cur->scheme == "https" && next->scheme != "https") {
        s.error = "refused redirect from https to insecure URL " + s.url;
        return s;
    }
    if (status == 303 || ((status == 301 || status == 302) && method == "POST")) {
        if (method != "HEAD") s.method = "GET";
        s.withBody = false;
    }
    if (s.withBody && !sameOrigin(s.url, originalUrl)) {
        s.error = "refused to resend the request body to another host (" + next->host + ")";
        return s;
    }
    return s;
}

#ifdef _WIN32

namespace {

struct InternetCloser {
    void operator()(HINTERNET h) const {
        if (h) WinHttpCloseHandle(h);
    }
};
using UniqueInternet = std::unique_ptr<void, InternetCloser>;

std::string lastErrorText(const char* what) {
    return std::string(what) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

std::string queryHeader(HINTERNET req, DWORD info) {
    DWORD size = 0;
    WinHttpQueryHeaders(req, info, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(req, info, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    buf.resize(size / sizeof(wchar_t));
    return platform::narrow(buf);
}

}  // namespace

HttpResponse httpRequest(const HttpRequest& req, std::stop_token stop) {
    HttpResponse res;
    UniqueInternet session(WinHttpOpen(L"VastGui/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session.reset(WinHttpOpen(L"VastGui/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0));
    }
    if (!session) {
        res.error = lastErrorText("WinHttpOpen");
        return res;
    }
    const int t = static_cast<int>(req.timeout.count());
    WinHttpSetTimeouts(session.get(), t, t, t, t);
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    // Redirects are followed by hand: WinHTTP would replay every added header - including Authorization - to
    // whatever host the Location points at.
    std::string url = req.url;
    std::string method = req.method;
    bool withBody = !req.body.empty();
    for (int hop = 0;; ++hop) {
        const std::wstring wurl = platform::widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        uc.dwHostNameLength = static_cast<DWORD>(-1);
        uc.dwUrlPathLength = static_cast<DWORD>(-1);
        uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) ||
            (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP)) {
            res.error = "Invalid URL: " + url;
            return res;
        }
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
        const bool sendAuth = sameOrigin(url, req.url);

        UniqueInternet connect(WinHttpConnect(session.get(), host.c_str(), uc.nPort, 0));
        if (!connect) {
            res.error = lastErrorText("WinHttpConnect");
            return res;
        }
        const std::wstring wmethod = platform::widen(method);
        UniqueInternet request(WinHttpOpenRequest(connect.get(), wmethod.c_str(), path.c_str(), nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request) {
            res.error = lastErrorText("WinHttpOpenRequest");
            return res;
        }
        DWORD noRedirects = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &noRedirects, sizeof(noRedirects));

        std::wstring headers;
        for (const auto& [k, v] : req.headers) {
            if (!sendAuth && _stricmp(k.c_str(), "Authorization") == 0) continue;
            if (!withBody && _stricmp(k.c_str(), "Content-Type") == 0) continue;
            // CR/LF in a value would inject extra headers.
            if (k.find_first_of("\r\n") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) continue;
            headers += platform::widen(k + ": " + v + "\r\n");
        }
        if (!headers.empty()) {
            WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
        }

        if (stop.stop_requested()) {
            res.error = "cancelled";
            return res;
        }
        void* body = withBody ? const_cast<char*>(req.body.data()) : WINHTTP_NO_REQUEST_DATA;
        const DWORD bodyLen = withBody ? static_cast<DWORD>(req.body.size()) : 0;
        if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, bodyLen, bodyLen, 0)) {
            res.error = lastErrorText("WinHttpSendRequest");
            return res;
        }
        if (!WinHttpReceiveResponse(request.get(), nullptr)) {
            res.error = lastErrorText("WinHttpReceiveResponse");
            return res;
        }

        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

        const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (redirect) {
            const std::string location = queryHeader(request.get(), WINHTTP_QUERY_LOCATION);
            if (!location.empty()) {
                if (hop >= 10) {
                    res.error = "too many redirects";
                    return res;
                }
                RedirectStep step = planRedirect(req.url, url, location, static_cast<int>(status), method, withBody);
                if (!step.error.empty()) {
                    res.error = step.error;
                    return res;
                }
                url = std::move(step.url);
                method = std::move(step.method);
                withBody = step.withBody;
                continue;
            }
        }

        res.status = static_cast<int>(status);
        res.contentType = queryHeader(request.get(), WINHTTP_QUERY_CONTENT_TYPE);
        res.retryAfter = queryHeader(request.get(), WINHTTP_QUERY_RETRY_AFTER);

        while (true) {
            if (stop.stop_requested()) {
                res.error = "cancelled";
                break;
            }
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &avail)) {
                res.error = lastErrorText("WinHttpQueryDataAvailable");
                break;
            }
            if (avail == 0) break;
            if (res.body.size() + avail > kMaxHttpBody) {
                res.error = "response too large";
                res.status = 0;
                break;
            }
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), chunk.data(), avail, &read)) {
                res.error = lastErrorText("WinHttpReadData");
                break;
            }
            res.body.append(chunk.data(), read);
        }
        return res;
    }
}

#else

namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

struct CurlCtx {
    std::string* body;
    std::stop_token stop;
    bool tooLarge = false;
};

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* user) {
    auto* ctx = static_cast<CurlCtx*>(user);
    if (ctx->body->size() + size * nmemb > kMaxHttpBody) {
        ctx->tooLarge = true;
        return 0;  // aborts with CURLE_WRITE_ERROR
    }
    ctx->body->append(ptr, size * nmemb);
    return size * nmemb;
}

int progressCb(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<CurlCtx*>(user);
    return ctx->stop.stop_requested() ? 1 : 0;
}

}  // namespace

HttpResponse httpRequest(const HttpRequest& req, std::stop_token stop) {
    static CurlGlobal global;
    // Redirects are followed by hand (like the WinHTTP path) so the Authorization header and request bodies, which
    // may contain the API key, never go to another origin.
    std::string url = req.url;
    std::string method = req.method;
    bool withBody = !req.body.empty();
    for (int hop = 0;; ++hop) {
        HttpResponse res;
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
        if (!curl) {
            res.error = "curl_easy_init failed";
            return res;
        }
        CurlCtx ctx{&res.body, stop};
        const bool sendAuth = sameOrigin(url, req.url);
        curl_slist* hdrs = nullptr;
        for (const auto& [k, v] : req.headers) {
            if (k.find_first_of("\r\n") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) continue;
            if (!sendAuth && strcasecmp(k.c_str(), "Authorization") == 0) continue;
            if (!withBody && strcasecmp(k.c_str(), "Content-Type") == 0) continue;
            hdrs = curl_slist_append(hdrs, (k + ": " + v).c_str());
        }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> hdrGuard(hdrs, &curl_slist_free_all);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
#else
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout.count()));
        curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "VastGui/1.0");
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &writeCb);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &progressCb);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &ctx);
        if (withBody) {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, req.body.data());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
        const CURLcode rc = curl_easy_perform(curl.get());
        if (rc != CURLE_OK) {
            res.error = ctx.tooLarge ? "response too large" : curl_easy_strerror(rc);
            return res;
        }
        long code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
        char* location = nullptr;
        const bool redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
        if (redirect && curl_easy_getinfo(curl.get(), CURLINFO_REDIRECT_URL, &location) == CURLE_OK && location) {
            if (hop >= 10) {
                res.error = "too many redirects";
                return res;
            }
            RedirectStep step = planRedirect(req.url, url, location, static_cast<int>(code), method, withBody);
            if (!step.error.empty()) {
                res.error = step.error;
                return res;
            }
            url = std::move(step.url);
            method = std::move(step.method);
            withBody = step.withBody;
            continue;
        }
        res.status = static_cast<int>(code);
        char* ct = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &ct);
        if (ct) res.contentType = ct;
        return res;
    }
}

#endif

}  // namespace vast
