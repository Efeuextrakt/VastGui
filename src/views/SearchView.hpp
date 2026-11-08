#pragma once

#include "logic/DefectFilter.hpp"
#include "logic/GpuCatalog.hpp"
#include "logic/Metrics.hpp"
#include "model/Offer.hpp"
#include "model/Template.hpp"
#include "views/Resource.hpp"
#include "widgets/OfferCard.hpp"
#include "widgets/Widgets.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vast {

class App;

class SearchView {
public:
    void init(App& app);
    void draw(App& app);
    void drawFloating(App& app);
    void onTemplateChanged(App& app);
    // Re-evaluates defects/visibility of the current results (no new request).
    void onBlacklistChanged(App& app) { recompute(app); }
    // Opens the template picker on a tab (0 recommended, 1 my templates, 2 search; -1 = default).
    void openTemplatePicker(int tab) {
        tplTab_ = tab < 0 || tab > 2 ? -1 : tab;
        openTemplatePicker_ = true;
    }
    // Pre-selects an existing volume to link on the next rental.
    void preselectVolume(App& app, std::int64_t volumeId);

private:
    struct RentState {
        Offer offer;
        std::string templateName;
        std::string templateFilters;  // template extra_filters (machine requirements)
        std::string image;
        std::string templateHash;
        bool useTemplateHash = false;
        double diskGb = 16.0;
        std::string label;
        std::vector<std::pair<std::string, std::string>> env;
        std::vector<std::string> ports;
        std::string extraDocker;
        std::string onstart;
        int launchMode = 0;  // 0 ssh, 1 jupyter, 2 args
        bool direct = true;
        std::string args;
        double bidPrice = 0.0;
        int volumeMode = 0;  // 0 none, 1 link existing, 2 create new
        std::int64_t volumeId = 0;
        double volumeSizeGb = 50.0;
        std::string mountPath = "/data";
        bool cancelUnavail = false;
        bool submitting = false;
        bool done = false;
    };

    void search(App& app);
    void recompute(App& app);
    void updateTemplateRequirements(App& app);
    void filtersChanged(App& app, bool immediate = false);
    void handleAction(App& app, ui::OfferAction a, const Offer& o);
    void openRent(App& app, const Offer& o);

    void drawTopBar(App& app);
    void drawFilterPanel(App& app);
    void drawResults(App& app);
    void drawCards(App& app);
    void drawTable(App& app);
    void drawRentModal(App& app);
    void drawTemplatePicker(App& app);
    void drawTemplateEditor(App& app);
    void drawPresetPopups(App& app);

    std::vector<Offer> offers_;
    std::vector<OfferMetrics> metrics_;
    std::vector<DefectInfo> defects_;
    std::vector<std::size_t> visible_;
    int defectiveCount_ = 0;
    int blacklistedCount_ = 0;
    int hiddenDeverified_ = 0;
    int hiddenIncompatible_ = 0;
    int hiddenRented_ = 0;
    std::set<std::int64_t> rentedMachines_;  // machines of the user's instances (all states)
    std::size_t receivedOffers_ = 0;  // offers in all responses of the last search, before merging
    int mergedRequests_ = 0;
    std::vector<std::size_t> tableRows_;  // visible_ in table sort order (cached)
    bool tableRowsDirty_ = true;
    int tableSortCol_ = -1;
    bool tableSortDesc_ = false;
    bool loading_ = false;
    std::string error_;
    std::vector<std::string> warnings_;
    double lastSearchAt_ = -1.0;
    double changedAt_ = -1.0;
    bool pendingSearch_ = false;
    bool filterPanelOpen_ = true;
    std::string gpuFilterText_;
    // GPU dropdown: model list from vast.ai (/gpu_types/, cached in Settings::gpuModels), grouped and sorted.
    void requestGpuTypes(App& app);
    void rebuildGpuCatalog(App& app);
    bool gpuTypesRequested_ = false;
    bool gpuCatalogDirty_ = true;
    std::vector<GpuGroup> gpuCatalog_;
    std::map<std::string, std::string> gpuSearchNames_;  // vast name -> normalised name (search matches both)
    std::string regionFilterText_;

    RentState rent_;
    bool openRentPopup_ = false;
    bool openTemplatePicker_ = false;
    bool openTemplateEditor_ = false;
    bool openSavePreset_ = false;
    std::string presetName_;
    std::string selectedPreset_;  // preset picked in the filter panel's dropdown

    void loadTemplateTab(App& app, int tab, bool force);
    void prepareTemplatePicker(App& app);
    void applyTemplateToRent(App& app, const Template& t);
    bool pickerForRent_ = false;  // template picker opened from the Rent dialog (applies to that rental only)

    std::array<Resource, 3> tplRes_;               // 0 recommended, 1 mine, 2 search
    std::array<ui::JsonTableState, 3> tplTables_;
    int tplTab_ = -1;          // -1: decided on first open (My templates when an API key is set)
    int tplTabRequest_ = -1;  // tab to select programmatically on the next frame
    std::string templateQuery_;
    Template editTemplate_;

    Resource volumesRes_;
    std::vector<std::pair<std::int64_t, json>> jsonWindows_;
    bool tableColumnsApplied_ = false;
    bool syncTableSort_ = true;
    float cardHeight_ = 0.0f;
};

}  // namespace vast
