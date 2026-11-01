#pragma once

#include "logic/DefectFilter.hpp"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "model/Offer.hpp"

namespace vast::ui {

enum class OfferAction { None, Rent, CopyOfferId, CopyMachineId, CopyHostId, HideHost, HideMachine, ViewJson };

struct OfferCardContext {
    const CurrencyFormat* currency = nullptr;
    bool greyed = false;          // defective offer shown on request
    bool incompatible = false;    // template needs a newer CUDA than this machine supports
    bool rentedByMe = false;      // the user already has an instance on this machine
    std::string incompatibleWhy;
    bool interruptible = false;   // show min bid instead of on-demand price
    float height = 0.0f;
    float* measuredHeight = nullptr;  // receives max(content height) so callers can size cards to fit
};

// Initial card height estimate for the current font (cards share one height for clipping).
[[nodiscard]] float OfferCardHeight();
OfferAction OfferCard(const Offer& o, const OfferMetrics& m, const DefectInfo& d, const OfferCardContext& ctx);

// Shared tooltip / context menu bits used by both card and table views.
void OfferPriceTooltip(const Offer& o, const CurrencyFormat& c);
OfferAction OfferContextMenuItems(const Offer& o);

}  // namespace vast::ui
