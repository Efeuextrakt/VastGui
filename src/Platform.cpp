#include "Platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <thread>
extern char** environ;
#endif

namespace vast::platform {

#ifdef _WIN32
std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}
#endif

std::string pathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    return narrow(p.native());
#else
    return p.string();
#endif
}

std::filesystem::path pathFromUtf8(std::string_view s) {
#ifdef _WIN32
    return std::filesystem::path(widen(s));
#else
    return std::filesystem::path(std::string(s));
#endif
}

std::optional<std::string> getEnv(const char* name) {
#ifdef _WIN32
    const std::wstring wname = widen(name);
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0) return std::nullopt;
    std::wstring buf(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(wname.c_str(), buf.data(), n);
    buf.resize(got);
    return narrow(buf);
#else
    const char* v = std::getenv(name);
    if (!v) return std::nullopt;
    return std::string(v);
#endif
}

std::filesystem::path executableDir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    while (true) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return std::filesystem::current_path();
    return p.parent_path();
#endif
}

std::filesystem::path homeDir() {
#ifdef _WIN32
    if (auto p = getEnv("USERPROFILE")) return pathFromUtf8(*p);
    if (auto d = getEnv("HOMEDRIVE")) {
        if (auto h = getEnv("HOMEPATH")) return pathFromUtf8(*d + *h);
    }
    return std::filesystem::path(L"C:\\");
#else
    if (auto p = getEnv("HOME")) return *p;
    return "/tmp";
#endif
}

std::filesystem::path appDataDir() {
#ifdef _WIN32
    if (auto p = getEnv("APPDATA")) return pathFromUtf8(*p) / "VastGui";
    return homeDir() / "AppData" / "Roaming" / "VastGui";
#else
    if (auto p = getEnv("XDG_CONFIG_HOME"); p && !p->empty()) return std::filesystem::path(*p) / "VastGui";
    return homeDir() / ".config" / "VastGui";
#endif
}

bool openUrl(const std::string& url) {
    if (url.empty()) return false;
#ifdef _WIN32
    const auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;
#else
    pid_t pid = 0;
    const char* argv[] = {"xdg-open", url.c_str(), nullptr};
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
    std::thread([pid] {
        int status = 0;
        waitpid(pid, &status, 0);
    }).detach();
    return true;
#endif
}

bool openPath(const std::filesystem::path& p) { return openUrl(pathToUtf8(p)); }

std::optional<std::string> readTextFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeTextFileAtomic(const std::filesystem::path& p, std::string_view content, bool ownerOnly) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    auto tmp = p;
    tmp += ".tmp";
#ifdef _WIN32
    (void)ownerOnly;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!f) return false;
    }
#else
    {
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, ownerOnly ? 0600 : 0644);
        if (fd < 0) return false;
        // A stale .tmp keeps its old mode on O_TRUNC.
        if (ownerOnly && fchmod(fd, 0600) != 0) {
            ::close(fd);
            return false;
        }
        std::size_t off = 0;
        while (off < content.size()) {
            const ssize_t n = ::write(fd, content.data() + off, content.size() - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                ::close(fd);
                return false;
            }
            off += static_cast<std::size_t>(n);
        }
        if (::close(fd) != 0) return false;
    }
#endif
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return !ec;
}

CreateResult createFileExclusive(const std::filesystem::path& p, std::string_view content) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    bool ok = true;
#ifdef _WIN32
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_EXISTS ? CreateResult::Exists : CreateResult::Failed;
    }
    std::size_t off = 0;
    while (ok && off < content.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(content.size() - off, 1u << 20));
        ok = WriteFile(h, content.data() + off, chunk, &written, nullptr) && written > 0;
        off += written;
    }
    CloseHandle(h);
#else
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return errno == EEXIST ? CreateResult::Exists : CreateResult::Failed;
    std::size_t off = 0;
    while (ok && off < content.size()) {
        const ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0 && errno == EINTR) continue;
        ok = n > 0;
        if (ok) off += static_cast<std::size_t>(n);
    }
    if (::close(fd) != 0) ok = false;
#endif
    if (!ok) {
        std::filesystem::remove(p, ec);
        return CreateResult::Failed;
    }
    return CreateResult::Created;
}

bool secureRandom(void* buf, std::size_t n) {
#ifdef _WIN32
    auto* p = static_cast<unsigned char*>(buf);
    while (n > 0) {
        const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(n, 1u << 20));
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return false;
        p += chunk;
        n -= chunk;
    }
    return true;
#else
    auto* p = static_cast<unsigned char*>(buf);
    while (n > 0) {
        const ssize_t got = getrandom(p, n, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        p += got;
        n -= static_cast<std::size_t>(got);
    }
    return true;
#endif
}

#ifdef _WIN32
namespace {
std::optional<std::filesystem::path> fileDialog(const char* title, const char* defaultName, bool save) {
    std::wstring buf(4096, L'\0');
    if (defaultName) {
        const std::wstring d = widen(defaultName);
        std::copy_n(d.begin(), std::min(d.size(), buf.size() - 1), buf.begin());
    }
    const std::wstring wtitle = widen(title ? title : "");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"All files\0*.*\0Text / keys\0*.txt;*.pub;*.csv;*.json;*.sh\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return std::nullopt;
    buf.resize(wcslen(buf.c_str()));
    return std::filesystem::path(buf);
}
}  // namespace

std::optional<std::filesystem::path> openFileDialog(const char* title) { return fileDialog(title, nullptr, false); }

std::optional<std::filesystem::path> saveFileDialog(const char* title, const char* defaultName) {
    return fileDialog(title, defaultName, true);
}
#else
namespace {
std::optional<std::filesystem::path> zenity(const std::string& args) {
    const std::string cmd = "zenity --file-selection " + args + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return std::nullopt;
    char buf[PATH_MAX + 2] = {};
    std::string out;
    while (fgets(buf, sizeof(buf), f)) out += buf;
    const int rc = pclose(f);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (rc != 0 || out.empty()) return std::nullopt;
    return std::filesystem::path(out);
}
std::string shq(const std::string& s) {
    std::string o = "'";
    for (char c : s) o += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return o + "'";
}
}  // namespace

std::optional<std::filesystem::path> openFileDialog(const char* title) {
    return zenity("--title=" + shq(title ? title : "Open"));
}

std::optional<std::filesystem::path> saveFileDialog(const char* title, const char* defaultName) {
    return zenity("--save --confirm-overwrite --title=" + shq(title ? title : "Save") +
                  (defaultName ? " --filename=" + shq(defaultName) : std::string{}));
}
#endif

}  // namespace vast::platform
