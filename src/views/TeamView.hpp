#pragma once

#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <string>

namespace vast {

class App;

class TeamView {
public:
    void draw(App& app);

private:
    void drawMembers(App& app);
    void drawRoles(App& app);
    void drawTeam(App& app);

    Resource members_;
    Resource roles_;
    ui::JsonTableState membersTable_;
    ui::JsonTableState rolesTable_;
    std::string inviteEmail_;
    std::string inviteRole_ = "member";
    std::string roleName_;
    std::string rolePermissions_ =
        "{\n  \"api\": {\n    \"instance_read\": {},\n    \"instance_write\": {},\n    \"user_read\": {}\n  }\n}";
    std::string teamName_;
    json pendingRemove_;
    bool openRemoveMember_ = false;
    std::string pendingRole_;
    bool openRemoveRole_ = false;
    bool openDestroyTeam_ = false;
};

class AuditLogsView {
public:
    void draw(App& app);

private:
    Resource logs_;
    Resource ips_;
    ui::JsonTableState table_;
    ui::JsonTableState ipTable_;
};

}  // namespace vast
