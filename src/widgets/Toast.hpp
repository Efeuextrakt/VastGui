#pragma once

#include <string>
#include <vector>

namespace vast::ui {

enum class ToastKind { Info, Success, Warning, Error };

// Stack of transient notifications rendered in the bottom-right corner.
class Toasts {
public:
    void push(ToastKind kind, std::string title, std::string body = {}, float seconds = 5.0f);
    void info(std::string title, std::string body = {}) { push(ToastKind::Info, std::move(title), std::move(body)); }
    void success(std::string title, std::string body = {}) { push(ToastKind::Success, std::move(title), std::move(body), 3.5f); }
    void warning(std::string title, std::string body = {}) { push(ToastKind::Warning, std::move(title), std::move(body), 7.0f); }
    void error(std::string title, std::string body = {}) { push(ToastKind::Error, std::move(title), std::move(body), 9.0f); }
    void draw();

private:
    struct Item {
        ToastKind kind;
        std::string title;
        std::string body;
        double created;
        float seconds;
        int id;
    };
    std::vector<Item> items_;
    int nextId_ = 1;
};

}  // namespace vast::ui
