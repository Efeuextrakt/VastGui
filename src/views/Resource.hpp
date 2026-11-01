#pragma once
// Cached result of one API call, with loading/error state and auto-refresh.

#include "backend/ApiCall.hpp"

#include <functional>
#include <string>

namespace vast {

class App;

struct Resource {
    json data;
    bool loading = false;
    bool loaded = false;
    std::string error;
    double fetchedAt = 0;    // last success
    double attemptedAt = 0;  // last attempt (success or failure)

    using After = std::function<void(Resource&, const ApiResult&)>;

    // Fetches unless a request is already in flight.
    void fetch(App& app, ApiCall call, After after = {});
    // Fetches when never loaded, or when auto-refresh is due.
    void autoFetch(App& app, const ApiCall& call, After after = {});
    [[nodiscard]] bool empty() const { return data.is_null() || (data.is_array() && data.empty()); }
    [[nodiscard]] double age() const;
};

}  // namespace vast
