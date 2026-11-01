#include "backend/Commands.hpp"
#include "backend/RestBackend.hpp"
#include "doctest/doctest.h"
#include "logic/DockerOptions.hpp"
#include "logic/Format.hpp"
#include "model/Instance.hpp"

#include <algorithm>

using namespace vast;

TEST_SUITE("Backend") {
    TEST_CASE("instance status matches the vast.ai web console") {
        const double now = 1793570000.0;
        // Field patterns taken from real instances; expected labels are what cloud.vast.ai shows for them.
        auto make = [&](bool bid, const char* intended, const char* actual, const char* cur, const char* next, double minBid,
                        double dph, double hostRun, double clientRun) {
            return json{{"id", 1},          {"is_bid", bid},          {"intended_status", intended},
                        {"actual_status", actual}, {"cur_state", cur},   {"next_state", next},
                        {"min_bid", minBid}, {"dph_total", dph},       {"host_run_time", hostRun},
                        {"client_run_time", clientRun}, {"end_date", now + 1e6}, {"duration", 1e6}}
                .get<Instance>();
        };
        // Stopped interruptible instance, host_run_time < 1000: "Outbid" in the console although bid > min_bid.
        const Instance a = make(true, "stopped", "exited", "stopped", "stopped", 0.96, 3.37, 1.0, 1.0);
        CHECK(a.consoleStatus(now).label == "outbid");
        CHECK(a.isOutbid(now));
        // Interruptible, created but never started, host_run_time large: "Scheduling...".
        CHECK(make(true, "stopped", "created", "stopped", "stopped", 3.09, 3.25, 2592000.0, 1.1).consoleStatus(now).label ==
              "scheduling");
        // Being restarted: "Starting...".
        CHECK(make(true, "running", "exited", "running", "running", 0.72, 0.76, 1.0, 1.1).consoleStatus(now).label == "starting");
        // Bid below the current minimum: outbid by price.
        CHECK(make(true, "running", "exited", "running", "stopped", 0.9, 0.8, 1.0, 1.0).isOutbid(now));
        // ...but like the console, a long host run time with next_state "stopped" ends as "Inactive".
        CHECK(make(true, "running", "exited", "running", "stopped", 0.9, 0.8, 5000.0, 1.0).consoleStatus(now).label ==
              "inactive");
        // Ran on the client for a while and was stopped by the scheduler.
        CHECK(make(true, "stopped", "exited", "stopped", "stopped", 0.5, 0.8, 5000.0, 5000.0).isOutbid(now));
        // On-demand instances are never outbid; a stopped one is "Inactive".
        const Instance od = make(false, "stopped", "exited", "stopped", "stopped", 0.5, 0.8, 5000.0, 1.0);
        CHECK_FALSE(od.isOutbid(now));
        CHECK(od.consoleStatus(now).label == "inactive");
        // Running instances keep their status; stopping is shown while next_state is stopped.
        CHECK(make(true, "running", "running", "running", "running", 2.67, 2.80, 1.0, 1.1).consoleStatus(now).label == "running");
        CHECK(make(false, "stopped", "running", "running", "stopped", 0.1, 0.5, 1.0, 1.0).consoleStatus(now).label == "stopping");
        // Offline host, expired contract, negative balance.
        CHECK(make(false, "running", "offline", "running", "running", 0.1, 0.5, 5000.0, 1.0).consoleStatus(now).label == "offline");
        Instance exp = od;
        exp.endDate = now - 10;
        CHECK(exp.consoleStatus(now).label == "unavailable");
        exp.duration = 0.5;
        CHECK(exp.consoleStatus(now).label == "expired");
        CHECK(od.consoleStatus(now, json{{"balance", -1.0}}).label == "insufficient credits");
        CHECK(od.consoleStatus(now, json{{"balance", -1.0}, {"billing_creditonly", true}}).label == "inactive");
    }

    TEST_CASE("REST URL building") {
        RestSpec s;
        s.path = "/instances/";
        s.query = {{"owner", "me"}};
        CHECK(RestBackend::buildUrl("https://console.vast.ai", s) == "https://console.vast.ai/api/v0/instances/?owner=me");
        s.path = "/api/v1/template/";
        s.query = json::object();
        CHECK(RestBackend::buildUrl("https://x", s) == "https://x/api/v1/template/");
        s.path = "/template/";
        s.query = {{"select_filters", {{"name", {{"eq", "a b"}}}}}};
        CHECK(RestBackend::buildUrl("https://x", s) ==
              "https://x/api/v0/template/?select_filters=%7B%22name%22%3A%7B%22eq%22%3A%22a+b%22%7D%7D");
        s.path = "https://run.vast.ai/get_endpoint_logs/";
        CHECK(RestBackend::buildUrl("https://x", s).rfind("https://run.vast.ai/get_endpoint_logs/?", 0) == 0);
    }

    TEST_CASE("command catalogue: instances") {
        const auto d1 = cmd::destroyInstances({7});
        CHECK(d1.rest.method == HttpMethod::Delete);
        CHECK(d1.rest.path == "/instances/7/");
        const auto d2 = cmd::destroyInstances({7, 8});
        CHECK(d2.rest.path == "/instances/");
        CHECK((*d2.rest.body)["instance_ids"] == json::array({7, 8}));
        const auto s = cmd::startInstances({5});
        CHECK((*s.rest.body)["state"] == "running");
        CHECK((*cmd::labelInstance(5, "my run").rest.body)["label"] == "my run");
    }

    TEST_CASE("command catalogue: create instance") {
        cmd::CreateInstanceParams p;
        p.offerId = 123;
        p.image = "vastai/pytorch:@vastai-automatic-tag";
        p.diskGb = 32;
        p.env = "-e TZ=UTC -p 8080:8080";
        p.jupyter = true;
        p.direct = true;
        p.bidPrice = 0.25;
        p.linkVolume = 9;
        p.mountPath = "/data";
        const ApiCall c = cmd::createInstance(p);
        const json& b = *c.rest.body;
        CHECK(c.rest.method == HttpMethod::Put);
        CHECK(c.rest.path == "/asks/123/");
        CHECK(b["runtype"] == "jupyter_direc ssh_direc ssh_proxy");
        CHECK(b["env"]["TZ"] == "UTC");
        CHECK(b["env"]["-p 8080:8080"] == "1");
        CHECK(b["price"].get<double>() == doctest::Approx(0.25));
        CHECK(b["volume_info"]["volume_id"] == 9);
        CHECK(b["volume_info"]["create_new"] == false);
    }

    TEST_CASE("responses with secrets are never logged") {
        CHECK(cmd::createEnvVar("HF_TOKEN", "hf_secret_value").sensitiveOutput);
        CHECK(cmd::showEnvVars().sensitiveOutput);
        CHECK(cmd::showApiKeys().sensitiveOutput);
        const ApiCall k = cmd::createApiKey("ci", "{\"api\": {}}");
        CHECK(k.sensitiveOutput);
        CHECK((*k.rest.body)["permissions"] == json{{"api", json::object()}});
        CHECK(cmd::showUser().rest.transform == RestTransform::StripApiKey);
    }

    TEST_CASE("ports of former CLI-only commands") {
        cmd::CloudCopyParams cc;
        cc.src = "/workspace";
        cc.dst = "/backup";
        cc.instanceId = 42;
        cc.connectionId = "1001";
        cc.transfer = "Instance To Cloud";
        cc.dryRun = true;
        const ApiCall c = cmd::cloudCopy(cc);
        CHECK(c.rest.method == HttpMethod::Post);
        CHECK(c.rest.path == "/commands/rclone/");
        CHECK(*c.rest.body == json{{"src", "/workspace"}, {"dst", "/backup"}, {"instance_id", "42"}, {"selected", "1001"},
                                   {"transfer", "Instance To Cloud"}, {"flags", json::array({"--dry-run"})}});

        const ApiCall w = cmd::updateWorkers(7);
        CHECK(w.rest.path == "https://run.vast.ai/update_workers/");
        CHECK(w.rest.injectApiKey);
        CHECK((*w.rest.body)["workergroup_id"] == 7);

        const ApiCall cp = cmd::copyFiles("C.11:/workspace/", "V.22:/data");
        REQUIRE(cp.error.empty());
        CHECK(cp.rest.method == HttpMethod::Put);
        CHECK(cp.rest.path == "/commands/copy_direct/");
        CHECK(*cp.rest.body == json{{"client_id", "me"}, {"src_id", "C.11"}, {"dst_id", "V.22"}, {"src_path", "/workspace/"},
                                    {"dst_path", "/data"}});
        CHECK((*cmd::copyFiles("11", "12:/x").rest.body)["src_id"] == 11);  // bare id -> number, path "/"
        CHECK_FALSE(cmd::copyFiles("./local", "C.1:/x").error.empty());     // this PC needs rsync
        CHECK_FALSE(cmd::copyFiles("local:data", "C.1:/x").error.empty());
        CHECK_FALSE(cmd::copyFiles("C.1:/a:b", "C.2:/x").error.empty());    // invalid location
        CHECK_FALSE(cmd::copyFiles("C.1:a//b", "C.2:/x").error.empty());    // invalid path
    }

    TEST_CASE("search offers request; an invalid raw query fails without a request") {
        SearchFilters f;
        f.gpuModels = {"RTX 5090"};
        const ApiCall c = cmd::searchOffers(f);
        CHECK(c.error.empty());
        CHECK(c.rest.path == "/bundles/");
        CHECK((*c.rest.body)["gpu_name"]["eq"] == "RTX 5090");
        f.rawQuery = "gpu_ram>=bad";
        CHECK(cmd::searchOffers(f).error.find("invalid search query") != std::string::npos);
    }

    TEST_CASE("docker options parse/build round trip") {
        const DockerOptions o = parseDockerOptions("-e A=1 -e 'B=two words' -p 8080:8080 --shm-size=8g -p 22:22/udp");
        REQUIRE(o.env.size() == 2);
        CHECK(o.env[1].first == "B");
        CHECK(o.env[1].second == "two words");
        CHECK(o.ports == std::vector<std::string>{"8080:8080", "22:22/udp"});
        CHECK(o.extra == "--shm-size=8g");
        CHECK(parseDockerOptions(buildDockerOptions(o)) == o);
    }

    TEST_CASE("formatting helpers") {
        CurrencyFormat c;
        CHECK(fmt::money(1234.5678, c) == "$1,234.568");
        c.symbol = "EUR";
        c.symbolAfter = true;
        c.decimals = 2;
        CHECK(fmt::money(-0.5, c) == "-0.50 EUR");
        CHECK(fmt::maskSecret("key=abcdef123 and abcdef123", "abcdef123") == "key=**** and ****");
        CHECK(fmt::maskedKey("0123456789abcdef") == "****cdef");
        CHECK(fmt::csvEscape("a,\"b\"") == "\"a,\"\"b\"\"\"");
        CHECK(fmt::epochFromDate("1970-01-02").value() == doctest::Approx(86400.0));
        CHECK_FALSE(fmt::epochFromDate("2026-02-30").has_value());
        CHECK(fmt::jsonArrayToCsv(json::parse(R"([{"a":1,"b":"x"},{"b":"y,z"}])")) == "a,b\r\n1,x\r\n,\"y,z\"\r\n");
    }
}
