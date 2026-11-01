#include "backend/VastClient.hpp"

#include "backend/RestBackend.hpp"
#include "logic/Format.hpp"

#include <format>

namespace vast {

VastClient::VastClient(Log& log, std::size_t threads) : log_(log), pool_(threads) {}

VastClient::~VastClient() { pool_.shutdown(); }

void VastClient::configure(const BackendConfig& cfg) {
    std::string server = cfg.serverUrl.empty() ? std::string("https://console.vast.ai") : cfg.serverUrl;
    while (!server.empty() && server.back() == '/') server.pop_back();
    auto rest = std::make_shared<RestBackend>(cfg.apiKey, server);
    std::lock_guard lock(mutex_);
    rest_ = std::move(rest);
    server_ = std::move(server);
    hasKey_ = !cfg.apiKey.empty();
    log_.addSecret(cfg.apiKey);
    log_.info("vast.ai API: " + server_);
}

std::string VastClient::requestLine(const ApiCall& call) const {
    if (!call.error.empty()) return call.title + " (not sent)";
    std::string server;
    {
        std::lock_guard lock(mutex_);
        server = server_;
    }
    std::string url = RestBackend::buildUrl(server, call.rest);
    if (url.starts_with(server)) url.erase(0, server.size());
    constexpr std::size_t kMax = 240;  // search filters make long query strings
    if (url.size() > kMax) url = url.substr(0, kMax) + "...";
    return std::string(httpMethodName(call.rest.method)) + " " + url;
}

std::uint64_t VastClient::submit(ApiCall call, Callback cb) {
    const std::uint64_t id = nextId_++;
    std::shared_ptr<RestBackend> backend;
    {
        std::lock_guard lock(mutex_);
        backend = rest_;
        running_[id] = call.title;
    }
    if (!call.quiet) log_.add(LogLevel::Command, "> " + requestLine(call), id);
    ++inFlight_;

    auto task = [this, id, backend, call = std::move(call), cb = std::move(cb)](std::stop_token st) mutable {
        ApiResult r;
        if (!backend) {
            r.error = "not configured";
        } else {
            try {
                r = backend->execute(call, st);
                if (r.ok && call.prepare) r.prepared = call.prepare(r);
            } catch (const std::exception& e) {
                r.ok = false;
                r.error = std::string("internal error: ") + e.what();
            }
        }
        finished_.push(Finished{id, std::move(call), std::move(r), std::move(cb)});
    };
    if (!pool_.post(std::move(task))) {
        // Shutting down: the call never runs and is never polled, so undo the bookkeeping here.
        --inFlight_;
        std::lock_guard lock(mutex_);
        running_.erase(id);
    }
    return id;
}

void VastClient::poll() {
    for (auto& f : finished_.drain()) {
        --inFlight_;
        {
            std::lock_guard lock(mutex_);
            running_.erase(f.id);
        }
        if (!f.call.quiet || !f.result.ok) {
            if (f.result.ok) {
                constexpr std::size_t kMax = 6000;
                // Large responses (offer searches are several MB) are previewed from the raw text instead of
                // pretty-printing everything on the UI thread just to truncate it.
                std::string out;
                const std::size_t total = f.result.text.size();
                if (f.call.sensitiveOutput) {
                    out = "(output hidden: contains secrets)";
                } else if (total > kMax) {
                    out = f.result.text.substr(0, kMax) + std::format("\n... ({} bytes total, truncated)", total);
                } else {
                    out = !f.result.data.is_null() ? f.result.data.dump(1) : f.result.text;
                    if (out.size() > kMax) {
                        out = out.substr(0, kMax) + std::format("\n... ({} bytes total, truncated)", out.size());
                    }
                }
                log_.add(LogLevel::Output,
                         std::format("[{} ok, {:.0f} ms]{}{}", f.call.title, f.result.elapsedMs, out.empty() ? "" : "\n", out),
                         f.id);
            } else if (!f.result.cancelled) {
                log_.add(LogLevel::Error, std::format("[{} failed] {}", f.call.title, f.result.error), f.id);
            }
        }
        if (hook_) hook_(f.call, f.result);
        if (f.cb) f.cb(f.result);
    }
}

std::vector<std::string> VastClient::inFlightTitles() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    for (const auto& [id, t] : running_) out.push_back(t);
    return out;
}

std::string VastClient::serverUrl() const {
    std::lock_guard lock(mutex_);
    return server_;
}

bool VastClient::hasApiKey() const {
    std::lock_guard lock(mutex_);
    return hasKey_;
}

}  // namespace vast
