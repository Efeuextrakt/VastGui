#pragma once

#include "model/Instance.hpp"
#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace vast {

class App;

class InstancesView {
public:
    void draw(App& app);
    void drawFloating(App& app);

private:
    enum class Dialog {
        None, Label, ChangeBid, Prepay, Execute, Copy, CloudCopy, CreateTemplate, Update, Destroy, Recycle,
        Snapshot, AttachSsh, Json
    };

    // One log source (container or daemon) with its own text, filter and tail.
    struct LogTab {
        int tail = 1000;
        std::string filter;
        Resource res;
    };

    struct LogWindow {
        std::int64_t id = 0;
        bool open = true;
        bool autoRefresh = true;
        int intervalSec = 10;
        int active = 0;  // 0 instance logs, 1 daemon logs
        LogTab tabs[2];
    };

    void fetchLogs(App& app, LogWindow& w, int tab);

    void drawToolbar(App& app);
    void drawTable(App& app);
    void drawRowMenu(App& app, const Instance& inst);
    void drawDialogs(App& app);
    void openDialog(Dialog d, const Instance& inst);
    void openLogs(App& app, std::int64_t id);
    [[nodiscard]] std::vector<std::int64_t> selectedIds() const;

    std::set<std::int64_t> selected_;
    std::string filter_;
    int statusFilter_ = 0;
    bool tableColumnsApplied_ = false;

    Dialog dialog_ = Dialog::None;
    bool openPopup_ = false;
    Instance target_;
    std::vector<std::int64_t> bulkIds_;
    std::string text1_, text2_, text3_, text4_;
    double number_ = 0.0;
    int choice_ = 0;
    bool flag_ = false;
    std::string execOutput_;
    bool busy_ = false;
    Resource connections_;
    Resource sshKeys_;

    std::vector<std::unique_ptr<LogWindow>> logWindows_;
};

}  // namespace vast
