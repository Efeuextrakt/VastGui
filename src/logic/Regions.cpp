#include "logic/Regions.hpp"

#include <array>

namespace vast {

namespace {

constexpr const char* kNA = "North America";
constexpr const char* kSA = "South America";
constexpr const char* kEU = "Europe";
constexpr const char* kAS = "Asia";
constexpr const char* kOC = "Oceania";
constexpr const char* kAF = "Africa";

constexpr std::array<const char*, 6> kContinents{kNA, kEU, kAS, kOC, kSA, kAF};

constexpr Country kCountries[] = {
    {"US", "United States", kNA}, {"CA", "Canada", kNA}, {"MX", "Mexico", kNA},
    {"CR", "Costa Rica", kNA}, {"PA", "Panama", kNA},
    {"BR", "Brazil", kSA}, {"AR", "Argentina", kSA}, {"CL", "Chile", kSA},
    {"CO", "Colombia", kSA}, {"PE", "Peru", kSA}, {"UY", "Uruguay", kSA},
    {"GB", "United Kingdom", kEU}, {"IE", "Ireland", kEU}, {"FR", "France", kEU},
    {"DE", "Germany", kEU}, {"NL", "Netherlands", kEU}, {"BE", "Belgium", kEU},
    {"LU", "Luxembourg", kEU}, {"CH", "Switzerland", kEU}, {"AT", "Austria", kEU},
    {"IT", "Italy", kEU}, {"ES", "Spain", kEU}, {"PT", "Portugal", kEU},
    {"SE", "Sweden", kEU}, {"NO", "Norway", kEU}, {"FI", "Finland", kEU},
    {"DK", "Denmark", kEU}, {"IS", "Iceland", kEU}, {"PL", "Poland", kEU},
    {"CZ", "Czechia", kEU}, {"SK", "Slovakia", kEU}, {"HU", "Hungary", kEU},
    {"RO", "Romania", kEU}, {"BG", "Bulgaria", kEU}, {"GR", "Greece", kEU},
    {"HR", "Croatia", kEU}, {"SI", "Slovenia", kEU}, {"RS", "Serbia", kEU},
    {"EE", "Estonia", kEU}, {"LV", "Latvia", kEU}, {"LT", "Lithuania", kEU},
    {"UA", "Ukraine", kEU}, {"MD", "Moldova", kEU}, {"BY", "Belarus", kEU},
    {"RU", "Russia", kEU}, {"CY", "Cyprus", kEU}, {"MT", "Malta", kEU},
    {"TR", "Turkey", kAS}, {"IL", "Israel", kAS}, {"AE", "United Arab Emirates", kAS},
    {"SA", "Saudi Arabia", kAS}, {"QA", "Qatar", kAS}, {"IN", "India", kAS},
    {"PK", "Pakistan", kAS}, {"CN", "China", kAS}, {"HK", "Hong Kong", kAS},
    {"TW", "Taiwan", kAS}, {"JP", "Japan", kAS}, {"KR", "South Korea", kAS},
    {"SG", "Singapore", kAS}, {"MY", "Malaysia", kAS}, {"TH", "Thailand", kAS},
    {"VN", "Vietnam", kAS}, {"ID", "Indonesia", kAS}, {"PH", "Philippines", kAS},
    {"KZ", "Kazakhstan", kAS}, {"GE", "Georgia", kAS}, {"AM", "Armenia", kAS},
    {"AU", "Australia", kOC}, {"NZ", "New Zealand", kOC},
    {"ZA", "South Africa", kAF}, {"EG", "Egypt", kAF}, {"NG", "Nigeria", kAF},
    {"KE", "Kenya", kAF}, {"MA", "Morocco", kAF}, {"TN", "Tunisia", kAF},
    {"GH", "Ghana", kAF},
};

}  // namespace

std::span<const Country> countries() { return kCountries; }

std::span<const char* const> continents() { return kContinents; }

bool isContinent(std::string_view region) {
    for (const char* c : kContinents) {
        if (region == c) return true;
    }
    return false;
}

std::vector<std::string> countriesInContinent(std::string_view continent) {
    std::vector<std::string> out;
    for (const auto& c : kCountries) {
        if (continent == c.continent) out.emplace_back(c.code);
    }
    return out;
}

std::string countryName(std::string_view code) {
    for (const auto& c : kCountries) {
        if (code == c.code) return c.name;
    }
    return std::string(code);
}

std::string regionLabel(std::string_view region) {
    if (region.empty()) return "Planet Earth";
    if (isContinent(region)) return std::string(region);
    return countryName(region);
}

}  // namespace vast
