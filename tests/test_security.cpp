#include "Config.hpp"
#include "Log.hpp"
#include "Platform.hpp"
#include "SecretBox.hpp"
#include "backend/Commands.hpp"
#include "backend/Http.hpp"
#include "backend/RestBackend.hpp"
#include "backend/VastClient.hpp"
#include "doctest/doctest.h"

#include <filesystem>

using namespace vast;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        unsigned char rnd[8];
        REQUIRE(platform::secureRandom(rnd, sizeof(rnd)));
        std::string name = "vastgui-test-";
        for (unsigned char c : rnd) name += "0123456789abcdef"[c & 15];
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST_SUITE("Security") {
    TEST_CASE("secretbox: seal / unseal round trip, wrong key and tampering are rejected") {
        secretbox::Key key{};
        secretbox::Nonce nonce{};
        REQUIRE(platform::secureRandom(key.data(), key.size()));
        REQUIRE(platform::secureRandom(nonce.data(), nonce.size()));
        const std::string secret = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        const std::string sealed = secretbox::seal(secret, key, nonce);
        CHECK(secretbox::isSealed(sealed));
        CHECK(sealed.find(secret) == std::string::npos);
        CHECK(secretbox::unseal(sealed, key) == secret);
        CHECK(secretbox::unseal(secretbox::seal("", key, nonce), key) == "");

        secretbox::Key other = key;
        other[0] ^= 1;
        CHECK_FALSE(secretbox::unseal(sealed, other).has_value());

        std::string tampered = sealed;
        tampered[tampered.size() - 3] = tampered[tampered.size() - 3] == 'A' ? 'B' : 'A';
        CHECK_FALSE(secretbox::unseal(tampered, key).has_value());
        CHECK_FALSE(secretbox::unseal("xc20p1:not base64!", key).has_value());
        CHECK_FALSE(secretbox::unseal("plaintext", key).has_value());
    }

    TEST_CASE("secretbox: key file is created once and reused") {
        TempDir dir;
        const auto keyFile = dir.path / "sub" / "secret.key";
        std::string err;
        CHECK_FALSE(secretbox::decrypt("xc20p1:AAAA", keyFile, err).has_value());  // no key file -> no creation
        CHECK_FALSE(std::filesystem::exists(keyFile));

        const auto a = secretbox::encrypt("my-api-key-123", keyFile, err);
        REQUIRE(a.has_value());
        CHECK(std::filesystem::file_size(keyFile) == 32);
        const auto b = secretbox::encrypt("my-api-key-123", keyFile, err);
        REQUIRE(b.has_value());
        CHECK(*a != *b);  // random nonce
        CHECK(secretbox::decrypt(*a, keyFile, err) == "my-api-key-123");
        CHECK(secretbox::decrypt(*b, keyFile, err) == "my-api-key-123");

        std::filesystem::remove(keyFile);
        CHECK_FALSE(secretbox::decrypt(*a, keyFile, err).has_value());
    }

    TEST_CASE("settings never store the API key in plaintext; legacy plaintext is migrated") {
        TempDir dir;
        ConfigPaths paths;
        paths.dir = dir.path;
        paths.settingsFile = dir.path / "VastGui.json";
        paths.keyFile = dir.path / "keys" / "secret.key";
        const std::string apiKey = "abcdef0123456789abcdef0123456789";

        Settings s;
        s.apiKey = apiKey;
        std::string err;
        REQUIRE(saveSettings(paths, s, &err));
        const std::string onDisk = *platform::readTextFile(paths.settingsFile);
        CHECK(onDisk.find(apiKey) == std::string::npos);
        CHECK(onDisk.find("\"api_key_enc\"") != std::string::npos);
        CHECK(loadSettings(paths, err).apiKey == apiKey);
        CHECK(err.empty());
#ifndef _WIN32
        constexpr auto groupOrOther = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
        CHECK((std::filesystem::status(paths.settingsFile).permissions() & groupOrOther) == std::filesystem::perms::none);
        CHECK((std::filesystem::status(paths.keyFile).permissions() & groupOrOther) == std::filesystem::perms::none);
#endif

        // Legacy file written by older versions.
        REQUIRE(platform::writeTextFileAtomic(paths.settingsFile, json{{"api_key", apiKey}}.dump()));
        CHECK(loadSettings(paths, err).apiKey == apiKey);
        const std::string migrated = *platform::readTextFile(paths.settingsFile);
        CHECK(migrated.find(apiKey) == std::string::npos);
        CHECK(migrated.find("api_key_enc") != std::string::npos);

        // Undecryptable value (e.g. copied from another PC) is reported and preserved.
        std::filesystem::remove(paths.keyFile);
        const Settings lost = loadSettings(paths, err);
        CHECK(lost.apiKey.empty());
        CHECK_FALSE(lost.apiKeySealed.empty());
        CHECK(err.find("could not be decrypted") != std::string::npos);
        REQUIRE(saveSettings(paths, lost, &err));
        CHECK(platform::readTextFile(paths.settingsFile)->find(lost.apiKeySealed) != std::string::npos);
    }

    TEST_CASE("settings: favorite machines are saved and never also blacklisted") {
        Settings s;
        s.setBlacklistedMachine(5, true);
        s.setFavoriteMachine(5, true);  // favoriting removes it from the blacklist
        s.setFavoriteMachine(7, true);
        CHECK(s.favoriteMachines == std::set<std::int64_t>{5, 7});
        CHECK_FALSE(s.blacklist.machines.contains(5));
        s.setBlacklistedMachine(7, true);  // and blacklisting removes it from the favorites
        CHECK(s.favoriteMachines == std::set<std::int64_t>{5});
        CHECK(s.blacklist.machines.contains(7));

        Settings back;
        std::vector<std::string> problems;
        readSettings(json(s), back, &problems);
        CHECK(problems.empty());
        CHECK(back.favoriteMachines == s.favoriteMachines);

        // A hand-edited file listing a machine in both: the blacklist wins; invalid IDs are reported.
        const json both = {{"favorite_machines", {7, 8, "x"}}, {"blacklist", {{"machines", {7}}, {"hosts", json::array()}}}};
        readSettings(both, back, &problems);
        CHECK(back.favoriteMachines == std::set<std::int64_t>{8});
        CHECK(back.blacklist.machines.contains(7));
        CHECK(problems.size() == 1);
    }

    TEST_CASE("settings: missing, outdated or invalid values fall back to the defaults one by one") {
        TempDir dir;
        ConfigPaths paths;
        paths.dir = dir.path;
        paths.settingsFile = dir.path / "VastGui.json";
        paths.keyFile = dir.path / "keys" / "secret.key";
        std::string err;

        // No file (clean machine): defaults, no message.
        Settings fresh = loadSettings(paths, err);
        CHECK(err.empty());
        const json defaults = Settings{};
        CHECK(json(fresh) == defaults);

        // Defaults written and read back are unchanged and raise no problems.
        std::vector<std::string> problems;
        Settings back;
        readSettings(defaults, back, &problems);
        CHECK(problems.empty());
        CHECK(json(back) == defaults);

        // An old / damaged file: bad values are replaced, valid ones are kept.
        const json bad = {{"theme", "neon"},          {"server_url", "ftp://x"},     {"default_sort", "nope"},
                          {"instance_sort", 42},      {"template_sort", "size"},     {"filters", "oops"},
                          {"defects", json::array()}, {"template", {{"name", "x"}}}, {"font_size", 99},
                          {"search_requests", 500},   {"card_view", false},          {"backend", "cli"},
                          {"currency", {{"symbol", "EUR"}, {"decimals", 2}}}};
        REQUIRE(platform::writeTextFileAtomic(paths.settingsFile, bad.dump()));
        const Settings s = loadSettings(paths, err);
        CHECK(s.theme == "dark");
        CHECK(s.serverUrl == Settings{}.serverUrl);
        CHECK(s.defaultSort == "score");
        CHECK(s.instanceSort == "label");
        CHECK(s.templateSort == "name");
        CHECK(json(s.filters) == json(SearchFilters{}));
        CHECK(json(s.defects) == json(DefectThresholds{}));
        CHECK(s.currentTemplate.image == Template::defaultTemplate().image);
        CHECK(s.fontSize == doctest::Approx(32.0f));
        CHECK(s.searchRequests == 50);
        CHECK_FALSE(s.cardView);                      // valid values are kept
        CHECK(s.currency.symbol == "EUR");
        CHECK(s.currency.decimals == 2);
        CHECK(err.find("reset to their defaults") != std::string::npos);
        CHECK(err.find("theme") != std::string::npos);
        CHECK(err.find("filters") != std::string::npos);
        CHECK(err.find("backend") == std::string::npos);  // unknown old keys are simply ignored

        // Not an object at all.
        REQUIRE(platform::writeTextFileAtomic(paths.settingsFile, "[1, 2, 3]"));
        CHECK(json(loadSettings(paths, err)) == defaults);
        CHECK_FALSE(err.empty());
    }

    TEST_CASE("API key only over https (http allowed for localhost)") {
        std::string why;
        CHECK(RestBackend::serverUrlAllowed("https://console.vast.ai/api/v0/x", why));
        CHECK(RestBackend::serverUrlAllowed("HTTPS://console.vast.ai", why));
        CHECK(RestBackend::serverUrlAllowed("http://localhost:8000/api", why));
        CHECK(RestBackend::serverUrlAllowed("http://127.0.0.1/api", why));
        CHECK(RestBackend::serverUrlAllowed("http://[::1]:80/", why));
        CHECK_FALSE(RestBackend::serverUrlAllowed("http://console.vast.ai", why));
        CHECK_FALSE(RestBackend::serverUrlAllowed("http://localhost.evil.com/", why));
        CHECK_FALSE(RestBackend::serverUrlAllowed("http://localhost@evil.com/", why));
        CHECK_FALSE(RestBackend::serverUrlAllowed("ftp://console.vast.ai", why));
        CHECK_FALSE(RestBackend::serverUrlAllowed("console.vast.ai", why));
    }

    TEST_CASE("redirect resolution and origin checks") {
        CHECK(resolveRedirectUrl("https://a.com/api/v0/x?y=1", "/api/v0/x/") == "https://a.com/api/v0/x/");
        CHECK(resolveRedirectUrl("https://a.com/api/v0/x", "https://b.com/z") == "https://b.com/z");
        CHECK(resolveRedirectUrl("https://a.com:8443/api/v0/x", "y") == "https://a.com:8443/api/v0/y");
        CHECK(resolveRedirectUrl("https://a.com/p", "//c.com/q") == "https://c.com/q");
        CHECK(sameOrigin("https://A.com/x", "https://a.com:443/y"));
        CHECK_FALSE(sameOrigin("https://a.com/x", "https://b.com/x"));
        CHECK_FALSE(sameOrigin("https://a.com/x", "http://a.com/x"));
        CHECK_FALSE(sameOrigin("https://a.com/x", "https://a.com:8443/x"));
        CHECK_FALSE(sameOrigin("https://a.com/x", "https://user@b.com/x"));

        // Bodies may hold api_key / secrets: never resent to another origin.
        auto s = planRedirect("https://a.com/x", "https://a.com/x", "https://evil.com/y", 307, "POST", true);
        CHECK_FALSE(s.error.empty());
        s = planRedirect("https://a.com/x", "https://a.com/x", "/y/", 308, "POST", true);
        CHECK(s.error.empty());
        CHECK(s.url == "https://a.com/y/");
        CHECK(s.withBody);
        s = planRedirect("https://a.com/x", "https://a.com/x", "https://b.com/y", 303, "POST", true);
        CHECK(s.error.empty());
        CHECK(s.method == "GET");
        CHECK_FALSE(s.withBody);
        CHECK_FALSE(planRedirect("https://a.com/x", "https://a.com/x", "http://a.com/x", 301, "GET", false).error.empty());
        CHECK_FALSE(planRedirect("https://a.com/x", "https://a.com/x", "file:///etc/passwd", 302, "GET", false).error.empty());
        CHECK_FALSE(planRedirect("http://localhost/x", "http://localhost/x", "http://b.com/", 302, "GET", true).error.empty());
    }

    TEST_CASE("log keeps masking previous API keys") {
        Log log;
        log.addSecret("first-key-1234");
        log.addSecret("second-key-5678");
        log.info("a first-key-1234 b second-key-5678");
        const auto entries = log.snapshot();
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].text == "a **** b ****");
    }

    TEST_CASE("exclusive file creation never overwrites") {
        TempDir dir;
        const auto f = dir.path / "x" / "once.bin";
        CHECK(platform::createFileExclusive(f, "first") == platform::CreateResult::Created);
        CHECK(platform::createFileExclusive(f, "second") == platform::CreateResult::Exists);
        CHECK(platform::readTextFile(f) == "first");
    }

    TEST_CASE("logged request lines never contain request bodies") {
        Log log;
        VastClient client(log, 1);
        client.configure(BackendConfig{"abcdef0123456789abcdef", "https://console.vast.ai/"});
        CHECK(client.serverUrl() == "https://console.vast.ai");
        CHECK(client.requestLine(cmd::createEnvVar("HF_TOKEN", "hf_secret_value")) == "POST /api/v0/secrets/");
        cmd::SnapshotParams sp;
        sp.repo = "r";
        sp.password = "docker-pass-123";
        CHECK(client.requestLine(cmd::takeSnapshot(5, sp)) == "POST /api/v0/instances/take_snapshot/5/");
        CHECK(client.requestLine(cmd::copyFiles("./here", "C.1:/x")) == "Copy files (not sent)");
    }
}
