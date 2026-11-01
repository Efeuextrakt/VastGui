#pragma once
// Encrypts small secrets (the API key in VastGui.json) with XChaCha20-Poly1305 (Monocypher). The random 32-byte
// key lives in its own owner-only file in the per-user config dir - never next to a portable VastGui.json - so
// the settings file alone (backups, a copy on a share) does not reveal the secret.
// Sealed format: "xc20p1:" + base64(nonce[24] | mac[16] | ciphertext).

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vast::secretbox {

inline constexpr std::string_view kPrefix = "xc20p1:";
using Key = std::array<std::uint8_t, 32>;
using Nonce = std::array<std::uint8_t, 24>;

[[nodiscard]] bool isSealed(std::string_view s);

[[nodiscard]] std::string seal(std::string_view plain, const Key& key, const Nonce& nonce);
// nullopt when the format is wrong or authentication fails (wrong key / tampered).
[[nodiscard]] std::optional<std::string> unseal(std::string_view sealed, const Key& key);

// Reads the key file; `create` generates it (exclusively, owner-only) when missing.
[[nodiscard]] std::optional<Key> loadKey(const std::filesystem::path& keyFile, bool create, std::string& error);

[[nodiscard]] std::optional<std::string> encrypt(std::string_view plain, const std::filesystem::path& keyFile,
                                                 std::string& error);
[[nodiscard]] std::optional<std::string> decrypt(std::string_view sealed, const std::filesystem::path& keyFile,
                                                 std::string& error);

// Overwrites the bytes of `s` before clearing it.
void wipe(std::string& s);

}  // namespace vast::secretbox
