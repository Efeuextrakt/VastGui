#pragma once

#include "Log.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vast {

class App;

// Full activity log: every API request, its response (or error) and info messages, with filters.
class ConsoleView {
public:
    void draw(App& app);

private:
    bool prettyJson_ = true;
    bool showCommands_ = true;
    bool showOutput_ = true;
    bool showErrors_ = true;
    bool showInfo_ = true;
    bool autoScroll_ = true;
    std::string filter_;
    std::vector<LogEntry> cache_;
    std::uint64_t version_ = 0;
};

}  // namespace vast
