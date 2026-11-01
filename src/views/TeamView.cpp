#include "views/TeamView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "logic/Format.hpp"
#include "widgets/Spinner.hpp"

#include <format>

namespace vast {

namespace {

std::string formatTimeCell(const std::string& col, const json& v) {
    if (v.is_number() && (col.find("time") != std::string::npos || col.find("date") != std::string::npos ||
                          col.find("_at") != std::string::npos || col == "when")) {
        const double t = v.get<double>();
        if (t > 1e9 && t < 1e11) return fmt::dateTimeFromEpoch(t);
    }
    return v.is_null() ? std::string{} : jsonutil::scalarToString(v);
}

}  // namespace

void TeamView::draw(App& app) {
    ui::PageHeader(ICON_FA_USERS, "Members / Team", "team members, invitations and roles");
    if (ImGui::BeginTabBar("##teamtabs")) {
        if (ImGui::BeginTabItem(ICON_FA_USERS " Members")) {
            drawMembers(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_SHIELD_HALVED " Roles")) {
            drawRoles(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_BUILDING " Team")) {
            drawTeam(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void TeamView::drawMembers(App& app) {
    members_.autoFetch(app, cmd::showMembers());
    roles_.autoFetch(app, cmd::showTeamRoles());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", members_.loading)) members_.fetch(app, cmd::showMembers());
    ui::InlineError(members_.error);

    ui::SectionTitle(ICON_FA_USER_PLUS, "Invite member");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    ImGui::InputTextWithHint("##email", "email@example.com", &inviteEmail_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::BeginCombo("##role", inviteRole_.c_str())) {
        for (const auto& r : jsonutil::extractArray(roles_.data, {"roles", "results"})) {
            const std::string name = jsonutil::str(r, "name");
            if (!name.empty() && ImGui::Selectable(name.c_str(), name == inviteRole_)) inviteRole_ = name;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(inviteEmail_.find('@') == std::string::npos || inviteRole_.empty());
    if (ImGui::Button(ICON_FA_PAPER_PLANE " Invite")) {
        app.run(cmd::inviteMember(fmt::trim(inviteEmail_), inviteRole_),
                [this, &app](const ApiResult&) { members_.fetch(app, cmd::showMembers()); }, "Invitation sent");
        inviteEmail_.clear();
    }
    ImGui::EndDisabled();

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "email", "username", "role", "roles", "fullname", "invited", "created_at"};
    opts.formatCell = formatTimeCell;
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_USER_MINUS "  Remove from team...")) {
            pendingRemove_ = row;
            openRemoveMember_ = true;
        }
    };
    ui::JsonTable("##members", jsonutil::extractArray(members_.data, {"members", "results"}), membersTable_, opts);

    if (openRemoveMember_) {
        ImGui::OpenPopup("Remove member?###rmmember");
        openRemoveMember_ = false;
    }
    const auto id = jsonutil::optInt(pendingRemove_, "id").value_or(0);
    if (ui::ConfirmModal("Remove member?###rmmember",
                         std::format("Remove {} (id {}) from the team?", jsonutil::str(pendingRemove_, "email"), id),
                         "Remove")) {
        app.run(cmd::removeMember(id), [this, &app](const ApiResult&) { members_.fetch(app, cmd::showMembers()); },
                "Member removed");
    }
}

void TeamView::drawRoles(App& app) {
    roles_.autoFetch(app, cmd::showTeamRoles());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", roles_.loading)) roles_.fetch(app, cmd::showTeamRoles());
    ui::InlineError(roles_.error);

    const float half = ImGui::GetContentRegionAvail().x * 0.55f;
    ImGui::BeginChild("##rolelist", ImVec2(half, 0));
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "name", "permissions", "identifier"};
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy permissions into editor")) {
            if (const json* p = jsonutil::find(row, "permissions")) rolePermissions_ = p->dump(2);
        }
        if (ImGui::MenuItem(ICON_FA_TRASH "  Remove role...")) {
            pendingRole_ = jsonutil::str(row, "name");
            openRemoveRole_ = true;
        }
    };
    ui::JsonTable("##roles", jsonutil::extractArray(roles_.data, {"roles", "results"}), rolesTable_, opts);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##roleform", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ui::SectionTitle(ICON_FA_PLUS, "Create role");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##rolename", "role name", &roleName_);
    ui::TextDim("Permissions (JSON):");
    if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
    ImGui::InputTextMultiline("##perm", &rolePermissions_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 12));
    if (ui::monoFont()) ImGui::PopFont();
    const bool valid = !json::parse(rolePermissions_, nullptr, false).is_discarded();
    if (!valid) ui::TextWithColor(ui::palette().danger, "Invalid JSON");
    ImGui::BeginDisabled(!valid || fmt::trim(roleName_).empty());
    if (ImGui::Button(ICON_FA_PLUS " Create role")) {
        app.run(cmd::createTeamRole(fmt::trim(roleName_), rolePermissions_),
                [this, &app](const ApiResult&) { roles_.fetch(app, cmd::showTeamRoles()); }, "Role created");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Permission docs")) {
        platform::openUrl("https://docs.vast.ai/cli/roles-and-permissions");
    }
    ImGui::EndChild();

    if (openRemoveRole_) {
        ImGui::OpenPopup("Remove role?###rmrole");
        openRemoveRole_ = false;
    }
    if (ui::ConfirmModal("Remove role?###rmrole", "Remove team role \"" + pendingRole_ + "\"?", "Remove")) {
        app.run(cmd::removeTeamRole(pendingRole_), [this, &app](const ApiResult&) { roles_.fetch(app, cmd::showTeamRoles()); },
                "Role removed");
    }
}

void TeamView::drawTeam(App& app) {
    ui::TextDim("Create a team to share billing and instances. Your current API key stays on your personal account.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    ImGui::InputTextWithHint("##teamname", "team name", &teamName_);
    ImGui::SameLine();
    ImGui::BeginDisabled(fmt::trim(teamName_).empty());
    if (ImGui::Button(ICON_FA_PLUS " Create team")) app.run(cmd::createTeam(fmt::trim(teamName_)), {}, "Team created");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();
    ui::TextWithColor(ui::palette().danger, ICON_FA_TRIANGLE_EXCLAMATION "  Danger zone");
    if (ui::DangerButton(ICON_FA_TRASH " Destroy team...")) openDestroyTeam_ = true;
    if (openDestroyTeam_) {
        ImGui::OpenPopup("Destroy team?###destroyteam");
        openDestroyTeam_ = false;
    }
    if (ui::ConfirmModal("Destroy team?###destroyteam",
                         "Destroy the team you are currently acting as? Members lose access. This cannot be undone.",
                         "Destroy")) {
        app.run(cmd::destroyTeam(), {}, "Team destroyed");
    }
}

void AuditLogsView::draw(App& app) {
    ui::PageHeader(ICON_FA_CLIPBOARD_LIST, "Audit Logs", "account activity");
    logs_.autoFetch(app, cmd::showAuditLogs());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", logs_.loading)) logs_.fetch(app, cmd::showAuditLogs());
    ImGui::SameLine();
    const json rows = jsonutil::extractArray(logs_.data, {"audit_logs", "results", "logs"});
    if (ui::SecondaryButton(ICON_FA_FILE_CSV " Export CSV")) {
        if (const auto path = platform::saveFileDialog("Export audit logs", "vast_audit_logs.csv")) {
            if (platform::writeTextFileAtomic(*path, fmt::jsonArrayToCsv(rows))) app.toasts().success("Exported", platform::pathToUtf8(*path));
        }
    }
    ui::InlineError(logs_.error);

    if (ImGui::BeginTabBar("##audittabs")) {
        if (ImGui::BeginTabItem(ICON_FA_CLIPBOARD_LIST " Audit log")) {
            ui::JsonTableOptions opts;
            opts.preferredColumns = {"created_at", "timestamp", "api_call", "action", "ip_address", "user_id", "args", "message"};
            opts.formatCell = formatTimeCell;
            ui::JsonTable("##audit", rows, table_, opts);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_NETWORK_WIRED " IP addresses")) {
            ips_.autoFetch(app, cmd::showIpAddrs());
            ui::JsonTableOptions opts;
            opts.formatCell = formatTimeCell;
            ui::JsonTable("##ips", jsonutil::extractArray(ips_.data, {"results", "ipaddrs"}), ipTable_, opts);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace vast
