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
    void openCreateApiKey(App& app);
    void drawCreateApiKey(App& app);
    void drawPermissionTable(App& app);
    void setApiPermissions(json perms);  // also refreshes the Advanced JSON text
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

    // "Create API Key" dialog. The form keeps its content when closed without creating a key.
    std::string newApiName_;
    json newApiPerms_ = json{{"api", json::object()}};  // {"api": {...}}
    std::string newApiPermsText_;                       // Advanced tab (JSON), kept in sync with newApiPerms_
    bool newApiPermsValid_ = true;
    bool newApiInitialized_ = false;
    bool openCreateApi_ = false;
    int permTab_ = 0;  // 0 Standard, 1 Advanced
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
