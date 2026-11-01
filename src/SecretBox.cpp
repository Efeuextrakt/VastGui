#include "SecretBox.hpp"

#include "Platform.hpp"
#include "monocypher/monocypher.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace vast::secretbox {

namespace {

constexpr std::string_view kAssociatedData = "VastGui secretbox v1";
constexpr std::size_t kMacSize = 16;

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string sbBase64Encode(const std::uint8_t* data, std::size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t v = (std::uint32_t{data[i]} << 16) | (i + 1 < n ? std::uint32_t{data[i + 1]} << 8 : 0u) |
                                (i + 2 < n ? std::uint32_t{data[i + 2]} : 0u);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < n ? kB64[v & 63] : '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> sbBase64Decode(std::string_view s) {
    if (s.size() % 4 != 0) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> out;
    out.reserve(s.size() / 4 * 3);
    for (std::size_t i = 0; i < s.size(); i += 4) {
        const bool last = i + 4 == s.size();
        const int pad = (last && s[i + 3] == '=') + (last && s[i + 2] == '=');
        const int a = val(s[i]), b = val(s[i + 1]);
        const int c = pad >= 2 ? 0 : val(s[i + 2]);
        const int d = pad >= 1 ? 0 : val(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
        const std::uint32_t v = (std::uint32_t(a) << 18) | (std::uint32_t(b) << 12) | (std::uint32_t(c) << 6) | std::uint32_t(d);
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        if (pad < 2) out.push_back(static_cast<std::uint8_t>(v >> 8));
        if (pad < 1) out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

const std::uint8_t* adBytes() { return reinterpret_cast<const std::uint8_t*>(kAssociatedData.data()); }

}  // namespace

bool isSealed(std::string_view s) { return s.starts_with(kPrefix); }

std::string seal(std::string_view plain, const Key& key, const Nonce& nonce) {
    std::vector<std::uint8_t> buf(nonce.size() + kMacSize + plain.size());
    std::memcpy(buf.data(), nonce.data(), nonce.size());
    crypto_aead_lock(buf.data() + nonce.size() + kMacSize, buf.data() + nonce.size(), key.data(), nonce.data(), adBytes(),
                     kAssociatedData.size(), reinterpret_cast<const std::uint8_t*>(plain.data()), plain.size());
    return std::string(kPrefix) + sbBase64Encode(buf.data(), buf.size());
}

std::optional<std::string> unseal(std::string_view sealed, const Key& key) {
    if (!isSealed(sealed)) return std::nullopt;
    const auto raw = sbBase64Decode(sealed.substr(kPrefix.size()));
    if (!raw || raw->size() < Nonce{}.size() + kMacSize) return std::nullopt;
    const std::size_t textSize = raw->size() - Nonce{}.size() - kMacSize;
    std::string plain(textSize, '\0');
    const std::uint8_t* nonce = raw->data();
    const std::uint8_t* mac = nonce + Nonce{}.size();
    if (crypto_aead_unlock(reinterpret_cast<std::uint8_t*>(plain.data()), mac, key.data(), nonce, adBytes(),
                           kAssociatedData.size(), mac + kMacSize, textSize) != 0) {
        return std::nullopt;
    }
    return plain;
}

std::optional<Key> loadKey(const std::filesystem::path& keyFile, bool create, std::string& error) {
    const std::string where = platform::pathToUtf8(keyFile);
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        if (std::filesystem::exists(keyFile, ec)) {
            auto data = platform::readTextFile(keyFile);
            if (data && data->size() == Key{}.size()) {
                Key k;
                std::memcpy(k.data(), data->data(), k.size());
                wipe(*data);
                return k;
            }
            if (data) wipe(*data);
            // Another instance may be creating it right now.
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        if (!create) {
            error = "encryption key file " + where + " not found";
            return std::nullopt;
        }
        Key k;
        if (!platform::secureRandom(k.data(), k.size())) {
            error = "no secure random source available";
            return std::nullopt;
        }
        const auto r = platform::createFileExclusive(
            keyFile, std::string_view(reinterpret_cast<const char*>(k.data()), k.size()));
        if (r == platform::CreateResult::Created) return k;
        crypto_wipe(k.data(), k.size());
        if (r == platform::CreateResult::Failed) {
            error = "could not create encryption key file " + where;
            return std::nullopt;
        }
    }
    error = "encryption key file " + where + " is unreadable or damaged";
    return std::nullopt;
}

std::optional<std::string> encrypt(std::string_view plain, const std::filesystem::path& keyFile, std::string& error) {
    auto key = loadKey(keyFile, true, error);
    if (!key) return std::nullopt;
    Nonce nonce;
    if (!platform::secureRandom(nonce.data(), nonce.size())) {
        crypto_wipe(key->data(), key->size());
        error = "no secure random source available";
        return std::nullopt;
    }
    std::string out = seal(plain, *key, nonce);
    crypto_wipe(key->data(), key->size());
    return out;
}

std::optional<std::string> decrypt(std::string_view sealed, const std::filesystem::path& keyFile, std::string& error) {
    if (!isSealed(sealed)) {
        error = "unknown encrypted value format";
        return std::nullopt;
    }
    auto key = loadKey(keyFile, false, error);
    if (!key) return std::nullopt;
    auto plain = unseal(sealed, *key);
    crypto_wipe(key->data(), key->size());
    if (!plain) error = "it was encrypted with a different key file (" + platform::pathToUtf8(keyFile) + ")";
    return plain;
}

void wipe(std::string& s) {
    if (!s.empty()) crypto_wipe(s.data(), s.size());
    s.clear();
}

}  // namespace vast::secretbox
