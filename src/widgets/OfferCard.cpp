#include "widgets/OfferCard.hpp"

#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui.h"
#include "logic/Regions.hpp"
#include "widgets/Badge.hpp"
#include "widgets/Widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>

namespace vast::ui {

namespace {

void dimLine(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}

void iconLine(const char* icon, const std::string& s, bool dim = false) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, ImGui::GetFontSize() * 0.45f);
    if (dim) {
        dimLine(s);
    } else {
        ImGui::TextUnformatted(s.c_str());
    }
}

void metricLine(const char* label, std::optional<double> v, int decimals, std::optional<float> rank) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const std::string s = formatMetric(v, decimals);
    if (rank) {
        TextWithColor(rankColor(*rank), s.c_str());
    } else {
        ImGui::TextUnformatted(s.c_str());
    }
}

}  // namespace

// Initial estimate only; SearchView switches to the measured content height after the first frame.
float OfferCardHeight() { return ImGui::GetTextLineHeightWithSpacing() * 6.0f + ImGui::GetStyle().WindowPadding.y * 2.0f; }

void OfferPriceTooltip(const Offer& o, const CurrencyFormat& c, std::optional<double> diskGb) {
    if (!ImGui::BeginTooltip()) return;
    ImGui::TextUnformatted("Price breakdown");
    ImGui::Separator();
    auto row = [&](const char* k, const std::string& v) {
        TextDim(k);
        ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
        ImGui::TextUnformatted(v.c_str());
    };
    const std::optional<double> gpu = o.gpuCostPerHour ? o.gpuCostPerHour : o.dphBase;
    row(o.bidMarkupPerHour > 0.0 ? "GPU (min bid)" : "GPU", fmt::money(gpu, c, 4) + "/hr");
    if (o.bidMarkupPerHour > 0.0) row("  suggested bid +5 %", fmt::money(o.bidMarkupPerHour, c, 4) + "/hr");
    const std::string storage = diskGb ? std::format("Storage ({:.0f} GB)", *diskGb) : std::string("Storage");
    row(storage.c_str(), fmt::money(o.diskCostPerHour ? o.diskCostPerHour : o.storageTotalCostHr, c, 4) + "/hr");
    if (o.storageCostGbMonth) row("  per GB", fmt::money(*o.storageCostGbMonth, c, 3) + "/month");
    if (o.volumePerHour > 0.0) row("New volume", fmt::money(o.volumePerHour, c, 4) + "/hr");
    if (o.discountedPerHour && o.dphTotal && *o.dphTotal - *o.discountedPerHour > 1e-6) {
        row("Discount", "-" + fmt::money(*o.dphTotal - *o.discountedPerHour, c, 4) + "/hr");
    }
    if (o.slaPremiumPerHour && *o.slaPremiumPerHour > 0.0) row("SLA premium", fmt::money(*o.slaPremiumPerHour, c, 4) + "/hr");
    ImGui::Separator();
    row("Total", fmt::money(o.priceHr, c, 4) + "/hr");
    ImGui::Separator();
    TextDim("Bandwidth (billed separately)");
    row("Upload", o.inetUpCostGb ? fmt::money(*o.inetUpCostGb * 1000.0, c, 2) + "/TB" : std::string(kMissing));
    row("Download", o.inetDownCostGb ? fmt::money(*o.inetDownCostGb * 1000.0, c, 2) + "/TB" : std::string(kMissing));
    ImGui::EndTooltip();
}

OfferAction OfferContextMenuItems(const Offer& o, bool canRent) {
    OfferAction a = OfferAction::None;
    if (ImGui::MenuItem(ICON_FA_ROCKET "  Rent...", nullptr, false, canRent)) a = OfferAction::Rent;
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy offer ID", std::to_string(o.id).c_str())) a = OfferAction::CopyOfferId;
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy machine ID", o.machineId ? std::to_string(*o.machineId).c_str() : nullptr,
                        false, o.machineId.has_value())) {
        a = OfferAction::CopyMachineId;
    }
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy host ID", o.hostId ? std::to_string(*o.hostId).c_str() : nullptr, false,
                        o.hostId.has_value())) {
        a = OfferAction::CopyHostId;
    }
    if (ImGui::MenuItem(ICON_FA_CODE "  View raw JSON")) a = OfferAction::ViewJson;
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_EYE_SLASH "  Hide this host", nullptr, false, o.hostId.has_value())) a = OfferAction::HideHost;
    if (ImGui::MenuItem(ICON_FA_BAN "  Hide this machine", nullptr, false, o.machineId.has_value())) {
        a = OfferAction::HideMachine;
    }
    return a;
}

OfferAction OfferCard(const Offer& o, const OfferMetrics& m, const DefectInfo& d, const OfferCardContext& ctx) {
    const Palette& p = palette();
    const CurrencyFormat cur = ctx.currency ? *ctx.currency : CurrencyFormat{};
    OfferAction action = OfferAction::None;
    const float height = ctx.height > 0 ? ctx.height : OfferCardHeight();

    ImGui::PushID(static_cast<int>(o.id));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.card);
    // Rented by you: bluish border; defective: reddish; blacklisted: grey (the card is not dimmed).
    const ImVec4 border = ctx.rentedByMe     ? p.info
                          : d.defective()   ? ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.6f)
                          : d.blacklisted() ? ImVec4(p.textDim.x, p.textDim.y, p.textDim.z, 0.7f)
                                            : p.border;
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, ctx.rentedByMe ? std::max(1.5f, ImGui::GetStyle().ChildBorderSize * 1.5f)
                                                                       : ImGui::GetStyle().ChildBorderSize);
    ImGui::BeginChild("##card", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    const bool cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (cardHovered) {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), col(p.accent, 0.05f), 8.0f);
    }

    const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##cols", 4, tf)) {
        ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("hw", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("perf", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableNextRow();

        // Left: identity + GPU.
        ImGui::TableSetColumnIndex(0);
        dimLine(std::format("m:{}  host:{}", o.machineId.value_or(0), o.hostId.value_or(0)));
        // "Blacklisted" badge on an existing line so the card height never changes; shortened when narrow.
        auto blacklistBadge = [&] {
            if (!d.blacklisted()) return;
            ImGui::SameLine();
            const std::string why = fmt::join(d.blacklistReasons, "\n") + "\n\nManage the blacklist in Settings.";
            const char* label = ImGui::GetContentRegionAvail().x >= BadgeSize("blacklisted").x ? "blacklisted" : ICON_FA_BAN;
            InlineBadge(label, p.textDim, true, why.c_str());
        };
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
        ImGui::Text("%dx %s", o.numGpus, o.gpuName.c_str());
        ImGui::PopFont();
        ImGui::Text("%s TFLOPS", formatMetric(o.totalFlops, 1).c_str());
        ImGui::Text("%s GB/GPU  " ICON_FA_MEMORY "  %s GB total", formatMetric(o.gpuRamGb(), 0).c_str(),
                    formatMetric(o.totalVramGb(), 0).c_str());
        if (ctx.incompatible) {
            InlineBadge("INCOMPATIBLE", p.danger, true, ctx.incompatibleWhy.c_str());
            ImGui::SameLine();
            const bool cudaUnmet = ctx.incompatibleWhy.find("cuda_max_good") != std::string::npos;
            TextWithColor(cudaUnmet ? p.danger : p.textDim, std::format("CUDA {}", formatMetric(o.cudaMaxGood, 1)).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ctx.incompatibleWhy.c_str());
        } else {
            dimLine(std::format("Max CUDA {}", formatMetric(o.cudaMaxGood, 1)));
        }
        blacklistBadge();

        // Middle: machine details.
        ImGui::TableSetColumnIndex(1);
        const std::string cc = o.countryCode();
        iconLine(ICON_FA_LOCATION_DOT, o.geolocation.empty() ? std::string(kMissing)
                                                             : std::format("{}  {}", cc.empty() ? "??" : cc,
                                                                           cc.empty() ? o.geolocation : countryName(cc)));
        iconLine(ICON_FA_MICROCHIP, std::format("{}  " ICON_FA_RIGHT_LEFT " PCIe {} {}x  {} GB/s",
                                               o.moboName.empty() ? "unknown board" : o.moboName,
                                               formatMetric(o.pciGen, 1), formatMetric(o.gpuLanes, 0),
                                               formatMetric(o.pcieBw, 1)),
                 true);
        std::string cpu = o.cpuName.empty() ? std::string("CPU") : o.cpuName;
        for (const char* noise : {" Processor", "(R)", "(TM)", " CPU"}) {
            if (const auto pos = cpu.find(noise); pos != std::string::npos) cpu.erase(pos, std::strlen(noise));
        }
        if (const auto pos = cpu.find("-Core"); pos != std::string::npos) {
            auto start = pos;
            while (start > 0 && std::isdigit(static_cast<unsigned char>(cpu[start - 1]))) --start;
            if (start > 0 && cpu[start - 1] == ' ') --start;
            cpu.erase(start, pos + 5 - start);
        }
        if (cpu.size() > 28) cpu = cpu.substr(0, 27) + "...";
        iconLine(ICON_FA_GAUGE_HIGH, std::format("{}  {}/{} cpu  {} GB RAM", cpu, formatMetric(o.cpuCoresEffective, 1),
                                                formatMetric(o.cpuCores, 0), formatMetric(o.cpuRamGb(), 0)));
        iconLine(ICON_FA_NETWORK_WIRED, std::format(ICON_FA_UPLOAD " {} " ICON_FA_DOWNLOAD " {} Mbps   {} ports",
                                                   formatMetric(o.inetUp, 0), formatMetric(o.inetDown, 0),
                                                   formatMetric(o.directPortCount, 0)));
        iconLine(ICON_FA_HARD_DRIVE, std::format("{}  {} MB/s  {} GB", o.diskName.empty() ? "disk" : o.diskName,
                                                formatMetric(o.diskBw, 0), formatMetric(o.diskSpace, 0)),
                 true);
        dimLine(std::format("Driver {}  {}  {}", o.driverVersion.empty() ? kMissing : o.driverVersion.c_str(),
                            o.osVersion.empty() ? "" : ("Ubuntu " + o.osVersion).c_str(),
                            o.staticIp ? "static IP" : ""));

        // Performance (higher per-dollar values are better and colour-coded within the result set).
        ImGui::TableSetColumnIndex(2);
        metricLine("DLPerf", o.dlperf, 1, std::nullopt);
        metricLine("TFLOPS/$/hr", m.tflopsPerDollar, 1, m.rankTflopsPerDollar);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("TFLOPS per $/hr of the shown price - higher is better");
        metricLine("DLP/$/hr", m.dlpPerDollar, 1, m.rankDlpPerDollar);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("DLPerf per $/hr of the shown price - higher is better");

        // Right: status + price.
        ImGui::TableSetColumnIndex(3);
        const std::string ver = o.verification.empty() ? "unverified" : o.verification;
        Badge(ver.c_str(), verificationColor(ver));
        if (o.datacenter) {
            ImGui::SameLine();
            Badge(ICON_FA_SHIELD_HALVED " secure", p.info, false, "Secure cloud / datacenter host");
        }
        if (d.defective()) {
            ImGui::SameLine();
            const std::string why = fmt::join(d.reasons, "\n");
            Badge("defective", p.danger, false, why.c_str());
        }
        dimLine(std::format(ICON_FA_CLOCK " {}  " ICON_FA_HEART_PULSE " {}",
                            o.durationSec ? fmt::humanDuration(*o.durationSec) : std::string(kMissing),
                            fmt::percent(o.reliability, 2)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Max duration / host reliability\nOffer ID %lld", static_cast<long long>(o.id));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
        ImGui::TextUnformatted((fmt::money(o.priceHr, cur) + "/hr").c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) OfferPriceTooltip(o, cur, ctx.diskGb);
        dimLine(ctx.interruptible ? "suggested bid  + bandwidth" : "+ bandwidth");
        if (ImGui::IsItemHovered()) OfferPriceTooltip(o, cur, ctx.diskGb);
        if (ctx.rentedByMe) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.info.x, p.info.y, p.info.z, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_Text, p.info);
            ImGui::BeginDisabled();
            ImGui::Button(ICON_FA_USER_CHECK "  RENTED", ImVec2(-FLT_MIN, 0));
            ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Rented by you: you already have an instance on this machine.");
            }
        } else if (ImGui::Button(ICON_FA_ROCKET "  RENT", ImVec2(-FLT_MIN, 0))) {
            action = OfferAction::Rent;
        }
        ImGui::EndTable();
    }
    if (ctx.measuredHeight) {
        // Content height in this child (cursor started at WindowPadding.y) plus the bottom padding.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float h = ImGui::GetCursorPosY() - style.ItemSpacing.y + style.WindowPadding.y;
        *ctx.measuredHeight = std::max(*ctx.measuredHeight, h);
    }

    if (ImGui::BeginPopupContextWindow("##offerctx")) {
        if (auto a = OfferContextMenuItems(o, !ctx.rentedByMe); a != OfferAction::None) action = a;
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return action;
}

}  // namespace vast::ui
