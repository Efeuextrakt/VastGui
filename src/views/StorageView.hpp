#pragma once

#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <cstdint>
#include <string>

namespace vast {

class App;

class StorageView {
public:
    void draw(App& app);

private:
    void drawVolumes(App& app);
    void drawOffers(App& app);
    void drawConnections(App& app);
    void drawCopy(App& app);

    Resource volumes_;
    Resource offers_;
    Resource connections_;
    ui::JsonTableState volumesTable_;
    ui::JsonTableState offersTable_;
    ui::JsonTableState connTable_;
    std::string offerQuery_ = "disk_space>=50";
    double offerStorage_ = 50.0;

    json pendingDelete_;
    bool openDelete_ = false;
    json createFrom_;
    bool openCreate_ = false;
    double createSize_ = 50.0;
    std::string createName_;
    json cloneFrom_;
    bool openClone_ = false;
    std::int64_t cloneDest_ = 0;
    double cloneSize_ = 0.0;

    int copyInstance_ = -1;
    int copyDirection_ = 0;
    std::string copyConn_;
    std::string copySrc_ = "/workspace/";
    std::string copyDst_ = "/";
    bool copyDryRun_ = false;
    std::string plainSrc_;
    std::string plainDst_;
};

}  // namespace vast
