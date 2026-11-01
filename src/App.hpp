#pragma once

#include "Config.hpp"
#include "Log.hpp"
#include "backend/ApiKey.hpp"
#include "backend/VastClient.hpp"
#include "model/Instance.hpp"
#include "widgets/Toast.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ImFont;

namespace vast {

enum class Page {
    Search,
    Templates,
    Instances,
    Storage,
    Serverless,
    Billing,
    Earnings,
    Team,
    AuditLogs,
    Keys,
    Settings,
    Hosting,
    Console,
    Count
};

class SearchView;
class TemplatesView;
class InstancesView;
class StorageView;
class ServerlessView;
class BillingView;
class EarningsView;
class TeamView;
class AuditLogsView;
class KeysView;
class HostingView;
class ConsoleView;
class SettingsView;

inline constexpr const char* kAppVersion = "1.1.0";

class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Call after the ImGui/ImPlot contexts exist and before the first frame.
    void init(float dpiScale);
    // Call before ImGui::NewFrame(): applies deferred style/font changes.
    void preFrame();
    void frame();
    void shutdown();
    void setDpiScale(float scale);
    [[nodiscard]] float dpiScale() const { return dpiScale_; }
    [[nodiscard]] const char* iniPath() const { return iniPathUtf8_.c_str(); }

    // Services for views.
    [[nodiscard]] Settings& settings() { return settings_; }
    void markSettingsDirty();
    void applyAppearance();    // theme/font changes (outside a frame)
    void requestAppearanceUpdate() { appearanceDirty_ = true; }  // applied at the start of the next frame
    void reconfigureBackend(); // backend/API key changes
    [[nodiscard]] VastClient& client() { return *client_; }
    [[nodiscard]] Log& log() { return log_; }
    [[nodiscard]] ui::Toasts& toasts() { return toasts_; }
    [[nodiscard]] const ConfigPaths& paths() const { return paths_; }
    [[nodiscard]] const ResolvedApiKey& apiKey() const { return apiKey_; }

    void navigate(Page p);
    [[nodiscard]] Page page() const { return page_; }

    // Submits a call; toasts `successMsg` on success (errors are toasted globally).
    std::uint64_t run(ApiCall call, std::function<void(const ApiResult&)> onSuccess = {}, std::string successMsg = {});

    // Shared instance list (sidebar badge, instances page, rent flow).
    [[nodiscard]] const std::vector<Instance>& instances() const { return instances_; }
    [[nodiscard]] bool instancesLoading() const { return instancesLoading_; }
    [[nodiscard]] double instancesFetchedAt() const { return instancesFetchedAt_; }
    [[nodiscard]] const std::string& instancesError() const { return instancesError_; }
    void refreshInstances(bool quiet = false);
    // $/hr samples per instance id (for sparklines), appended on every refresh.
    [[nodiscard]] const std::vector<std::pair<double, double>>* costHistory(std::int64_t id) const;

    [[nodiscard]] const json& user() const { return user_; }
    // Reloads `show user`; `then` runs on the UI thread when the call finished (success or not).
    void refreshUser(std::function<void()> then = {});

    void setCurrentTemplate(const Template& t);

    [[nodiscard]] SearchView& searchView() { return *search_; }
    [[nodiscard]] ConsoleView& consoleView() { return *console_; }

private:
    void loadFonts();
    void drawSidebar(float width);
    void drawStatusBar();
    void drawActivityLog();
    void drawPage();
    void autoRefresh();
    void saveIfDirty(bool force);

    Log log_;
    ConfigPaths paths_;
    std::string iniPathUtf8_;
    Settings settings_;
    ResolvedApiKey apiKey_;
    std::unique_ptr<VastClient> client_;
    ui::Toasts toasts_;
    float dpiScale_ = 1.0f;
    Page page_ = Page::Search;
    bool settingsDirty_ = false;
    double dirtySince_ = 0;
    bool firstFrame_ = true;
    bool appearanceDirty_ = false;
    unsigned int dockspaceId_ = 0;

    std::vector<Instance> instances_;
    bool instancesLoading_ = false;
    double instancesFetchedAt_ = 0;
    double instancesAttemptedAt_ = 0;
    std::string instancesError_;
    std::vector<std::pair<std::int64_t, std::vector<std::pair<double, double>>>> costHistory_;
    json user_;
    double userFetchedAt_ = 0;
    std::vector<LogEntry> activityCache_;
    std::uint64_t activityVersion_ = 0;

    std::unique_ptr<SearchView> search_;
    std::unique_ptr<TemplatesView> templates_;
    std::unique_ptr<InstancesView> instancesView_;
    std::unique_ptr<StorageView> storage_;
    std::unique_ptr<ServerlessView> serverless_;
    std::unique_ptr<BillingView> billing_;
    std::unique_ptr<EarningsView> earnings_;
    std::unique_ptr<TeamView> team_;
    std::unique_ptr<AuditLogsView> audit_;
    std::unique_ptr<KeysView> keys_;
    std::unique_ptr<HostingView> hosting_;
    std::unique_ptr<ConsoleView> console_;
    std::unique_ptr<SettingsView> settingsView_;
};

}  // namespace vast
