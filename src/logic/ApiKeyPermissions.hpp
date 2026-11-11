#pragma once
// API key permissions as the vast.ai console builds them ("Create API Key" dialog, Standard tab). No UI dependency.
//
// The permissions JSON is {"api": {...}}. Each category maps to keys with an empty object as value:
//   "<cat>_read", "<cat>_write" (admin: "admin_read_new" / "admin_write_new"), and "misc" for Miscellaneous.
// 2FA: a key's object gets {"constraints": {"tfa": {"eq": true}}}; "Require 2FA for this key" puts the same
// constraint on "api" itself and removes the per-key ones.

#include "model/Json.hpp"

#include <map>
#include <span>
#include <string>
#include <string_view>

namespace vast::apiperm {

enum class Level { NoAccess, Read, ReadWrite };

struct Category {
    const char* key;      // "user", "admin", "machine", "instance", "billing", "team", "misc"
    const char* label;    // "User", ...
    const char* tooltip;  // the console's description
};

// In the console's order: User, Admin, Machines, Instances, Billing/Earning, Team, Miscellaneous.
[[nodiscard]] std::span<const Category> categories();

// JSON key of a category's read / write permission ("user_read", "admin_write_new", "misc").
[[nodiscard]] std::string readKey(std::string_view cat);
[[nodiscard]] std::string writeKey(std::string_view cat);

// Level of a category in an "api" object.
[[nodiscard]] Level levelOf(const json& api, std::string_view cat);
// Sets a category's level; other keys are kept, and kept keys of this category keep their constraints (2FA).
[[nodiscard]] json withLevel(const json& api, std::string_view cat, Level level);

// What the current key may grant, per category ("rights" of the `show user` response, i.e. {"api": {...}}).
// Categories the key does not have are missing from the map.
[[nodiscard]] std::map<std::string, Level> grantable(const json& rights);
// Initial permissions of a new key: everything the current key has, except admin ({"api": {...}}).
[[nodiscard]] json defaultPermissions(const json& rights);
// Keeps only the keys the Standard table knows (plus "overrides" / "constraints").
[[nodiscard]] json onlyKnownKeys(const json& api);
// True when at least one permission is granted (keys other than "overrides" / "constraints").
[[nodiscard]] bool hasAnyPermission(const json& api);

// 2FA per permission key and for the whole key.
[[nodiscard]] bool tfa(const json& api, std::string_view key);
[[nodiscard]] json withTfa(const json& api, std::string_view key, bool on);
[[nodiscard]] bool tfaAll(const json& api);
[[nodiscard]] json withTfaAll(const json& api, bool on);

}  // namespace vast::apiperm
