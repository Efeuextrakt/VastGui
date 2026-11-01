#pragma once

#include "backend/Commands.hpp"
#include "model/Machine.hpp"
#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <string>
#include <vector>

namespace vast {

class App;

class HostingView {
public:
    void draw(App& app);

private:
    enum class Dialog { None, List, MinBid, Defjob, Maint, Reports, Maints, Cleanup, Unlist, Json };
    void open(Dialog d, const Machine& m);
    void drawDialog(App& app);

    Resource machines_;
    std::vector<Machine> parsed_;
    std::string filter_;
    Dialog dialog_ = Dialog::None;
    bool openPopup_ = false;
    Machine target_;
    cmd::ListMachineParams list_;
    double priceGpu_ = 0, priceDisk_ = 0, priceUp_ = 0, priceDown_ = 0, minBid_ = 0, discount_ = 0;
    int minChunk_ = 1;
    std::string endDate_;
    std::string image_;
    std::string args_;
    std::string maintDate_;
    std::string maintTime_ = "02:00";
    double maintHours_ = 2.0;
    int maintCategory_ = 0;
    Resource reports_;
};

}  // namespace vast
