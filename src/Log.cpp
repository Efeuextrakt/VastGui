#include "Log.hpp"

#include "Platform.hpp"
#include "logic/Format.hpp"

#include <algorithm>
namespace vast {

const char* logLevelName(LogLevel l) {
    switch (l) {
        case LogLevel::Command: return "CMD";
        case LogLevel::Output: return "OUT";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Info: break;
    }
    return "INFO";
}

void Log::add(LogLevel level, std::string text, std::uint64_t callId) {
    std::lock_guard lock(mutex_);
    for (const auto& s : secrets_) text = fmt::maskSecret(std::move(text), s);
    LogEntry e{fmt::nowEpoch(), level, std::move(text), callId};
    if (file_.is_open()) {
        file_ << fmt::dateTimeFromEpoch(e.time) << " [" << logLevelName(level) << "] " << e.text << '\n';
        file_.flush();
    }
    entries_.push_back(std::move(e));
    ++version_;
    while (entries_.size() > kMaxEntries) entries_.pop_front();
}

void Log::addSecret(std::string secret) {
    std::lock_guard lock(mutex_);
    if (secret.size() < 4 || std::find(secrets_.begin(), secrets_.end(), secret) != secrets_.end()) return;
    secrets_.push_back(std::move(secret));
}

void Log::setFile(const std::filesystem::path& path, bool enabled) {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) file_.close();
    if (enabled) {
        // Created owner-only (POSIX) if missing; an existing file keeps its permissions.
        (void)platform::createFileExclusive(path, {});
        file_.open(path, std::ios::app);
    }
}

void Log::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

std::uint64_t Log::copySince(std::uint64_t sinceVersion, std::vector<LogEntry>& out) const {
    std::lock_guard lock(mutex_);
    const std::uint64_t first = version_ - entries_.size();
    const std::uint64_t start = std::max(sinceVersion, first);
    for (std::uint64_t v = start; v < version_; ++v) out.push_back(entries_[static_cast<std::size_t>(v - first)]);
    return version_;
}

std::vector<LogEntry> Log::snapshot() const {
    std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

std::uint64_t Log::version() const {
    std::lock_guard lock(mutex_);
    return version_;
}

}  // namespace vast
