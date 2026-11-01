#pragma once

#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vast {

class App;

// Daily totals extracted from invoice / earnings rows.
struct DailySeries {
    std::vector<double> days;     // epoch seconds (midnight UTC)
    std::vector<double> primary;  // charges / gpu earnings
    std::vector<double> secondary;  // credits / storage+bandwidth earnings
};

class BillingView {
public:
    void draw(App& app);

private:
    void load(App& app);
    Resource invoices_;
    std::string start_;
    std::string end_;
    ui::JsonTableState table_;
    DailySeries series_;
    double totalCharges_ = 0;
    double totalCredits_ = 0;
};

class EarningsView {
public:
    void draw(App& app);

private:
    void load(App& app);
    Resource earnings_;
    std::string start_;
    std::string end_;
    std::int64_t machineId_ = 0;
    ui::JsonTableState perDayTable_;
    ui::JsonTableState perMachineTable_;
    DailySeries series_;
};

}  // namespace vast
