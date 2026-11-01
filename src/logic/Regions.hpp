#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vast {

struct Country {
    const char* code;
    const char* name;
    const char* continent;
};

[[nodiscard]] std::span<const Country> countries();
[[nodiscard]] std::span<const char* const> continents();
[[nodiscard]] bool isContinent(std::string_view region);
[[nodiscard]] std::vector<std::string> countriesInContinent(std::string_view continent);
[[nodiscard]] std::string countryName(std::string_view code);
[[nodiscard]] std::string regionLabel(std::string_view region);

}  // namespace vast
