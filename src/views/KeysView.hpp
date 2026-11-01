#pragma once

#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <cstdint>
#include <set>
#include <string>

namespace vast {

class App;

class KeysView {
public:
    void draw(App& app);

private:
    void drawSsh(App& app);
    void drawApiKeys(App& app);
    void drawEnvVars(App& app);

    Resource ssh_;
    Resource apiKeys_;
    Resource envVars_;
    ui::JsonTableState sshTable_;
    ui::JsonTableState apiTable_;

    std::string newSshKey_;
    json pendingSsh_;
    bool openDeleteSsh_ = false;
    bool openAttach_ = false;
    int attachInstance_ = -1;

    std::string newApiName_;
    std::string newApiPermissions_ =
        "{\n  \"api\": {\n    \"misc\": {},\n    \"user_read\": {},\n    \"instance_read\": {},\n    \"instance_write\": {}\n  }\n}";
    std::string createdKey_;  // shown exactly once, wiped when the dialog closes
    bool openCreatedKey_ = false;
    json pendingApi_;
    bool openDeleteApi_ = false;

    std::string envName_;
    std::string envValue_;
    std::set<std::string> revealed_;
    std::string pendingEnv_;
    bool openDeleteEnv_ = false;
    std::string editEnvName_;
    std::string editEnvValue_;
    bool openEditEnv_ = false;
};

}  // namespace vast
