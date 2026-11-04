#pragma once

#include "imgui.h"
#include "imgui_stdlib.h"
#include "model/Json.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vast::ui {

void setMonoFont(ImFont* f);
[[nodiscard]] ImFont* monoFont();

bool IconButton(const char* icon, const char* tooltip = nullptr, const ImVec4* color = nullptr);
// Button with a leading icon. While `busy`, the icon is replaced by a spinner of the
// same footprint, so the button never changes size and the layout never shifts.
bool BusyButton(const char* icon, const char* text, bool busy, bool primary = true, const ImVec2& size = ImVec2(0, 0));
bool BusyIconButton(const char* icon, bool busy, const char* tooltip = nullptr);
bool SecondaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));
bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));
bool ToggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0, 0));
bool ToggleSwitch(const char* label, bool* v);
// Checkbox with a box about the size of a tree node's arrow (for dense lists, e.g. the GPU picker).
bool SmallCheckbox(const char* label, bool* v);
// Collapsible group header for dropdown lists (arrow + label, same hover highlight as list rows). The open state
// is kept per window under strId; forceOpen shows it open without changing that state. Returns whether it is open
// (the caller indents its rows itself).
bool GroupHeader(const char* strId, const char* label, bool forceOpen = false);
// Row selector for tables: call in the row's first cell instead of Selectable(SpanAllColumns). The hover / selected
// highlight covers the whole row (full height, rounded corners, drawn behind the cells), and a click anywhere on
// the row counts. IsItemHovered() / BeginPopupContextItem() afterwards refer to the row.
bool TableRowSelectable(const char* label, bool selected);
void HelpMarker(const char* text);
// Visible vertical drag separator between two side-by-side panels. Updates *leftWidth while dragging
// (clamped to [minLeft, maxLeft]); returns true on the frame the drag ends.
bool VSplitter(const char* id, float* leftWidth, float minLeft, float maxLeft);
[[nodiscard]] float VSplitterWidth();
// Error shown on the current line (truncated, full text in a tooltip); renders nothing when empty.
void InlineError(const std::string& error, std::size_t maxChars = 90);
void TextDim(const char* text);
// Shortens `s` with "..." to fit `maxWidth` pixels in the current font.
[[nodiscard]] std::string Ellipsize(const std::string& s, float maxWidth);
void TextWithColor(const ImVec4& c, const char* text);
// Label on the left, value right-aligned within the given width (or remaining width).
void KeyValue(const char* key, const std::string& value, float width = 0.0f);
// Inline "copy" icon that copies `text` to the clipboard.
bool CopyIcon(const char* id, const std::string& text);
void SectionTitle(const char* icon, const char* title);
bool BeginCard(const char* id, const ImVec2& size = ImVec2(0, 0), bool hoverHighlight = false);
void EndCard();
void PageHeader(const char* icon, const char* title, const char* subtitle = nullptr);

// Collapsible JSON tree.
void JsonTree(const json& j, const char* rootLabel = nullptr);
// Pretty-printed JSON in a scrolling monospace child.
void JsonText(const char* id, const json& j, const ImVec2& size = ImVec2(0, 0));
void MonoText(const char* id, const std::string& text, const ImVec2& size = ImVec2(0, 0), bool autoScroll = false);
// Monospace log viewer with a line-number gutter. Numbers are display-only (copy gives the plain text).
void LogText(const char* id, const std::string& text, const ImVec2& size = ImVec2(0, 0), bool autoScroll = true);

// Generic sortable/filterable table for JSON arrays of objects.
struct JsonTableState {
    std::string filter;
    int sortColumn = -1;
    bool sortDesc = false;
    int selected = -1;
    std::vector<std::string> columns;  // cached column order
    std::vector<float> widths;         // initial column widths (from sampled content)
    std::size_t lastSize = static_cast<std::size_t>(-1);
};

struct JsonTableOptions {
    std::vector<std::string> preferredColumns;
    std::size_t maxColumns = 40;
    float height = 0.0f;  // 0 = fill
    bool showFilter = true;
    std::function<void(const json& row, int index)> rowMenu;   // extra context menu items
    std::function<std::string(const std::string& col, const json& value)> formatCell;
};

// Returns index of the row double-clicked this frame, if any.
std::optional<int> JsonTable(const char* id, const json& rows, JsonTableState& state, const JsonTableOptions& opts = {});

// Simple yes/no modal. Call every frame; returns true once when confirmed.
bool ConfirmModal(const char* id, const std::string& message, const char* confirmLabel = "Confirm", bool danger = true);

// Modal dialog with a close button (x) that also closes on a click anywhere outside it. Use like BeginPopupModal
// (call ImGui::EndPopup() only when it returns true). *closed is set on the frame the user closed it.
bool BeginDialog(const char* name, ImGuiWindowFlags flags = 0, bool* closed = nullptr);
// True on the frame the mouse was clicked outside the current window and the popups/children it opened.
[[nodiscard]] bool ClickedOutsideCurrentWindow();

}  // namespace vast::ui
