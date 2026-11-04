#pragma once

#include "model/Json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vast {

struct CurrencyFormat {
    std::string symbol = "$";
    int decimals = 3;
    bool symbolAfter = false;
    bool thousandsSeparator = true;
};

void to_json(json& j, const CurrencyFormat& c);
void from_json(const json& j, CurrencyFormat& c);

namespace fmt {

[[nodiscard]] std::string money(double v, const CurrencyFormat& c, int decimalsOverride = -1);
[[nodiscard]] std::string money(std::optional<double> v, const CurrencyFormat& c, int decimalsOverride = -1);
[[nodiscard]] std::string fixed(std::optional<double> v, int decimals, std::string_view suffix = {});
[[nodiscard]] std::string percent(std::optional<double> fraction, int decimals = 1);
[[nodiscard]] std::string humanDuration(double seconds);
[[nodiscard]] std::string compactNumber(double v);  // 950, 1.2k, 12k, 3.4M
[[nodiscard]] std::string dateFromEpoch(double epoch);       // local "YYYY-MM-DD"
[[nodiscard]] std::string dateTimeFromEpoch(double epoch);   // local "YYYY-MM-DD HH:MM"
[[nodiscard]] std::optional<double> epochFromDate(std::string_view yyyyMmDd);  // UTC midnight
[[nodiscard]] double nowEpoch();

[[nodiscard]] std::string trim(std::string_view s);
[[nodiscard]] std::string toLower(std::string_view s);
[[nodiscard]] bool containsCi(std::string_view haystack, std::string_view needle);
[[nodiscard]] std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty = true);
[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view sep);

// Replaces every occurrence of `secret` (len >= 4) with "****".
[[nodiscard]] std::string maskSecret(std::string text, std::string_view secret);
[[nodiscard]] std::string maskedKey(std::string_view key);  // "****abcd"

[[nodiscard]] std::string csvEscape(std::string_view field);
[[nodiscard]] std::string toCsv(const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows);
// Union of object keys as columns, one row per element.
[[nodiscard]] std::string jsonArrayToCsv(const json& arr);

}  // namespace fmt
}  // namespace vast
