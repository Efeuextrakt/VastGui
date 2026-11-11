#include "views/KeysView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "SecretBox.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "logic/ApiKeyPermissions.hpp"
#include "logic/Format.hpp"
#include "widgets/Spinner.hpp"

#include <algorithm>
#include <format>

namespace vast {

namespace {

std::string maskKeyCell(const std::string& col, const json& v) {
    std::string s = v.is_null() ? std::string{} : jsonutil::scalarToString(v);
    const bool secretCol = col == "key" || col == "api_key" || col == "token" || col == "secret";
    if (secretCol && s.size() > 8) return fmt::maskedKey(s);
    if (v.is_number() && (col.find("_at") != std::string::npos || col.find("date") != std::string::npos)) {
        const double t = v.get<double>();
        if (t > 1e9 && t < 1e11) return fmt::dateTimeFromEpoch(t);
    }
    return s;
}

json envRows(const json& data) {
    json rows = json::array();
    if (data.is_object()) {
        const json* inner = jsonutil::find(data, "secrets");
        const json& src = inner && inner->is_object() ? *inner : data;
        for (const auto& [k, v] : src.items()) rows.push_back({{"name", k}, {"value", jsonutil::scalarToString(v)}});
    } else if (data.is_array()) {
        for (const auto& r : data) {
            rows.push_back({{"name", jsonutil::str(r, "key", jsonutil::str(r, "name"))}, {"value", jsonutil::str(r, "value")}});
        }
    }
    return rows;
}

std::string findSecret(const json& j) {
    if (j.is_string()) return j.get<std::string>();
    if (!j.is_object()) return {};
    for (const char* k : {"key", "api_key", "token", "new_key"}) {
        if (const json* v = jsonutil::find(j, k); v && v->is_string()) return v->get<std::string>();
    }
    for (const auto& [k, v] : j.items()) {
        if (v.is_object()) {
            if (auto s = findSecret(v); !s.empty()) return s;
        }
    }
    return {};
}

}  // namespace

void KeysView::draw(App& app) {
    ui::PageHeader(ICON_FA_KEY, "Keys", "SSH keys, API keys and environment variables");
    if (ImGui::BeginTabBar("##keystabs")) {
        if (ImGui::BeginTabItem(ICON_FA_TERMINAL " SSH keys")) {
            drawSsh(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_KEY " API keys")) {
            drawApiKeys(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_LIST " Environment variables")) {
            drawEnvVars(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void KeysView::drawSsh(App& app) {
    ssh_.autoFetch(app, cmd::showSshKeys());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", ssh_.loading)) ssh_.fetch(app, cmd::showSshKeys());
    ui::InlineError(ssh_.error);

    ui::SectionTitle(ICON_FA_PLUS, "Add SSH public key");
    ImGui::InputTextMultiline("##newssh", &newSshKey_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3),
                              ImGuiInputTextFlags_WordWrap);
    if (ui::SecondaryButton(ICON_FA_FOLDER_OPEN " Load from file...")) {
        if (const auto p = platform::openFileDialog("Select SSH public key (.pub)")) {
            if (auto content = platform::readTextFile(*p)) {
                if (content->find("PRIVATE KEY") != std::string::npos) {
                    app.toasts().error("That is a private key", "Select the .pub file instead.");
                } else {
                    newSshKey_ = fmt::trim(*content);
                }
            }
        }
    }
    ImGui::SameLine();
    if (ui::SecondaryButton(ICON_FA_KEY " Use ~/.ssh/id_ed25519.pub")) {
        for (const char* n : {"id_ed25519.pub", "id_rsa.pub", "id_ecdsa.pub"}) {
            if (auto content = platform::readTextFile(platform::homeDir() / ".ssh" / n)) {
                newSshKey_ = fmt::trim(*content);
                break;
            }
        }
        if (newSshKey_.empty()) app.toasts().warning("No public key found in ~/.ssh");
    }
    ImGui::SameLine();
    const std::string key = fmt::trim(newSshKey_);
    ImGui::BeginDisabled(key.empty() || key.find("PRIVATE KEY") != std::string::npos);
    if (ImGui::Button(ICON_FA_PLUS " Add key")) {
        app.run(cmd::createSshKey(key), [this, &app](const ApiResult&) { ssh_.fetch(app, cmd::showSshKeys()); }, "SSH key added");
        newSshKey_.clear();
    }
    ImGui::EndDisabled();

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "public_key", "ssh_key", "key", "created_at"};
    opts.formatCell = [](const std::string& col, const json& v) {
        std::string s = v.is_null() ? std::string{} : jsonutil::scalarToString(v);
        if ((col == "public_key" || col == "ssh_key") && s.size() > 60) s = s.substr(0, 30) + "..." + s.substr(s.size() - 26);
        if (v.is_number() && col.find("_at") != std::string::npos) return fmt::dateTimeFromEpoch(v.get<double>());
        return s;
    };
    opts.rowMenu = [&](const json& row, int) {
        const std::string pub = jsonutil::str(row, "public_key", jsonutil::str(row, "ssh_key"));
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy public key")) ImGui::SetClipboardText(pub.c_str());
        if (ImGui::MenuItem(ICON_FA_LINK "  Attach to instance...")) {
            pendingSsh_ = row;
            attachInstance_ = -1;
            openAttach_ = true;
        }
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingSsh_ = row;
            openDeleteSsh_ = true;
        }
    };
    ui::JsonTable("##sshkeys", jsonutil::extractArray(ssh_.data, {"ssh_keys", "keys", "results"}), sshTable_, opts);

    if (openDeleteSsh_) {
        ImGui::OpenPopup("Delete SSH key?###delssh");
        openDeleteSsh_ = false;
    }
    const auto id = jsonutil::optInt(pendingSsh_, "id").value_or(0);
    if (ui::ConfirmModal("Delete SSH key?###delssh", std::format("Delete SSH key {} from your account?", id), "Delete")) {
        app.run(cmd::deleteSshKey(id), [this, &app](const ApiResult&) { ssh_.fetch(app, cmd::showSshKeys()); }, "SSH key deleted");
    }

    if (openAttach_) {
        ImGui::OpenPopup("Attach SSH key###attachssh");
        openAttach_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Attach SSH key###attachssh", ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& inst = app.instances();
        ImGui::Text("Attach key %lld to instance:", static_cast<long long>(id));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
        const std::string preview = attachInstance_ >= 0 && static_cast<std::size_t>(attachInstance_) < inst.size()
                                        ? std::to_string(inst[static_cast<std::size_t>(attachInstance_)].id)
                                        : std::string("select instance...");
        if (ImGui::BeginCombo("##ai", preview.c_str())) {
            for (std::size_t i = 0; i < inst.size(); ++i) {
                const std::string l = std::format("{}  {}  {}x {}", inst[i].id, inst[i].label, inst[i].numGpus, inst[i].gpuName);
                if (ImGui::Selectable(l.c_str(), attachInstance_ == static_cast<int>(i))) attachInstance_ = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(attachInstance_ < 0);
        if (ImGui::Button(ICON_FA_LINK " Attach") && attachInstance_ >= 0 && static_cast<std::size_t>(attachInstance_) < inst.size()) {
            app.run(cmd::attachSsh(inst[static_cast<std::size_t>(attachInstance_)].id,
                                   jsonutil::str(pendingSsh_, "public_key", jsonutil::str(pendingSsh_, "ssh_key"))),
                    {}, "SSH key attached");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void KeysView::setApiPermissions(json perms) {
    newApiPerms_ = std::move(perms);
    newApiPermsText_ = newApiPerms_.dump(4);
    newApiPermsValid_ = true;
}

void KeysView::openCreateApiKey(App& app) {
    if (!newApiInitialized_) {
        // Like the console: start with everything the current key may do (except admin).
        const json* rights = jsonutil::find(app.user(), "rights");
        json perms = apiperm::defaultPermissions(rights ? *rights : json());
        if (!apiperm::hasAnyPermission(perms["api"])) {
            // Rights unknown (user not loaded / not reported): a safe everyday set.
            perms = json{{"api",
                          {{"user_read", json::object()},
                           {"instance_read", json::object()},
                           {"instance_write", json::object()},
                           {"misc", json::object()}}}};
        }
        setApiPermissions(std::move(perms));
        newApiInitialized_ = true;
    }
    if (app.user().is_null()) app.refreshUser();
    openCreateApi_ = true;
}

void KeysView::drawPermissionTable(App& app) {
    using apiperm::Level;
    const ui::Palette& p = ui::palette();
    const float fs = ImGui::GetFontSize();
    json& api = newApiPerms_["api"];
    if (!api.is_object()) api = json::object();
    const json* rights = jsonutil::find(app.user(), "rights");
    const auto grant = apiperm::grantable(rights ? *rights : json());
    const bool rightsKnown = !grant.empty();
    const bool tfaAll = apiperm::tfaAll(api);
    bool changed = false;

    // Standard changes keep only the permissions shown here (like the console); Advanced keeps everything.
    auto setLevel = [&](const char* cat, Level l) {
        api = apiperm::withLevel(apiperm::onlyKnownKeys(api), cat, l);
        changed = true;
    };
    auto tfaSwitch = [&](const char* id, const std::string& key, bool granted) {
        bool on = apiperm::tfa(api, key);
        ImGui::BeginDisabled(!granted);
        if (ui::ToggleSwitch(id, &on)) {
            api = apiperm::withTfa(api, key, on);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(granted ? "Require two-factor authentication (2FA) for requests that use this permission."
                                      : "Grant the permission first.");
        }
    };

    const int cols = tfaAll ? 3 : 5;
    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##perms", cols, tf)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Permission", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        if (!tfaAll) ImGui::TableSetupColumn("2FA", ImGuiTableColumnFlags_WidthFixed, fs * 2.6f);
        ImGui::TableSetupColumn("Permission##w", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        if (!tfaAll) ImGui::TableSetupColumn("2FA##w", ImGuiTableColumnFlags_WidthFixed, fs * 2.6f);
        ImGui::TableHeadersRow();
        const int colRead = 1, colReadTfa = 2, colWrite = tfaAll ? 2 : 3, colWriteTfa = 4;

        for (const apiperm::Category& c : apiperm::categories()) {
            const std::string_view key = c.key;
            // Rows the current key cannot grant are hidden (admin unless the key has it; unknown rights: no admin).
            const auto g = grant.find(c.key);
            if (rightsKnown ? g == grant.end() : key == "admin") continue;
            const Level maxLevel = rightsKnown ? g->second : Level::ReadWrite;
            const Level level = apiperm::levelOf(api, key);
            ImGui::TableNextRow();
            ImGui::PushID(c.key);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(c.label);
            ImGui::SameLine(0, fs * 0.4f);
            ui::TextDim(ICON_FA_CIRCLE_INFO);
            if (ImGui::IsItemHovered()) {
                ImGui::SetNextWindowSize(ImVec2(fs * 22.0f, 0));
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", c.tooltip);
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(colRead);
            if (key == "misc") {
                bool on = level != Level::NoAccess;
                if (ImGui::Checkbox("Enabled", &on)) setLevel(c.key, on ? Level::ReadWrite : Level::NoAccess);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.tooltip);
                if (!tfaAll) {
                    ImGui::TableSetColumnIndex(colReadTfa);
                    tfaSwitch("##tfa", "misc", on);
                }
            } else {
                bool read = level != Level::NoAccess;
                if (ImGui::Checkbox("Read", &read)) setLevel(c.key, read ? Level::Read : Level::NoAccess);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s: view only (list and show), no changes.%s", c.label,
                                      level == Level::ReadWrite ? "\nUnticking Read also removes Write." : "");
                }
                if (!tfaAll) {
                    ImGui::TableSetColumnIndex(colReadTfa);
                    tfaSwitch("##tfar", apiperm::readKey(key), read);
                }
                ImGui::TableSetColumnIndex(colWrite);
                bool write = level == Level::ReadWrite;
                const bool canWrite = maxLevel == Level::ReadWrite;
                ImGui::BeginDisabled(!canWrite);
                if (ImGui::Checkbox("Write", &write)) setLevel(c.key, write ? Level::ReadWrite : Level::Read);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (canWrite) {
                        ImGui::SetTooltip("%s: create, change and delete. Includes Read.", c.label);
                    } else {
                        ImGui::SetTooltip("Your current API key can only read %s, so it cannot grant Write.", c.label);
                    }
                }
                if (!tfaAll) {
                    ImGui::TableSetColumnIndex(colWriteTfa);
                    tfaSwitch("##tfaw", apiperm::writeKey(key), write);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!rightsKnown) {
        ui::TextWithColor(p.warning, ICON_FA_TRIANGLE_EXCLAMATION "  Your key's own rights are unknown - all types are offered; "
                                                                  "vast.ai rejects permissions your key does not have.");
    }
    if (changed) setApiPermissions(newApiPerms_);
}

void KeysView::drawCreateApiKey(App& app) {
    const ui::Palette& p = ui::palette();
    const float fs = ImGui::GetFontSize();
    if (openCreateApi_) {
        ImGui::OpenPopup("Create API Key###createapikey");
        openCreateApi_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 40.0f, 0), ImVec2(fs * 40.0f, ImGui::GetMainViewport()->WorkSize.y * 0.9f));
    if (!ui::BeginDialog("Create API Key###createapikey", ImGuiWindowFlags_AlwaysAutoResize)) return;

    ui::TextDim("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##apiname", "3-50 characters", &newApiName_) && newApiName_.size() > 50) newApiName_.resize(50);
    const std::size_t nameLen = fmt::trim(newApiName_).size();
    if (nameLen > 0 && nameLen < 3) ui::TextWithColor(p.danger, "Name must be between 3 and 50 characters");

    if (!newApiPerms_["api"].is_object()) newApiPerms_["api"] = json::object();
    ImGui::Spacing();
    if (permTab_ == 0) {
        bool all = apiperm::tfaAll(newApiPerms_["api"]);
        if (ui::ToggleSwitch("Require 2FA for this key or customize below", &all)) {
            json perms = newApiPerms_;
            perms["api"] = apiperm::withTfaAll(perms["api"], all);
            setApiPermissions(std::move(perms));
        }
        ui::HelpMarker("On: every request made with this key requires two-factor authentication (2FA). "
                       "Off: choose 2FA per permission in the table below.");
    }

    if (ImGui::BeginTabBar("##permtabs")) {
        if (ImGui::BeginTabItem("Standard")) {
            if (permTab_ != 0 && !newApiPermsValid_) setApiPermissions(newApiPerms_);  // drop an invalid edit
            permTab_ = 0;
            ImGui::PushTextWrapPos(0.0f);
            ui::TextDim("Changing Standard selections keeps only the permissions shown below. Use Advanced to customize "
                        "other scopes.");
            ImGui::PopTextWrapPos();
            drawPermissionTable(app);
            if (ImGui::TextLink("Read more")) platform::openUrl("https://docs.vast.ai/api-reference/permissions");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced")) {
            permTab_ = 1;
            ui::TextDim("Permissions as JSON - edit freely, the Standard tab follows.");
            if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
            if (ImGui::InputTextMultiline("##apiperm", &newApiPermsText_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 18),
                                          ImGuiInputTextFlags_AllowTabInput)) {
                const json parsed = json::parse(newApiPermsText_, nullptr, false);
                newApiPermsValid_ = !parsed.is_discarded() && parsed.is_object();
                if (newApiPermsValid_) newApiPerms_ = parsed;
            }
            if (ui::monoFont()) ImGui::PopFont();
            if (!newApiPermsValid_) ui::TextWithColor(p.danger, "Invalid JSON");
            if (ImGui::TextLink("Permission syntax")) platform::openUrl("https://docs.vast.ai/api-reference/permissions");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    const json* finalApi = jsonutil::find(newApiPerms_, "api");
    const bool anyPerm = finalApi && apiperm::hasAnyPermission(*finalApi);
    const char* why = nameLen < 3 || nameLen > 50 ? "Enter a name (3-50 characters)."
                      : !newApiPermsValid_       ? "Fix the JSON first."
                      : !anyPerm                 ? "Select at least one permission."
                                                 : nullptr;
    if (why) {
        ImGui::AlignTextToFramePadding();
        ui::TextDim(why);
    } else {
        ImGui::AlignTextToFramePadding();
        ui::TextDim("The new key is shown once and never logged.");
    }
    const float btnW = fs * 7.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - btnW * 2.0f - ImGui::GetStyle().ItemSpacing.x);
    if (ui::SecondaryButton("Cancel", ImVec2(btnW, 0))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(why != nullptr);
    if (ImGui::Button(ICON_FA_KEY " Create", ImVec2(btnW, 0))) {
        app.run(cmd::createApiKey(fmt::trim(newApiName_), newApiPerms_), [this, &app](const ApiResult& r) {
            createdKey_ = findSecret(r.data.is_null() ? json(r.text) : r.data);
            if (createdKey_.empty()) createdKey_ = fmt::trim(r.text);
            openCreatedKey_ = true;
            apiKeys_.fetch(app, cmd::showApiKeys());
            // Next time the form starts fresh.
            newApiName_.clear();
            newApiInitialized_ = false;
            permTab_ = 0;
        });
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void KeysView::drawApiKeys(App& app) {
    apiKeys_.autoFetch(app, cmd::showApiKeys());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", apiKeys_.loading)) apiKeys_.fetch(app, cmd::showApiKeys());
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " Create API key")) openCreateApiKey(app);
    ui::InlineError(apiKeys_.error);

    ui::JsonTableOptions opts;
    opts.preferredColumns = {"id", "name", "created_at", "permissions", "key"};
    opts.formatCell = maskKeyCell;
    opts.rowMenu = [&](const json& row, int) {
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete...")) {
            pendingApi_ = row;
            openDeleteApi_ = true;
        }
    };
    ui::JsonTable("##apikeys", jsonutil::extractArray(apiKeys_.data, {"apikeys", "keys", "results"}), apiTable_, opts);

    drawCreateApiKey(app);

    if (openCreatedKey_) {
        ImGui::OpenPopup("New API key###newkey");
        openCreatedKey_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Closable with x but deliberately not by an outside click: the key is shown only once.
    bool keyOpen = true;
    if (ImGui::BeginPopupModal("New API key###newkey", &keyOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::TextWithColor(ui::palette().warning, ICON_FA_TRIANGLE_EXCLAMATION "  Copy this key now - it will not be shown again.");
        if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
        ImGui::InputText("##createdkey", &createdKey_, ImGuiInputTextFlags_ReadOnly);
        if (ui::monoFont()) ImGui::PopFont();
        if (ImGui::Button(ICON_FA_COPY " Copy")) ImGui::SetClipboardText(createdKey_.c_str());
        ImGui::SameLine();
        if (ui::SecondaryButton("Done")) {
            secretbox::wipe(createdKey_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!keyOpen) {
        secretbox::wipe(createdKey_);
    }

    if (openDeleteApi_) {
        ImGui::OpenPopup("Delete API key?###delapi");
        openDeleteApi_ = false;
    }
    const auto id = jsonutil::optInt(pendingApi_, "id").value_or(0);
    if (ui::ConfirmModal("Delete API key?###delapi",
                         std::format("Delete API key {} (\"{}\")? Anything using it stops working.", id,
                                     jsonutil::str(pendingApi_, "name")),
                         "Delete")) {
        app.run(cmd::deleteApiKey(id), [this, &app](const ApiResult&) { apiKeys_.fetch(app, cmd::showApiKeys()); }, "API key deleted");
    }
}

void KeysView::drawEnvVars(App& app) {
    envVars_.autoFetch(app, cmd::showEnvVars());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", envVars_.loading)) envVars_.fetch(app, cmd::showEnvVars());
    ui::InlineError(envVars_.error);
    ui::TextDim("Account-level environment variables are injected into every instance you create.");

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputTextWithHint("##envname", "NAME", &envName_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
    ImGui::InputTextWithHint("##envvalue", "value", &envValue_, ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::BeginDisabled(fmt::trim(envName_).empty());
    if (ImGui::Button(ICON_FA_PLUS " Create")) {
        app.run(cmd::createEnvVar(fmt::trim(envName_), envValue_), [this, &app](const ApiResult&) { envVars_.fetch(app, cmd::showEnvVars()); },
                "Variable created");
        envName_.clear();
        secretbox::wipe(envValue_);
    }
    ImGui::EndDisabled();

    const json rows = envRows(envVars_.data);
    if (ImGui::BeginTable("##envtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
        ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            const std::string name = jsonutil::str(r, "name");
            const std::string value = jsonutil::str(r, "value");
            ImGui::TableNextRow();
            ImGui::PushID(name.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(1);
            const bool shown = revealed_.contains(name);
            ImGui::TextUnformatted(shown ? value.c_str() : "********");
            ImGui::TableSetColumnIndex(2);
            if (ui::IconButton(shown ? ICON_FA_EYE_SLASH : ICON_FA_EYE, shown ? "Hide" : "Reveal")) {
                if (shown) {
                    revealed_.erase(name);
                } else {
                    revealed_.insert(name);
                }
            }
            ImGui::SameLine(0, 2);
            if (ui::IconButton(ICON_FA_PEN, "Update value")) {
                editEnvName_ = name;
                secretbox::wipe(editEnvValue_);
                openEditEnv_ = true;
            }
            ImGui::SameLine(0, 2);
            if (ui::IconButton(ICON_FA_TRASH, "Delete")) {
                pendingEnv_ = name;
                openDeleteEnv_ = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (openEditEnv_) {
        ImGui::OpenPopup("Update variable###editenv");
        openEditEnv_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ui::BeginDialog("Update variable###editenv", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New value for %s", editEnvName_.c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
        ImGui::InputText("##newval", &editEnvValue_, ImGuiInputTextFlags_Password);
        if (ImGui::Button("Update")) {
            app.run(cmd::updateEnvVar(editEnvName_, editEnvValue_),
                    [this, &app](const ApiResult&) { envVars_.fetch(app, cmd::showEnvVars()); }, "Variable updated");
            secretbox::wipe(editEnvValue_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (openDeleteEnv_) {
        ImGui::OpenPopup("Delete variable?###delenv");
        openDeleteEnv_ = false;
    }
    if (ui::ConfirmModal("Delete variable?###delenv", "Delete environment variable " + pendingEnv_ + "?", "Delete")) {
        app.run(cmd::deleteEnvVar(pendingEnv_), [this, &app](const ApiResult&) { envVars_.fetch(app, cmd::showEnvVars()); },
                "Variable deleted");
    }
}

}  // namespace vast
