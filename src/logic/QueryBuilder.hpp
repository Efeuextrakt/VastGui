#pragma once
// Translates SearchFilters into the vastai query language and into the
// REST JSON body used by POST /api/v0/bundles/. No UI dependency.

#include "logic/SearchFilters.hpp"
#include "model/Json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vast {

struct QueryParseResult {
    json query = json::object();
    std::vector<std::string> warnings;
    std::string error;
    [[nodiscard]] bool ok() const { return error.empty(); }
};

class QueryBuilder {
public:
    // "num_gpus=1 gpu_name=RTX_4090 reliability>=0.9 ..." (GUI filters + raw query, raw last).
    [[nodiscard]] static std::string buildQueryString(const SearchFilters& f);
    // "-o" argument, e.g. "dph_total" or "score-".
    [[nodiscard]] static std::string orderString(const SearchFilters& f);
    // Equivalent REST body; throws std::invalid_argument on an invalid raw query.
    [[nodiscard]] static json buildRestBody(const SearchFilters& f);

    // C++ port of vastai's parse_query() for offer searches.
    [[nodiscard]] static QueryParseResult parseQuery(std::string_view query, json base = json::object());

    [[nodiscard]] static std::string gpuNameToken(std::string_view name);
    [[nodiscard]] static std::string formatNumber(double v);
    [[nodiscard]] static std::string normalizeDriverVersion(std::string_view v);
    [[nodiscard]] static std::optional<long long> numericDriverVersion(std::string_view v);
    [[nodiscard]] static std::optional<double> cudaFromImageTag(std::string_view image);
    [[nodiscard]] static std::optional<double> parseVersionNumber(std::string_view v);

    // Shell-quotes one argument (used for docker option strings).
    [[nodiscard]] static std::string quoteArgPosix(std::string_view arg);
};

}  // namespace vast
