#pragma once
// Offer table shared by the Search page and the Favorites & Blacklist page: sortable, resizable, reorderable and
// hideable columns, the same cells, colours, tooltips and right-click menu as the offer cards.

#include "logic/DefectFilter.hpp"
#include "logic/Format.hpp"
#include "logic/Metrics.hpp"
#include "logic/SearchFilters.hpp"
#include "model/Offer.hpp"
#include "widgets/OfferCard.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vast::ui {

// What the caller knows about a row (own rentals, favorites, the template).
struct OfferRowInfo {
    bool rentedByMe = false;
    bool favorite = false;
    bool renting = false;          // a one-click rental is in flight
    std::string incompatibleWhy;   // non-empty: the machine does not meet the template's requirements
};

struct OfferTableConfig {
    const CurrencyFormat* currency = nullptr;
    std::optional<double> diskGb;       // disk size the prices include (price tooltip)
    bool rentNow = false;               // RENT rents at once (RentNow) instead of opening the dialog (Rent)
    const char* actionLabel = nullptr;  // replaces RENT (returns ListAction); "" = no button
    float height = 0.0f;                // 0 = fill the remaining space
    float scale = 1.0f;                 // UI scale for the default column widths
    std::vector<bool>* columns = nullptr;  // column visibility, kept in sync (saved by the caller)
    bool* columnsChanged = nullptr;        // set when the user showed / hid a column
    std::function<OfferRowInfo(std::size_t index)> rowInfo;
};

struct OfferTableResult {
    OfferAction action = OfferAction::None;
    std::size_t index = 0;           // row (offer index) of `action`
    std::optional<SortKey> sortKey;  // the header sort changed to a column vast.ai can sort by
    bool sortDesc = false;
};

class OfferTable {
public:
    // Draws offers[rows] (rows are indices into offers / metrics / defects). `id` keeps ImGui's saved column
    // widths and order apart per table.
    OfferTableResult draw(const char* id, std::span<const Offer> offers, std::span<const OfferMetrics> metrics,
                          std::span<const DefectInfo> defects, const std::vector<std::size_t>& rows,
                          const OfferTableConfig& cfg);
    // Shows `key` as the header sort on the next draw (no sort arrow when no column matches).
    void setSort(SortKey key, bool desc);
    // The offers changed (the row order is kept until then; changed `rows` are detected automatically).
    void invalidate() { rowsDirty_ = true; }

    [[nodiscard]] static std::vector<bool> defaultColumns();
    // Height of a table showing `rows` rows (header and horizontal scrollbar included).
    [[nodiscard]] static float heightForRows(std::size_t rows);

private:
    bool columnsApplied_ = false;
    std::vector<bool> appliedColumns_;
    std::optional<std::pair<SortKey, bool>> pendingSort_;
    std::vector<std::size_t> sorted_;
    std::vector<std::size_t> lastRows_;
    bool rowsDirty_ = true;
    int sortCol_ = -1;
    bool sortDesc_ = false;
};

}  // namespace vast::ui
