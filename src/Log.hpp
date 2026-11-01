#pragma once
// Thread-safe application log feeding the activity log views and the optional log
// file. Every entry is masked against the configured API key.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace vast {

enum class LogLevel { Info, Command, Output, Warning, Error };

struct LogEntry {
    double time = 0;
    LogLevel level = LogLevel::Info;
    std::string text;
    std::uint64_t callId = 0;
};

class Log {
public:
    void add(LogLevel level, std::string text, std::uint64_t callId = 0);
    void info(std::string text) { add(LogLevel::Info, std::move(text)); }
    void warn(std::string text) { add(LogLevel::Warning, std::move(text)); }
    void error(std::string text) { add(LogLevel::Error, std::move(text)); }

    // Masks `secret` in every later entry. Earlier keys stay masked after the key is changed.
    void addSecret(std::string secret);
    void setFile(const std::filesystem::path& path, bool enabled);
    void clear();

    // Copies entries newer than `sinceVersion`; returns the current version.
    std::uint64_t copySince(std::uint64_t sinceVersion, std::vector<LogEntry>& out) const;
    [[nodiscard]] std::vector<LogEntry> snapshot() const;
    [[nodiscard]] std::uint64_t version() const;

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::uint64_t version_ = 0;       // total entries ever added
    std::vector<std::string> secrets_;
    std::ofstream file_;
    static constexpr std::size_t kMaxEntries = 8000;
};

[[nodiscard]] const char* logLevelName(LogLevel l);

}  // namespace vast
