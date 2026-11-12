#pragma once

#include "logic/DefectFilter.hpp"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "model/Offer.hpp"

namespace vast::ui {

enum class OfferAction { None, Rent, RentNow, CopyOfferId, CopyMachineId, CopyHostId, HideHost, HideMachine, ToggleFavorite, ViewJson };

struct OfferCardContext {
    const CurrencyFormat* currency = nullptr;
    bool incompatible = false;    // template needs a newer CUDA than this machine supports
    bool rentedByMe = false;      // the user already has an instance on this machine
    bool rentNow = false;         // RENT rents at once (on-demand); otherwise it opens the Rent dialog
    bool renting = false;         // a one-click rental of this offer is in flight
    bool favorite = false;        // the machine is on the user's favorites
    std::string incompatibleWhy;
    bool interruptible = false;   // price is the suggested bid
    std::optional<double> diskGb;  // disk size the price includes (for the price tooltip)
    float height = 0.0f;
    float* measuredHeight = nullptr;  // receives max(content height) so callers can size cards to fit
};

// Initial card height estimate for the current font (cards share one height for clipping).
[[nodiscard]] float OfferCardHeight();
OfferAction OfferCard(const Offer& o, const OfferMetrics& m, const DefectInfo& d, const OfferCardContext& ctx);

// Shared tooltip / context menu bits used by both card and table views.
void OfferPriceTooltip(const Offer& o, const CurrencyFormat& c, std::optional<double> diskGb = std::nullopt);
OfferAction OfferContextMenuItems(const Offer& o, bool canRent = true, bool favorite = false);

}  // namespace vast::ui
