#pragma once
// Thin OS abstraction (paths, env, URLs, files, dialogs). No ImGui dependency.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vast::platform {

#ifdef _WIN32
[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);
#endif

[[nodiscard]] std::filesystem::path executableDir();
[[nodiscard]] std::filesystem::path homeDir();
[[nodiscard]] std::filesystem::path appDataDir();  // %APPDATA%\VastGui or ~/.config/VastGui
[[nodiscard]] std::optional<std::string> getEnv(const char* name);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& p);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view s);

bool openUrl(const std::string& url);
bool openPath(const std::filesystem::path& p);

[[nodiscard]] std::optional<std::string> readTextFile(const std::filesystem::path& p);
// Writes via <p>.tmp + rename. `ownerOnly` creates the file with mode 0600 on POSIX (Windows: the per-user profile
// ACLs already apply).
bool writeTextFileAtomic(const std::filesystem::path& p, std::string_view content, bool ownerOnly = false);

enum class CreateResult { Created, Exists, Failed };
// Creates `p` exclusively (never follows/overwrites an existing file), owner-only on POSIX.
[[nodiscard]] CreateResult createFileExclusive(const std::filesystem::path& p, std::string_view content);

// Fills `buf` from the OS CSPRNG.
[[nodiscard]] bool secureRandom(void* buf, std::size_t n);


// Native file dialogs; nullopt when cancelled/unavailable.
[[nodiscard]] std::optional<std::filesystem::path> openFileDialog(const char* title);
[[nodiscard]] std::optional<std::filesystem::path> saveFileDialog(const char* title, const char* defaultName);

}  // namespace vast::platform
