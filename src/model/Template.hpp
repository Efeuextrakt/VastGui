#pragma once

#include "model/Json.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace vast {

// A vast.ai template (`vastai search templates --raw`).
struct Template {
    std::optional<std::int64_t> id;
    std::string hashId;
    std::string name;
    std::string image;
    std::string tag;
    std::string env;          // docker options: "-e K=V -p 8080:8080"
    std::string onstart;
    std::string runtype;      // "ssh", "jupyter", "args"
    std::string readme;
    std::string desc;
    std::string href;
    std::string repo;
    std::string searchParams;
    bool sshDirect = false;
    bool jupyterDirect = false;
    bool useSsh = true;
    bool isPrivate = true;
    bool recommended = false;
    std::optional<double> recommendedDiskGb;
    std::optional<std::int64_t> creatorId;
    std::optional<double> createdAt;
    std::optional<double> countCreated;

    [[nodiscard]] std::string imageWithTag() const {
        if (tag.empty() || image.find(':') != std::string::npos) return image;
        return image + ":" + tag;
    }
    [[nodiscard]] bool usesJupyter() const { return runtype.find("jupyter") != std::string::npos; }
    [[nodiscard]] bool usesSsh() const { return useSsh || runtype.find("ssh") != std::string::npos; }
    [[nodiscard]] bool isDirect() const { return sshDirect || jupyterDirect; }

    static Template defaultTemplate() {
        Template t;
        t.name = "PyTorch (Vast)";
        t.image = "vastai/pytorch";
        t.tag = "@vastai-automatic-tag";
        t.runtype = "jupyter_direc ssh_direc ssh_proxy";
        t.sshDirect = true;
        t.jupyterDirect = true;
        t.useSsh = true;
        t.env = "-p 1111:1111 -p 8080:8080 -e OPEN_BUTTON_PORT=1111";
        t.recommendedDiskGb = 32;
        return t;
    }
};

inline void from_json(const json& j, Template& t) {
    using namespace jsonutil;
    t.id = optInt(j, "id");
    t.hashId = str(j, "hash_id");
    t.name = str(j, "name");
    t.image = str(j, "image");
    t.tag = str(j, "tag");
    if (t.tag.empty()) t.tag = str(j, "default_tag");
    if (const json* e = find(j, "env")) t.env = e->is_string() ? e->get<std::string>() : e->dump();
    t.onstart = str(j, "onstart");
    t.runtype = str(j, "runtype");
    t.readme = str(j, "readme");
    t.desc = str(j, "desc");
    t.href = str(j, "href");
    t.repo = str(j, "docker_login_repo");
    if (const json* e = find(j, "extra_filters")) t.searchParams = e->is_string() ? e->get<std::string>() : e->dump();
    t.sshDirect = optBool(j, "ssh_direct").value_or(false);
    t.jupyterDirect = optBool(j, "jup_direct").value_or(false);
    t.useSsh = optBool(j, "use_ssh").value_or(t.runtype.find("ssh") != std::string::npos);
    t.isPrivate = optBool(j, "private").value_or(false);
    t.recommended = optBool(j, "recommended").value_or(false);
    t.recommendedDiskGb = optDouble(j, "recommended_disk_space");
    t.creatorId = optInt(j, "creator_id");
    t.createdAt = optDouble(j, "created_at");
    t.countCreated = optDouble(j, "count_created");
}

inline void to_json(json& j, const Template& t) {
    j = json{{"hash_id", t.hashId},   {"name", t.name},         {"image", t.image},
             {"tag", t.tag},          {"env", t.env},           {"onstart", t.onstart},
             {"runtype", t.runtype},  {"readme", t.readme},     {"desc", t.desc},
             {"href", t.href},        {"docker_login_repo", t.repo},
             {"ssh_direct", t.sshDirect}, {"jup_direct", t.jupyterDirect}, {"use_ssh", t.useSsh},
             {"private", t.isPrivate}, {"recommended", t.recommended}};
    if (t.id) j["id"] = *t.id;
    if (t.recommendedDiskGb) j["recommended_disk_space"] = *t.recommendedDiskGb;
    if (!t.searchParams.empty()) j["extra_filters"] = t.searchParams;
}

}  // namespace vast
