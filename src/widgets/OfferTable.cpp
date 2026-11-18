#include "widgets/OfferTable.hpp"

#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"  // TableSetColumnSortDirection
#include "widgets/Spinner.hpp"
#include "widgets/Widgets.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace vast::ui {

namespace {

enum OfferCol {
    ColId, ColMachine, ColHost, ColGpu, ColNum, ColVram, ColTotalVram, ColTflops, ColDlperf, ColPrice,
    ColTflopsPerDollar, ColDlpPerDollar, ColReliability, ColDuration, ColInetUp, ColInetDown, ColDiskBw,
    ColDisk, ColCpu, ColCores, ColRam, ColPcie, ColCuda, ColLocation, ColVerification, ColPorts, ColRent, ColCount
};

struct OfferColumn {
    const char* name;
    float width;
    bool defaultHidden;
};

constexpr OfferColumn kOfferColumns[ColCount] = {
    {"Offer ID", 80, false}, {"Machine", 65, false},   {"Host", 65, true},          {"GPU", 120, false},
    {"#", 28, false},        {"VRAM/GPU", 70, false},  {"Total VRAM", 75, true},    {"TFLOPS", 60, false},
    {"DLPerf", 60, false},   {"$/hr", 65, false},      {"TFLOPS/$/hr", 85, false},  {"DLP/$/hr", 75, false},
    {"Reliability", 75, false}, {"Max Dur", 70, false},
    {"Inet Up", 65, false},  {"Inet Down", 70, false}, {"Disk BW", 65, false},      {"Disk GB", 60, true},
    {"CPU", 160, true},      {"Cores", 55, true},      {"RAM GB", 60, true},        {"PCIe", 70, true},
    {"CUDA", 45, false},     {"Location", 110, false}, {"Status", 80, false},       {"Ports", 50, true},
    {"", 60, false},
};

constexpr std::pair<int, SortKey> kColumnSortKeys[] = {
    {ColNum, SortKey::NumGpus},          {ColVram, SortKey::Vram},
    {ColTflops, SortKey::Tflops},        {ColDlperf, SortKey::Dlperf},
    {ColPrice, SortKey::Price},          {ColDlpPerDollar, SortKey::DlpPerDollar},
    {ColTflopsPerDollar, SortKey::TflopsPerDollar}, {ColReliability, SortKey::Reliability},
    {ColDuration, SortKey::Duration},    {ColInetUp, SortKey::InetUp},
    {ColInetDown, SortKey::InetDown},    {ColDiskBw, SortKey::DiskBw},
    {ColCuda, SortKey::Cuda},
};

std::optional<SortKey> sortKeyForColumn(int c) {
    for (const auto& [col, key] : kColumnSortKeys) {
        if (col == c) return key;
    }
    return std::nullopt;
}

std::optional<int> sortColumnFor(SortKey k) {
    for (const auto& [col, key] : kColumnSortKeys) {
        if (key == k) return col;
    }
    return std::nullopt;
}

std::optional<double> numericCell(int c, const Offer& o, const OfferMetrics& m) {
    switch (c) {
        case ColId: return static_cast<double>(o.id);
        case ColMachine: return o.machineId ? std::optional<double>(static_cast<double>(*o.machineId)) : std::nullopt;
        case ColHost: return o.hostId ? std::optional<double>(static_cast<double>(*o.hostId)) : std::nullopt;
        case ColNum: return static_cast<double>(o.numGpus);
        case ColVram: return o.gpuRamGb();
        case ColTotalVram: return o.totalVramGb();
        case ColTflops: return o.totalFlops;
        case ColDlperf: return o.dlperf;
        case ColPrice: return o.priceHr;
        case ColDlpPerDollar: return m.dlpPerDollar;
        case ColTflopsPerDollar: return m.tflopsPerDollar;
        case ColReliability: return o.reliability;
        case ColDuration: return o.durationSec;
        case ColInetUp: return o.inetUp;
        case ColInetDown: return o.inetDown;
        case ColDiskBw: return o.diskBw;
        case ColDisk: return o.diskSpace;
        case ColCores: return o.cpuCoresEffective;
        case ColRam: return o.cpuRamGb();
        case ColPcie: return o.pcieBw;
        case ColCuda: return o.cudaMaxGood;
        case ColPorts: return o.directPortCount;
        default: return std::nullopt;
    }
}

std::string textCell(int c, const Offer& o, const OfferMetrics& m, const CurrencyFormat& cur) {
    switch (c) {
        case ColId: return std::to_string(o.id);
        case ColMachine: return o.machineId ? std::to_string(*o.machineId) : kMissing;
        case ColHost: return o.hostId ? std::to_string(*o.hostId) : kMissing;
        case ColGpu: return o.gpuName;
        case ColNum: return std::to_string(o.numGpus);
        case ColVram: return formatMetric(o.gpuRamGb(), 0);
        case ColTotalVram: return formatMetric(o.totalVramGb(), 0);
        case ColTflops: return formatMetric(o.totalFlops, 1);
        case ColDlperf: return formatMetric(o.dlperf, 1);
        case ColPrice: return fmt::money(o.priceHr, cur);
        case ColDlpPerDollar: return formatMetric(m.dlpPerDollar, 1);
        case ColTflopsPerDollar: return formatMetric(m.tflopsPerDollar, 1);
        case ColReliability: return fmt::percent(o.reliability, 2);
        case ColDuration: return o.durationSec ? fmt::humanDuration(*o.durationSec) : std::string(kMissing);
        case ColInetUp: return formatMetric(o.inetUp, 0);
        case ColInetDown: return formatMetric(o.inetDown, 0);
        case ColDiskBw: return formatMetric(o.diskBw, 0);
        case ColDisk: return formatMetric(o.diskSpace, 0);
        case ColCpu: return o.cpuName;
        case ColCores: return formatMetric(o.cpuCoresEffective, 1);
        case ColRam: return formatMetric(o.cpuRamGb(), 0);
        case ColPcie: return std::format("{} x{}", formatMetric(o.pciGen, 1), formatMetric(o.gpuLanes, 0));
        case ColCuda: return formatMetric(o.cudaMaxGood, 1);
        case ColLocation: return o.geolocation;
        case ColVerification: return o.verification;
        case ColPorts: return formatMetric(o.directPortCount, 0);
        default: return {};
    }
}

}  // namespace

std::vector<bool> OfferTable::defaultColumns() {
    std::vector<bool> v(ColCount, true);
    for (int c = 0; c < ColCount; ++c) v[static_cast<std::size_t>(c)] = !kOfferColumns[c].defaultHidden;
    return v;
}

float OfferTable::heightForRows(std::size_t rows) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float row = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    return row * static_cast<float>(rows + 1) + style.ScrollbarSize + style.CellPadding.y * 2.0f;
}

void OfferTable::setSort(SortKey key, bool desc) { pendingSort_ = std::pair{key, desc}; }

OfferTableResult OfferTable::draw(const char* id, std::span<const Offer> offers, std::span<const OfferMetrics> metrics,
                                  std::span<const DefectInfo> defects, const std::vector<std::size_t>& rows,
                                  const OfferTableConfig& cfg) {
    OfferTableResult result;
    const CurrencyFormat cur = cfg.currency ? *cfg.currency : CurrencyFormat{};
    const float scale = cfg.scale;
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable(id, ColCount, flags, ImVec2(0.0f, cfg.height))) return result;
    ImGui::TableSetupScrollFreeze(2, 1);
    for (int c = 0; c < ColCount; ++c) {
        ImGuiTableColumnFlags cf = ImGuiTableColumnFlags_WidthFixed;
        if (kOfferColumns[c].defaultHidden) cf |= ImGuiTableColumnFlags_DefaultHide;
        if (c == ColRent) cf |= ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide;
        ImGui::TableSetupColumn(kOfferColumns[c].name, cf, kOfferColumns[c].width * scale);
    }
    ImGui::TableHeadersRow();

    // Column visibility: applied from the settings (initially and whenever another table sharing them changed
    // them), otherwise kept in sync with the header's context menu.
    if (std::vector<bool>* cols = cfg.columns; cols && cols->size() == ColCount) {
        if (!columnsApplied_ || *cols != appliedColumns_) {
            for (int c = 0; c < ColCount; ++c) ImGui::TableSetColumnEnabled(c, (*cols)[static_cast<std::size_t>(c)]);
            columnsApplied_ = true;
        } else {
            for (int c = 0; c < ColCount; ++c) {
                const bool en = (ImGui::TableGetColumnFlags(c) & ImGuiTableColumnFlags_IsEnabled) != 0;
                if ((*cols)[static_cast<std::size_t>(c)] != en) {
                    (*cols)[static_cast<std::size_t>(c)] = en;
                    if (cfg.columnsChanged) *cfg.columnsChanged = true;
                }
            }
        }
        appliedColumns_ = *cols;
    }

    if (pendingSort_) {
        const auto [key, desc] = *pendingSort_;
        pendingSort_.reset();
        if (const auto col = sortColumnFor(key)) {
            ImGui::TableSetColumnSortDirection(*col, desc ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending, false);
        } else if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            ImGui::TableSetColumnSortDirection(specs->Specs[0].ColumnIndex, ImGuiSortDirection_None, false);
        }
    }
    int sortCol = -1;
    bool sortDesc = false;
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        sortCol = specs->Specs[0].ColumnIndex;
        sortDesc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        if (specs->SpecsDirty) {
            specs->SpecsDirty = false;
            result.sortKey = sortKeyForColumn(sortCol);
            result.sortDesc = sortDesc;
        }
    }
    // Sorting ~1000+ rows is only redone when the rows or the sort column change, not every frame.
    if (rowsDirty_ || rows != lastRows_ || sortCol != sortCol_ || sortDesc != sortDesc_) {
        sorted_ = rows;
        lastRows_ = rows;
        if (sortCol >= 0) {
            const int c = sortCol;
            const bool desc = sortDesc;
            std::stable_sort(sorted_.begin(), sorted_.end(), [&](std::size_t a, std::size_t b) {
                const auto na = numericCell(c, offers[a], metrics[a]);
                const auto nb = numericCell(c, offers[b], metrics[b]);
                if (na || nb) {
                    if (!na || !nb) return na.has_value();
                    return desc ? *na > *nb : *na < *nb;
                }
                const auto sa = textCell(c, offers[a], metrics[a], cur);
                const auto sb = textCell(c, offers[b], metrics[b], cur);
                return desc ? sa > sb : sa < sb;
            });
        }
        rowsDirty_ = false;
        sortCol_ = sortCol;
        sortDesc_ = sortDesc;
    }

    const Palette& p = palette();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(sorted_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::size_t idx = sorted_[static_cast<std::size_t>(i)];
            const Offer& o = offers[idx];
            const OfferMetrics& m = metrics[idx];
            const DefectInfo& d = defects[idx];
            const OfferRowInfo info = cfg.rowInfo ? cfg.rowInfo(idx) : OfferRowInfo{};
            const bool defective = d.defective();
            const bool incompatible = !info.incompatibleWhy.empty();
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(idx));
            if (info.rentedByMe) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, col(p.info, 0.10f));
            for (int c = 0; c < ColCount; ++c) {
                if (!ImGui::TableSetColumnIndex(c)) continue;
                if (c == ColId) {
                    // Favorite machines: gold ID with a star.
                    if (info.favorite) ImGui::PushStyleColor(ImGuiCol_Text, p.caution);
                    TableRowSelectable(info.favorite ? (ICON_FA_STAR " " + textCell(c, o, m, cur)).c_str() : textCell(c, o, m, cur).c_str(),
                                       false);
                    if (info.favorite) ImGui::PopStyleColor();
                    if (ImGui::BeginPopupContextItem("##rowctx")) {
                        if (auto a = OfferContextMenuItems(o, !info.rentedByMe, info.favorite); a != OfferAction::None) {
                            result.action = a;
                            result.index = idx;
                        }
                        ImGui::EndPopup();
                    }
                } else if (c == ColRent) {
                    if (cfg.actionLabel) {
                        if (*cfg.actionLabel && ImGui::SmallButton(cfg.actionLabel)) {
                            result.action = OfferAction::ListAction;
                            result.index = idx;
                        }
                    } else if (info.rentedByMe) {
                        ImGui::BeginDisabled();
                        ImGui::PushStyleColor(ImGuiCol_Text, p.info);
                        ImGui::SmallButton("RENTED");
                        ImGui::PopStyleColor();
                        ImGui::EndDisabled();
                    } else if (info.renting) {
                        SpinnerText("##renting", "renting");
                    } else if (ImGui::SmallButton("RENT")) {
                        result.action = cfg.rentNow ? OfferAction::RentNow : OfferAction::Rent;
                        result.index = idx;
                    }
                } else if (c == ColTflopsPerDollar && m.rankTflopsPerDollar) {
                    TextWithColor(rankColor(*m.rankTflopsPerDollar), textCell(c, o, m, cur).c_str());
                } else if (c == ColDlpPerDollar && m.rankDlpPerDollar) {
                    TextWithColor(rankColor(*m.rankDlpPerDollar), textCell(c, o, m, cur).c_str());
                } else if (c == ColVerification) {
                    TextWithColor(verificationColor(o.verification), o.verification.c_str());
                    if (defective) {
                        ImGui::SameLine();
                        TextWithColor(p.danger, "defective");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(d.reasons, "\n").c_str());
                    }
                    if (d.blacklisted()) {
                        ImGui::SameLine();
                        TextWithColor(p.textDim, "blacklisted");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fmt::join(d.blacklistReasons, "\n").c_str());
                    }
                } else if (c == ColPrice) {
                    ImGui::TextUnformatted(textCell(c, o, m, cur).c_str());
                    if (ImGui::IsItemHovered()) OfferPriceTooltip(o, cur, cfg.diskGb);
                } else if (c == ColGpu && incompatible) {
                    TextWithColor(p.danger, textCell(c, o, m, cur).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Incompatible with the template:\n%s", info.incompatibleWhy.c_str());
                } else if (c == ColCuda && incompatible && info.incompatibleWhy.find("cuda_max_good") != std::string::npos) {
                    TextWithColor(p.danger, textCell(c, o, m, cur).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.incompatibleWhy.c_str());
                } else {
                    ImGui::TextUnformatted(textCell(c, o, m, cur).c_str());
                }
                if (c == ColGpu && defective && !incompatible && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", fmt::join(d.reasons, "\n").c_str());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return result;
}

}  // namespace vast::ui
