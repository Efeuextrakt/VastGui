#pragma once

#include <cstdint>
#include <string>

namespace vast {

class App;

class SettingsView {
public:
    void draw(App& app);

private:
    void drawAccount(App& app);
    void drawBackend(App& app);
    void drawAppearance(App& app);
    void drawSearch(App& app);
    void drawBlacklist(App& app);
    void drawAbout(App& app);

    std::string keyInput_;
    bool keyInputInit_ = false;
    bool showKey_ = false;
    bool testing_ = false;
    std::string testResult_;
    bool testOk_ = false;
    std::int64_t addMachine_ = 0;
    std::int64_t addHost_ = 0;
    float pendingFontSize_ = 15.0f;
    float pendingUiScale_ = 1.0f;
    bool pendingInit_ = false;
};

}  // namespace vast
