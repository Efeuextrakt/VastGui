#pragma once

#include "backend/Commands.hpp"
#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vast {

class App;

class ServerlessView {
public:
    void draw(App& app);
    void drawFloating(App& app);

private:
    struct LogWindow {
        bool endpoint = true;
        std::int64_t id = 0;
        bool open = true;
        int tail = 300;
        Resource res;
    };

    void drawEndpoints(App& app);
    void drawWorkergroups(App& app);
    void drawDeployments(App& app);
    void openLogs(App& app, bool endpoint, std::int64_t id);

    Resource endpoints_;
    Resource workergroups_;
    Resource deployments_;
    ui::JsonTableState epTable_, wgTable_, depTable_;

    bool openEpForm_ = false;
    std::optional<std::int64_t> epEditId_;
    cmd::EndpointParams epForm_;
    int epState_ = 0;

    bool openWgForm_ = false;
    std::optional<std::int64_t> wgEditId_;
    cmd::WorkergroupParams wgForm_;
    std::int64_t wgEndpointId_ = 0;
    std::int64_t wgTemplateId_ = 0;
    double wgGpuRam_ = 0;

    json pendingDelete_;
    int pendingKind_ = 0;  // 0 endpoint, 1 workergroup, 2 deployment
    bool openDelete_ = false;

    std::vector<std::unique_ptr<LogWindow>> logs_;
};

}  // namespace vast
