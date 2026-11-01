#pragma once
// Facade used by the UI: runs ApiCalls on a worker pool through the REST backend
// and hands results back on the UI thread via poll().

#include "Log.hpp"
#include "backend/ApiCall.hpp"
#include "backend/ThreadPool.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vast {

class RestBackend;

struct BackendConfig {
    std::string apiKey;
    std::string serverUrl = "https://console.vast.ai";
};

class VastClient {
public:
    using Callback = std::function<void(const ApiResult&)>;
    using CompletionHook = std::function<void(const ApiCall&, const ApiResult&)>;

    VastClient(Log& log, std::size_t threads = 6);
    ~VastClient();

    VastClient(const VastClient&) = delete;
    VastClient& operator=(const VastClient&) = delete;

    void configure(const BackendConfig& cfg);

    // Queues the call; `cb` runs on the UI thread inside poll().
    std::uint64_t submit(ApiCall call, Callback cb = {});
    // Delivers finished results. Call once per frame on the UI thread.
    void poll();

    void setCompletionHook(CompletionHook hook) { hook_ = std::move(hook); }

    [[nodiscard]] int inFlight() const { return inFlight_.load(); }
    [[nodiscard]] std::vector<std::string> inFlightTitles() const;
    [[nodiscard]] std::string serverUrl() const;
    [[nodiscard]] bool hasApiKey() const;
    // How this call appears in the activity log: "POST /api/v0/bundles/".
    [[nodiscard]] std::string requestLine(const ApiCall& call) const;

private:
    struct Finished {
        std::uint64_t id;
        ApiCall call;
        ApiResult result;
        Callback cb;
    };

    Log& log_;
    ThreadPool pool_;
    ResultQueue<Finished> finished_;
    std::atomic<int> inFlight_{0};
    std::atomic<std::uint64_t> nextId_{1};

    mutable std::mutex mutex_;
    std::shared_ptr<RestBackend> rest_;
    std::string server_ = "https://console.vast.ai";
    bool hasKey_ = false;
    std::map<std::uint64_t, std::string> running_;

    CompletionHook hook_;
};

}  // namespace vast
