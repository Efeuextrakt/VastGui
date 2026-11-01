#include "views/KeysView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "SecretBox.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
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

void KeysView::drawApiKeys(App& app) {
    apiKeys_.autoFetch(app, cmd::showApiKeys());
    if (ui::BusyButton(ICON_FA_ARROWS_ROTATE, "Refresh", apiKeys_.loading)) apiKeys_.fetch(app, cmd::showApiKeys());
    ui::InlineError(apiKeys_.error);

    // A visible separator between the list and the Create panel can be dragged sideways (ratio kept in VastGui.json).
    Settings& st = app.settings();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float em = ImGui::GetFontSize();
    const float splitW = ui::VSplitterWidth();
    const float minList = std::min(em * 12.0f, avail * 0.4f);
    const float maxList = std::max(minList, avail - splitW - em * 16.0f);
    float listW = std::clamp(avail * st.apiKeysSplit, minList, maxList);
    ImGui::BeginChild("##apilist", ImVec2(listW, 0));
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
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    const bool released = ui::VSplitter("##apisplit", &listW, minList, maxList);
    if ((ImGui::IsItemActive() || released) && avail > 0.0f) st.apiKeysSplit = std::clamp(listW / avail, 0.15f, 0.85f);
    if (released) app.markSettingsDirty();
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##apiform", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ui::SectionTitle(ICON_FA_PLUS, "Create API key");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##apiname", "key name", &newApiName_);
    ui::TextDim("Permissions (JSON):");
    if (ui::monoFont()) ImGui::PushFont(ui::monoFont(), 0.0f);
    ImGui::InputTextMultiline("##apiperm", &newApiPermissions_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 10));
    if (ui::monoFont()) ImGui::PopFont();
    const bool valid = !json::parse(newApiPermissions_, nullptr, false).is_discarded();
    if (!valid) ui::TextWithColor(ui::palette().danger, "Invalid JSON");
    ImGui::BeginDisabled(!valid || fmt::trim(newApiName_).empty());
    if (ImGui::Button(ICON_FA_KEY " Create key")) {
        app.run(cmd::createApiKey(fmt::trim(newApiName_), newApiPermissions_), [this, &app](const ApiResult& r) {
            createdKey_ = findSecret(r.data.is_null() ? json(r.text) : r.data);
            if (createdKey_.empty()) createdKey_ = fmt::trim(r.text);
            openCreatedKey_ = true;
            apiKeys_.fetch(app, cmd::showApiKeys());
        });
    }
    ImGui::EndDisabled();
    ui::TextDim("The new key is shown once and never logged.");
    ImGui::EndChild();

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
    if (ImGui::BeginTable("##envtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
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
