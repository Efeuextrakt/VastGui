#include "doctest/doctest.h"
#include "logic/ApiKeyPermissions.hpp"

using namespace vast;
using namespace vast::apiperm;

TEST_SUITE("ApiKeyPermissions") {
    TEST_CASE("keys and levels like the vast.ai console") {
        CHECK(readKey("user") == "user_read");
        CHECK(writeKey("instance") == "instance_write");
        CHECK(readKey("admin") == "admin_read_new");
        CHECK(writeKey("misc") == "misc");

        json api = json::object();
        CHECK(levelOf(api, "user") == Level::NoAccess);
        api = withLevel(api, "user", Level::ReadWrite);
        CHECK(api == json{{"user_read", json::object()}, {"user_write", json::object()}});
        CHECK(levelOf(api, "user") == Level::ReadWrite);
        api = withLevel(api, "user", Level::Read);  // unticking Write keeps Read
        CHECK(api == json{{"user_read", json::object()}});
        api = withLevel(api, "misc", Level::ReadWrite);
        CHECK(api.contains("misc"));
        CHECK(levelOf(api, "misc") == Level::ReadWrite);
        api = withLevel(api, "user", Level::NoAccess);
        CHECK(api == json{{"misc", json::object()}});
        CHECK(hasAnyPermission(api));
        CHECK_FALSE(hasAnyPermission(json{{"constraints", {{"tfa", {{"eq", true}}}}}}));
    }

    TEST_CASE("changing a level keeps other keys and the 2FA of kept keys") {
        json api = {{"user_read", {{"constraints", {{"tfa", {{"eq", true}}}}}}}, {"misc", json::object()}};
        api = withLevel(api, "user", Level::ReadWrite);
        CHECK(tfa(api, "user_read"));
        CHECK_FALSE(tfa(api, "user_write"));
        CHECK(api.contains("misc"));
        // Old admin keys are replaced by the "_new" ones.
        api = withLevel(json{{"admin_read", json::object()}}, "admin", Level::Read);
        CHECK(api == json{{"admin_read_new", json::object()}});
    }

    TEST_CASE("what the current key may grant and the initial permissions") {
        const json rights = {{"api",
                              {{"user_read", json::object()},
                               {"user_write", json::object()},
                               {"instance_read", json::object()},
                               {"billing_read", json::object()},
                               {"admin_read", json::object()},
                               {"misc", json::object()},
                               {"overrides", json::object()}}}};
        const auto g = grantable(rights);
        CHECK(g.at("user") == Level::ReadWrite);
        CHECK(g.at("instance") == Level::Read);
        CHECK(g.at("billing") == Level::Read);
        CHECK(g.at("misc") == Level::ReadWrite);
        CHECK_FALSE(g.contains("team"));
        CHECK_FALSE(g.contains("overrides"));
        CHECK(grantable(json::object()).empty());

        const json d = defaultPermissions(rights);
        CHECK_FALSE(d["api"].contains("admin_read"));  // like the console: never copied
        CHECK(d["api"].contains("user_write"));
        CHECK(d["api"].contains("overrides"));
        CHECK(defaultPermissions(nullptr) == json{{"api", json::object()}});

        const json known = onlyKnownKeys(json{{"user_read", json::object()}, {"something_else", 1}, {"overrides", json::object()}});
        CHECK(known == json{{"user_read", json::object()}, {"overrides", json::object()}});
    }

    TEST_CASE("2FA per permission and for the whole key") {
        json api = {{"user_read", json::object()}, {"misc", json::object()}};
        api = withTfa(api, "user_read", true);
        CHECK(api["user_read"] == json{{"constraints", {{"tfa", {{"eq", true}}}}}});
        CHECK(tfa(api, "user_read"));
        CHECK_FALSE(tfa(api, "misc"));
        CHECK(withTfa(api, "billing_read", true) == api);  // not granted: unchanged
        api = withTfa(api, "user_read", false);
        CHECK(api["user_read"] == json::object());

        api = withTfa(api, "misc", true);
        api = withTfaAll(api, true);
        CHECK(tfaAll(api));
        CHECK_FALSE(tfa(api, "misc"));  // replaced by the key-wide constraint
        CHECK(api["constraints"] == json{{"tfa", {{"eq", true}}}});
        api = withTfaAll(api, false);
        CHECK_FALSE(tfaAll(api));
        CHECK_FALSE(api.contains("constraints"));
    }
}
