#include "logic/Format.hpp"

#include "logic/Metrics.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <set>

namespace vast {

void to_json(json& j, const CurrencyFormat& c) {
    j = json{{"symbol", c.symbol}, {"decimals", c.decimals}, {"symbol_after", c.symbolAfter},
             {"thousands_separator", c.thousandsSeparator}};
}

void from_json(const json& j, CurrencyFormat& c) {
    using namespace jsonutil;
    c = CurrencyFormat{};
    c.symbol = str(j, "symbol", "$");
    c.decimals = std::clamp(static_cast<int>(optInt(j, "decimals").value_or(3)), 0, 6);
    c.symbolAfter = optBool(j, "symbol_after").value_or(false);
    c.thousandsSeparator = optBool(j, "thousands_separator").value_or(true);
}

namespace fmt {

std::string money(double v, const CurrencyFormat& c, int decimalsOverride) {
    const int dec = decimalsOverride >= 0 ? decimalsOverride : c.decimals;
    std::string num = std::format("{:.{}f}", std::abs(v), dec);
    if (c.thousandsSeparator) {
        const auto dot = num.find('.');
        std::string intPart = num.substr(0, dot);
        const std::string frac = dot == std::string::npos ? std::string{} : num.substr(dot);
        for (int i = static_cast<int>(intPart.size()) - 3; i > 0; i -= 3) intPart.insert(static_cast<std::size_t>(i), ",");
        num = intPart + frac;
    }
    const std::string sign = v < 0 ? "-" : "";
    return c.symbolAfter ? sign + num + " " + c.symbol : sign + c.symbol + num;
}

std::string money(std::optional<double> v, const CurrencyFormat& c, int decimalsOverride) {
    if (!v || !std::isfinite(*v)) return kMissing;
    return money(*v, c, decimalsOverride);
}

std::string fixed(std::optional<double> v, int decimals, std::string_view suffix) {
    if (!v || !std::isfinite(*v)) return kMissing;
    return std::format("{:.{}f}{}", *v, decimals, suffix);
}

std::string percent(std::optional<double> fraction, int decimals) {
    if (!fraction || !std::isfinite(*fraction)) return kMissing;
    return std::format("{:.{}f}%", *fraction * 100.0, decimals);
}

std::string humanDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return kMissing;
    if (seconds < 120) return std::format("{:.0f} sec", seconds);
    if (seconds < 7200) return std::format("{:.0f} min", seconds / 60.0);
    if (seconds < 2 * 86400) return std::format("{:.1f} hrs", seconds / 3600.0);
    if (seconds < 60 * 86400) return std::format("{:.1f} days", seconds / 86400.0);
    if (seconds < 730 * 86400) return std::format("{:.1f} mon", seconds / (30.0 * 86400.0));
    return std::format("{:.1f} yrs", seconds / (365.0 * 86400.0));
}

std::string compactNumber(double v) {
    if (!std::isfinite(v)) return kMissing;
    const double a = std::abs(v);
    auto scaled = [&](double div, const char* suffix) {
        const double x = v / div;
        return std::abs(x) < 9.95 ? std::format("{:.1f}{}", x, suffix) : std::format("{:.0f}{}", x, suffix);
    };
    if (a >= 999500.0) return scaled(1e6, "M");
    if (a >= 999.5) return scaled(1e3, "k");
    return std::format("{:.0f}", v);
}

namespace {

std::tm localTm(double epoch) {
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace

std::string dateFromEpoch(double epoch) {
    if (!std::isfinite(epoch) || epoch <= 0) return kMissing;
    const std::tm tm = localTm(epoch);
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

std::string dateTimeFromEpoch(double epoch) {
    if (!std::isfinite(epoch) || epoch <= 0) return kMissing;
    const std::tm tm = localTm(epoch);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min);
}

std::optional<double> epochFromDate(std::string_view s) {
    const std::string t = trim(s);
    int y = 0, m = 0, d = 0;
    if (std::sscanf(t.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return std::nullopt;
    using namespace std::chrono;
    const year_month_day ymd{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) return std::nullopt;
    const sys_days days{ymd};
    return static_cast<double>(duration_cast<seconds>(days.time_since_epoch()).count());
}

double nowEpoch() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool containsCi(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != haystack.end();
}

std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto pos = s.find(sep, start);
        std::string part = trim(s.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
        if (!part.empty() || !skipEmpty) out.push_back(std::move(part));
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string maskSecret(std::string text, std::string_view secret) {
    if (secret.size() < 4) return text;
    std::size_t pos = 0;
    while ((pos = text.find(secret, pos)) != std::string::npos) {
        text.replace(pos, secret.size(), "****");
        pos += 4;
    }
    return text;
}

std::string maskedKey(std::string_view key) {
    if (key.empty()) return "(none)";
    if (key.size() <= 8) return "****";
    return "****" + std::string(key.substr(key.size() - 4));
}

std::string csvEscape(std::string_view field) {
    const bool needs = field.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needs) return std::string(field);
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string toCsv(const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows) {
    std::string out;
    auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i) out += ',';
            out += csvEscape(cells[i]);
        }
        out += "\r\n";
    };
    line(header);
    for (const auto& r : rows) line(r);
    return out;
}

std::string jsonArrayToCsv(const json& arr) {
    std::vector<std::string> cols;
    std::set<std::string> seen;
    if (!arr.is_array()) return {};
    for (const auto& row : arr) {
        if (!row.is_object()) continue;
        for (const auto& [k, v] : row.items()) {
            if (seen.insert(k).second) cols.push_back(k);
        }
    }
    std::vector<std::vector<std::string>> rows;
    for (const auto& row : arr) {
        std::vector<std::string> cells;
        for (const auto& c : cols) {
            const json* v = row.is_object() ? jsonutil::find(row, c) : nullptr;
            cells.push_back(v ? jsonutil::scalarToString(*v) : std::string{});
        }
        rows.push_back(std::move(cells));
    }
    return toCsv(cols, rows);
}

}  // namespace fmt
}  // namespace vast
