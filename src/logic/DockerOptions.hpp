#pragma once
// Parses/builds the docker options string used by vast templates and
// `create instance --env`, e.g. "-e TZ=UTC -p 8080:8080 --shm-size=8g".

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vast {

struct DockerOptions {
    std::vector<std::pair<std::string, std::string>> env;
    std::vector<std::string> ports;  // "8080:8080" or "8080:8080/udp"
    std::string extra;               // everything else, verbatim
    bool operator==(const DockerOptions&) const = default;
};

// Shell-like split honoring single/double quotes and backslash escapes.
[[nodiscard]] std::vector<std::string> splitShellArgs(std::string_view s);
[[nodiscard]] DockerOptions parseDockerOptions(std::string_view s);
[[nodiscard]] std::string buildDockerOptions(const DockerOptions& o);

}  // namespace vast
