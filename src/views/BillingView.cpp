#include "views/BillingView.hpp"

#include "App.hpp"
#include "IconsFontAwesome6.h"
#include "Platform.hpp"
#include "Theme.hpp"
#include "backend/Commands.hpp"
#include "imgui.h"
#include "implot.h"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "widgets/Spinner.hpp"

#include <cmath>
#include <format>
#include <map>

namespace vast {

namespace {

void defaultRange(std::string& start, std::string& end, int days) {
    if (start.empty()) start = fmt::dateFromEpoch(fmt::nowEpoch() - days * 86400.0);
    if (end.empty()) end = fmt::dateFromEpoch(fmt::nowEpoch());
}

std::optional<double> rowTime(const json& row) {
    for (const char* k : {"timestamp", "when", "start", "date", "created_at", "end"}) {
        if (const json* v = jsonutil::find(row, k)) {
            if (v->is_number()) return v->get<double>();
            if (v->is_string()) {
                if (auto e = fmt::epochFromDate(v->get<std::string>().substr(0, 10))) return e;
                if (auto d = jsonutil::parseDouble(v->get<std::string>())) return d;
            }
        }
    }
    return std::nullopt;
}

bool exportCsv(App& app, const json& rows, const char* defaultName) {
    const auto path = platform::saveFileDialog("Export CSV", defaultName);
    if (!path) return false;
    if (platform::writeTextFileAtomic(*path, fmt::jsonArrayToCsv(rows))) {
        app.toasts().success("Exported " + std::to_string(rows.size()) + " rows", platform::pathToUtf8(*path));
        return true;
    }
    app.toasts().error("Export failed", platform::pathToUtf8(*path));
    return false;
}

void dateRange(std::string& start, std::string& end) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    ImGui::InputTextWithHint("##start", "YYYY-MM-DD", &start);
    ImGui::SameLine();
    ui::TextDim("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    ImGui::InputTextWithHint("##end", "YYYY-MM-DD", &end);
    ImGui::SameLine();
    for (const auto& [label, days] : {std::pair{"7d", 7}, std::pair{"30d", 30}, std::pair{"90d", 90}, std::pair{"1y", 365}}) {
        if (ui::SecondaryButton(label)) {
            start = fmt::dateFromEpoch(fmt::nowEpoch() - days * 86400.0);
            end = fmt::dateFromEpoch(fmt::nowEpoch());
        }
        ImGui::SameLine();
    }
}

void barChart(const char* id, const DailySeries& s, const char* primaryLabel, const char* secondaryLabel, float height) {
    if (s.days.empty()) {
        ui::TextDim("No data for this range.");
        return;
    }
    if (ImPlot::BeginPlot(id, ImVec2(-FLT_MIN, height), ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, "$", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        std::vector<double> x2(s.days.size());
        for (std::size_t i = 0; i < s.days.size(); ++i) x2[i] = s.days[i] + 86400.0 * 0.3;
        ImPlotSpec primary;
        primary.FillColor = ui::palette().accent;
        primary.LineColor = ui::palette().accent;
        ImPlot::PlotBars(primaryLabel, s.days.data(), s.primary.data(), static_cast<int>(s.days.size()), 86400.0 * 0.4,
                         primary);
        ImPlotSpec secondary;
        secondary.FillColor = ui::palette().success;
        secondary.LineColor = ui::palette().success;
        ImPlot::PlotBars(secondaryLabel, x2.data(), s.secondary.data(), static_cast<int>(x2.size()), 86400.0 * 0.4,
                         secondary);
        ImPlot::EndPlot();
    }
}

}  // namespace

// ---- Billing ------------------------------------------------------------------

void BillingView::load(App& app) {
    invoices_.fetch(app, cmd::showInvoices(start_, end_), [this](Resource& r, const ApiResult&) {
        series_ = {};
        totalCharges_ = totalCredits_ = 0;
        std::map<double, std::pair<double, double>> byDay;
        for (const auto& row : jsonutil::extractArray(r.data, {"invoices"})) {
            const auto t = rowTime(row);
            const double amount = std::abs(jsonutil::num(row, "amount"));
            const std::string type = jsonutil::str(row, "type");
            const bool credit = type == "payment" || type == "credit" || jsonutil::optBool(row, "is_credit").value_or(false);
            (credit ? totalCredits_ : totalCharges_) += amount;
            if (!t) continue;
            auto& d = byDay[std::floor(*t / 86400.0) * 86400.0];
            (credit ? d.second : d.first) += amount;
        }
        for (const auto& [day, v] : byDay) {
            series_.days.push_back(day);
            series_.primary.push_back(v.first);
            series_.secondary.push_back(v.second);
        }
    });
}

void BillingView::draw(App& app) {
    const Settings& st = app.settings();
    ui::PageHeader(ICON_FA_CREDIT_CARD, "Billing", "balance, invoices and charges");
    defaultRange(start_, end_, 30);

    const json& u = app.user();
    if (ui::BeginCard("##balance", ImVec2(0, 0))) {
        ui::TextDim("Current credit");
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
        ImGui::TextUnformatted(u.is_null() ? kMissing : fmt::money(jsonutil::num(u, "credit"), st.currency, 2).c_str());
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetFontSize() * 14);
        ImGui::BeginGroup();
        ui::TextDim(std::format("Balance: {}", fmt::money(jsonutil::optDouble(u, "balance"), st.currency, 2)).c_str());
        if (!u.is_null()) {
            // When the credit reaches 0 at the current instance costs (prepaid instances join when their credit ends).
            const double credit = jsonutil::num(u, "credit", jsonutil::num(u, "balance", 0.0));
            const App::AccountCost cost = app.accountCost();
            const auto hours = creditRunOutHours(credit, cost.rate, cost.later);
            const ui::Palette& p = ui::palette();
            if (!hours) {
                ui::TextDim(ICON_FA_HOURGLASS_HALF "  No instance costs - the credit does not run out");
            } else if (*hours <= 0.0) {
                ui::TextWithColor(p.danger, ICON_FA_HOURGLASS_END "  Credit used up");
            } else {
                const double at = fmt::nowEpoch() + *hours * 3600.0;
                const ImVec4 c = *hours < 24.0 ? p.danger : *hours < 72.0 ? p.warning : p.text;
                ui::TextWithColor(c, std::format(ICON_FA_HOURGLASS_HALF "  Runs out in {}  -  {}", fmt::humanDuration(*hours * 3600.0),
                                                 fmt::dateTimeFromEpoch(at))
                                         .c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Estimate at the current instance costs: %s/hr billed to the account%s.\n"
                                  "Bandwidth, new rentals and price changes are not included.",
                                  fmt::money(cost.rate, st.currency, 3).c_str(),
                                  cost.prepaidCount > 0 ? std::format(", plus {} prepaid instance(s) at their normal price once their "
                                                                      "prepaid credit is used up",
                                                                      cost.prepaidCount)
                                                              .c_str()
                                                        : "");
            }
        }
        ui::TextDim(std::format("Account: {}  {}", jsonutil::str(u, "username"), jsonutil::str(u, "email")).c_str());
        if (auto thr = jsonutil::optDouble(u, "balance_threshold")) {
            ui::TextDim(std::format("Low-balance alert at {} ({})", fmt::money(*thr, st.currency, 2),
                                    jsonutil::optBool(u, "balance_threshold_enabled").value_or(false) ? "on" : "off")
                            .c_str());
        }
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 12);
        if (ui::SecondaryButton(ICON_FA_ARROWS_ROTATE " Refresh")) app.refreshUser();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " Add credit")) platform::openUrl("https://cloud.vast.ai/billing/");
    }
    ui::EndCard();
    ImGui::Spacing();

    dateRange(start_, end_);
    if (ui::BusyButton(ICON_FA_MAGNIFYING_GLASS, "Load invoices", invoices_.loading) ||
        (!invoices_.loaded && !invoices_.loading && invoices_.attemptedAt == 0 && !app.apiKey().key.empty())) {
        load(app);
    }
    ImGui::SameLine();
    const json rows = jsonutil::extractArray(invoices_.data, {"invoices"});
    if (ui::SecondaryButton(ICON_FA_FILE_CSV " Export CSV")) exportCsv(app, rows, "vast_invoices.csv");
    ui::InlineError(invoices_.error);

    ImGui::Text("Charges: %s   Credits/payments: %s   (%zu items)", fmt::money(totalCharges_, st.currency, 2).c_str(),
                fmt::money(totalCredits_, st.currency, 2).c_str(), rows.size());
    barChart("##charges", series_, "Charges", "Credits", ImGui::GetFontSize() * 13);
    ui::JsonTableOptions opts;
    opts.preferredColumns = {"timestamp", "type", "description", "quantity", "rate", "amount", "instance_id", "machine_id"};
    opts.formatCell = [](const std::string& col, const json& v) {
        if (col == "timestamp" && v.is_number()) return fmt::dateTimeFromEpoch(v.get<double>());
        return v.is_null() ? std::string{} : jsonutil::scalarToString(v);
    };
    ui::JsonTable("##invoices", rows, table_, opts);
}

// ---- Earnings -----------------------------------------------------------------

void EarningsView::load(App& app) {
    earnings_.fetch(app, cmd::showEarnings(start_, end_, machineId_ > 0 ? std::optional(machineId_) : std::nullopt),
                    [this](Resource& r, const ApiResult&) {
                        series_ = {};
                        for (const auto& row : jsonutil::extractArray(r.data, {"per_day", "days"})) {
                            double day = jsonutil::num(row, "day", -1);
                            if (day < 0) {
                                auto t = rowTime(row);
                                if (!t) continue;
                                day = *t;
                            } else if (day < 1e6) {
                                day *= 86400.0;
                            }
                            const double gpu = jsonutil::num(row, "gpu_earn");
                            const double other = jsonutil::num(row, "sto_earn") + jsonutil::num(row, "bwu_earn") +
                                                 jsonutil::num(row, "bwd_earn");
                            series_.days.push_back(day);
                            series_.primary.push_back(gpu);
                            series_.secondary.push_back(other);
                        }
                    });
}

void EarningsView::draw(App& app) {
    ui::PageHeader(ICON_FA_SACK_DOLLAR, "Earnings", "host earnings");
    defaultRange(start_, end_, 30);
    dateRange(start_, end_);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputScalar("Machine ID (0 = all)", ImGuiDataType_S64, &machineId_);
    ImGui::SameLine();
    if (ui::BusyButton(ICON_FA_MAGNIFYING_GLASS, "Load earnings", earnings_.loading) ||
        (!earnings_.loaded && !earnings_.loading && earnings_.attemptedAt == 0 && !app.apiKey().key.empty())) {
        load(app);
    }
    ImGui::SameLine();
    const json perDay = jsonutil::extractArray(earnings_.data, {"per_day", "days"});
    const json perMachine = earnings_.data.is_object() && earnings_.data.contains("per_machine")
                                ? earnings_.data["per_machine"]
                                : json::array();
    if (ui::SecondaryButton(ICON_FA_FILE_CSV " Export CSV")) exportCsv(app, perDay, "vast_earnings.csv");
    ui::InlineError(earnings_.error);

    if (earnings_.data.is_object() && earnings_.data.contains("summary")) {
        const json& s = earnings_.data["summary"];
        std::string line;
        for (const auto& [k, v] : s.items()) {
            if (v.is_number()) line += std::format("{}: {:.2f}   ", k, v.get<double>());
        }
        ImGui::TextUnformatted(line.c_str());
    }
    double total = 0;
    for (std::size_t i = 0; i < series_.days.size(); ++i) total += series_.primary[i] + series_.secondary[i];
    ImGui::Text("Total in range: %s", fmt::money(total, app.settings().currency, 2).c_str());
    barChart("##earnchart", series_, "GPU", "Storage + bandwidth", ImGui::GetFontSize() * 13);

    if (ImGui::BeginTabBar("##earntabs")) {
        if (ImGui::BeginTabItem("Per day")) {
            ui::JsonTable("##perday", perDay, perDayTable_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Per machine")) {
            ui::JsonTable("##permachine", perMachine, perMachineTable_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raw")) {
            ui::JsonText("##earnraw", earnings_.data);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace vast
