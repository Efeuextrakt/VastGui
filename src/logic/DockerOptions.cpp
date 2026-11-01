#include "logic/DockerOptions.hpp"

#include "logic/QueryBuilder.hpp"

namespace vast {

std::vector<std::string> splitShellArgs(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    bool inToken = false;
    char quote = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else if (c == '\\' && quote == '"' && i + 1 < s.size() && (s[i + 1] == '"' || s[i + 1] == '\\')) {
                cur += s[++i];
            } else {
                cur += c;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            inToken = true;
        } else if (c == '\\' && i + 1 < s.size()) {
            cur += s[++i];
            inToken = true;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inToken) {
                out.push_back(std::move(cur));
                cur.clear();
                inToken = false;
            }
        } else {
            cur += c;
            inToken = true;
        }
    }
    if (inToken) out.push_back(std::move(cur));
    return out;
}

DockerOptions parseDockerOptions(std::string_view s) {
    DockerOptions o;
    const auto args = splitShellArgs(s);
    std::vector<std::string> extra;
    auto addEnv = [&](const std::string& kv) {
        const auto eq = kv.find('=');
        if (eq == std::string::npos) {
            o.env.emplace_back(kv, std::string{});
        } else {
            o.env.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if ((a == "-e" || a == "--env") && i + 1 < args.size()) {
            addEnv(args[++i]);
        } else if (a.starts_with("--env=")) {
            addEnv(a.substr(6));
        } else if ((a == "-p" || a == "--publish") && i + 1 < args.size()) {
            o.ports.push_back(args[++i]);
        } else if (a.starts_with("--publish=")) {
            o.ports.push_back(a.substr(10));
        } else {
            extra.push_back(a);
        }
    }
    for (const auto& e : extra) {
        if (!o.extra.empty()) o.extra += ' ';
        o.extra += QueryBuilder::quoteArgPosix(e);
    }
    return o;
}

std::string buildDockerOptions(const DockerOptions& o) {
    std::string out;
    auto append = [&](std::string_view part) {
        if (part.empty()) return;
        if (!out.empty()) out += ' ';
        out += part;
    };
    for (const auto& [k, v] : o.env) {
        if (k.empty()) continue;
        append("-e " + QueryBuilder::quoteArgPosix(k + "=" + v));
    }
    for (const auto& p : o.ports) {
        if (!p.empty()) append("-p " + QueryBuilder::quoteArgPosix(p));
    }
    append(o.extra);
    return out;
}

}  // namespace vast
