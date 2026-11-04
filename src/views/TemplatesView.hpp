#pragma once

#include "backend/Commands.hpp"
#include "views/Resource.hpp"
#include "widgets/Widgets.hpp"

#include <array>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace vast {

class App;

// Loads the current user's templates (created by you + private), merged and de-duplicated.
void fetchMyTemplates(App& app, Resource& res);

class TemplatesView {
public:
    void draw(App& app);

private:
    enum Scope { kRecommended = 0, kMine = 1, kSearch = 2 };

    void load(App& app, int scope, bool force);
    void reloadAfterChange(App& app);
    void drawForm(App& app);
    void editFrom(const json& row, bool asNew);
    void useTemplate(App& app, const json& row);
    void rowMenu(App& app, const json& row);
    void drawCards(App& app, const json& rows);
    void drawCard(App& app, const json& row, int index, const ImVec2& size);

    std::array<Resource, 3> res_;  // indexed by Scope
    std::array<ui::JsonTableState, 3> tables_;
    std::vector<std::size_t> cardOrder_;  // sorted row indices for the card view
    std::tuple<int, double, std::size_t, std::string, bool> cardOrderKey_{-1, 0.0, 0, {}, false};
    std::string query_;
    int scope_ = -1;  // decided on first draw: My templates when an API key is set
    int scopeRequest_ = -1;
    bool formOpen_ = false;
    bool editing_ = false;  // update vs create
    std::string editHash_;
    cmd::TemplateParams form_;
    std::vector<std::pair<std::string, std::string>> env_;
    std::vector<std::string> ports_;
    std::string extra_;
    int launchMode_ = 0;
    json pendingDelete_;
    bool openDelete_ = false;
};

}  // namespace vast
