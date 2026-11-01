#pragma once
// Tolerant accessors for vast.ai JSON payloads: fields may be missing, null,
// numbers encoded as strings, or booleans encoded as 0/1.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace vast {

using json = nlohmann::json;

namespace jsonutil {

inline const json* find(const json& j, std::string_view key) {
    if (!j.is_object()) return nullptr;
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return nullptr;
    return &*it;
}

inline std::optional<double> parseDouble(std::string_view s) {
    if (s.empty()) return std::nullopt;
    const std::string tmp(s);
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str()) return std::nullopt;
    return v;
}

inline std::optional<double> optDouble(const json& j, std::string_view key) {
    const json* v = find(j, key);
    if (!v) return std::nullopt;
    if (v->is_number()) return v->get<double>();
    if (v->is_boolean()) return v->get<bool>() ? 1.0 : 0.0;
    if (v->is_string()) return parseDouble(v->get_ref<const std::string&>());
    return std::nullopt;
}

inline std::optional<std::int64_t> optInt(const json& j, std::string_view key) {
    const json* v = find(j, key);
    if (!v) return std::nullopt;
    if (v->is_number_integer()) return v->get<std::int64_t>();
    if (v->is_number()) return static_cast<std::int64_t>(v->get<double>());
    if (v->is_string()) {
        if (auto d = parseDouble(v->get_ref<const std::string&>())) return static_cast<std::int64_t>(*d);
    }
    return std::nullopt;
}

inline std::optional<std::string> optString(const json& j, std::string_view key) {
    const json* v = find(j, key);
    if (!v) return std::nullopt;
    if (v->is_string()) return v->get<std::string>();
    if (v->is_boolean()) return v->get<bool>() ? std::string("true") : std::string("false");
    if (v->is_number_integer()) return std::to_string(v->get<std::int64_t>());
    if (v->is_number()) return v->dump();
    return std::nullopt;
}

inline std::optional<bool> optBool(const json& j, std::string_view key) {
    const json* v = find(j, key);
    if (!v) return std::nullopt;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number()) return v->get<double>() != 0.0;
    if (v->is_string()) {
        const auto& s = v->get_ref<const std::string&>();
        return s == "true" || s == "True" || s == "1";
    }
    return std::nullopt;
}

inline std::string str(const json& j, std::string_view key, std::string_view def = {}) {
    auto v = optString(j, key);
    return v ? *v : std::string(def);
}

inline double num(const json& j, std::string_view key, double def = 0.0) {
    return optDouble(j, key).value_or(def);
}

// Human readable scalar rendering used by generic JSON tables.
inline std::string scalarToString(const json& v) {
    if (v.is_null()) return {};
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_float()) {
        const double d = v.get<double>();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", d);
        return buf;
    }
    if (v.is_number()) return v.dump();
    return v.dump();
}

// Accepts either a bare array or an object wrapping the array under one of
// the usual keys ("instances", "offers", ...). Returns an empty array otherwise.
inline json extractArray(const json& j, std::initializer_list<std::string_view> keys) {
    if (j.is_array()) return j;
    if (j.is_object()) {
        for (auto k : keys) {
            if (const json* v = find(j, k); v && v->is_array()) return *v;
        }
        for (const auto& [k, v] : j.items()) {
            if (v.is_array()) return v;
        }
    }
    return json::array();
}

}  // namespace jsonutil
}  // namespace vast
