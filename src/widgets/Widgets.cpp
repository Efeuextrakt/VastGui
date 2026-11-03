#include "widgets/Widgets.hpp"

#include "IconsFontAwesome6.h"
#include "Theme.hpp"
#include "imgui_internal.h"
#include "logic/Format.hpp"
#include "widgets/Spinner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>

namespace vast::ui {

namespace {
ImFont* g_mono = nullptr;
}

void setMonoFont(ImFont* f) { g_mono = f; }
ImFont* monoFont() { return g_mono; }

bool IconButton(const char* icon, const char* tooltip, const ImVec4* color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lerpColor(palette().panel, palette().accent, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, lerpColor(palette().panel, palette().accent, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Text, color ? *color : palette().textDim);
    const bool r = ImGui::Button(icon);
    ImGui::PopStyleColor(4);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return r;
}

bool SecondaryButton(const char* label, const ImVec2& size) {
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, p.frame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lerpColor(p.frame, p.accent, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, lerpColor(p.frame, p.accent, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool BusyButton(const char* icon, const char* text, bool busy, bool primary, const ImVec2& sizeArg) {
    const Palette& p = palette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconW = std::max(ImGui::CalcTextSize(icon).x, ImGui::GetFontSize());
    const float gap = ImGui::GetFontSize() * 0.45f;
    const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, true);
    ImVec2 size(iconW + gap + textSize.x + style.FramePadding.x * 2.0f, ImGui::GetFrameHeight());
    if (sizeArg.x > 0) size.x = sizeArg.x;
    if (sizeArg.x < 0) size.x = std::max(size.x, ImGui::GetContentRegionAvail().x + sizeArg.x + 1.0f);
    if (sizeArg.y > 0) size.y = sizeArg.y;

    ImGui::PushID(text);
    ImGui::PushID(icon);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed;
    if (primary) {
        pressed = ImGui::Button("##busybtn", size);
    } else {
        pressed = SecondaryButton("##busybtn", size);
    }
    ImGui::PopID();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float contentW = iconW + gap + textSize.x;
    const float x0 = pos.x + std::max(style.FramePadding.x, (size.x - contentW) * 0.5f);
    const float cy = pos.y + size.y * 0.5f;
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    if (busy) {
        const float r = ImGui::GetFontSize() * 0.36f;
        DrawSpinner(dl, ImVec2(x0 + iconW * 0.5f, cy), r, std::max(1.5f, r * 0.3f), primary ? textCol : col(p.accent));
    } else {
        const ImVec2 is = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(x0 + (iconW - is.x) * 0.5f, cy - is.y * 0.5f), textCol, icon);
    }
    dl->AddText(ImVec2(x0 + iconW + gap, cy - textSize.y * 0.5f), textCol, text, ImGui::FindRenderedTextEnd(text));
    return pressed;
}

bool BusyIconButton(const char* icon, bool busy, const char* tooltip) {
    const float w = ImGui::GetFrameHeight();
    ImGui::PushID(icon);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lerpColor(palette().panel, palette().accent, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, lerpColor(palette().panel, palette().accent, 0.4f));
    const bool pressed = ImGui::Button("##busyicon", ImVec2(w, w));
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(pos.x + w * 0.5f, pos.y + w * 0.5f);
    if (busy) {
        const float r = ImGui::GetFontSize() * 0.36f;
        DrawSpinner(dl, c, r, std::max(1.5f, r * 0.3f), col(palette().accent));
    } else {
        const ImVec2 is = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(c.x - is.x * 0.5f, c.y - is.y * 0.5f), col(palette().textDim), icon);
    }
    return pressed;
}

bool DangerButton(const char* label, const ImVec2& size) {
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, lerpColor(p.danger, p.bg, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, lerpColor(p.danger, p.bg, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool ToggleButton(const char* label, bool active, const ImVec2& size) {
    if (active) return ImGui::Button(label, size);
    return SecondaryButton(label, size);
}

namespace {

// A text-line-high list row that looks and behaves like ImGui::Selectable: it spans the available width, reacts
// to the mouse on the row extended by half the item spacing on each side (no dead gap between rows) and draws the
// same rounded hover highlight. Used by the GPU picker's checkboxes and group headers so every dropdown row
// highlights identically.
struct ListRow {
    ImVec2 pos;  // top-left of the text line
    bool visible = false;
    bool hovered = false;
    bool clicked = false;
};

ListRow listRow(const char* strId, float contentWidth) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    ListRow r;
    r.pos = window->DC.CursorPos;
    const float h = ImGui::GetTextLineHeight();
    ImGui::ItemSize(ImVec2(contentWidth, h), 0.0f);
    // Same box as Selectable(): row from the cursor to the work rect's right edge, padded by half the spacing.
    ImRect bb(r.pos.x, r.pos.y, std::max(window->WorkRect.Max.x, r.pos.x + contentWidth), r.pos.y + h);
    const float spacingL = IM_TRUNC(style.ItemSpacing.x * 0.5f);
    const float spacingU = IM_TRUNC(style.ItemSpacing.y * 0.5f);
    bb.Min.x -= spacingL;
    bb.Min.y -= spacingU;
    bb.Max.x += style.ItemSpacing.x - spacingL;
    bb.Max.y += style.ItemSpacing.y - spacingU;
    const ImGuiID id = window->GetID(strId);
    if (!ImGui::ItemAdd(bb, id)) return r;
    r.visible = true;
    bool held = false;
    r.clicked = ImGui::ButtonBehavior(bb, id, &r.hovered, &held);
    if (r.hovered) {
        window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(held ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered),
                                        style.SelectableRounding);
    }
    return r;
}

}  // namespace

bool SmallCheckbox(const char* label, bool* v) {
    const Palette& p = palette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float box = std::floor(ImGui::GetFontSize() * 0.77f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float gap = style.ItemInnerSpacing.x;
    const ListRow row = listRow(label, box + gap + ImGui::CalcTextSize(label, end).x);
    if (!row.visible) return false;
    if (row.clicked) *v = !*v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Centre the box on the capital letters (cap height ~ 0.7 em) rather than on the full line height, which
    // includes room for descenders the GPU names do not use.
    const float capCentre = row.pos.y + ImGui::GetFontBaked()->Ascent - ImGui::GetFontSize() * 0.35f;
    const ImVec2 a(row.pos.x, std::round(capCentre - box * 0.5f));
    const ImVec2 b(a.x + box, a.y + box);
    const float rounding = std::max(1.0f, box * 0.2f);
    if (*v) {
        dl->AddRectFilled(a, b, col(p.accent), rounding);
        const float t = std::max(1.0f, box * 0.14f);
        const ImVec2 c1(a.x + box * 0.22f, a.y + box * 0.52f), c2(a.x + box * 0.42f, a.y + box * 0.72f),
            c3(a.x + box * 0.78f, a.y + box * 0.28f);
        dl->AddLine(c1, c2, IM_COL32_WHITE, t);
        dl->AddLine(c2, c3, IM_COL32_WHITE, t);
    } else {
        dl->AddRectFilled(a, b, col(p.frame), rounding);
        dl->AddRect(a, b, col(row.hovered ? p.accent : p.border), rounding);
    }
    dl->AddText(ImVec2(b.x + gap, row.pos.y), col(p.text), label, end);
    return row.clicked;
}

bool GroupHeader(const char* strId, const char* label, bool forceOpen) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(strId);
    bool open = storage->GetBool(key, false);
    // Arrow and text placed like an unframed tree node, so the look matches the rest of the UI.
    const float textX = ImGui::GetFontSize() + style.FramePadding.x * 2.0f;
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ListRow row = listRow(strId, textX + ImGui::CalcTextSize(label, end).x);
    if (row.clicked && !forceOpen) {
        open = !open;
        storage->SetBool(key, open);
    }
    const bool shown = open || forceOpen;
    if (row.visible) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float f = ImGui::GetFontSize();
        ImGui::RenderArrow(dl, ImVec2(row.pos.x + style.FramePadding.x + f * 0.15f, row.pos.y + f * 0.15f),
                           ImGui::GetColorU32(ImGuiCol_Text), shown ? ImGuiDir_Down : ImGuiDir_Right, 0.70f);
        dl->AddText(ImVec2(row.pos.x + textX, row.pos.y), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    }
    return shown;
}

bool TableRowSelectable(const char* label, bool selected) {
    ImGuiTable* table = ImGui::GetCurrentTable();
    const ImGuiStyle& style = ImGui::GetStyle();
    // The Selectable only handles input; its own highlight (text-high) is made invisible.
    constexpr ImU32 kClear = IM_COL32(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Header, kClear);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kClear);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kClear);
    bool clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
    const bool itemActive = ImGui::IsItemActive();
    const ImGuiID rowKey = ImGui::GetItemID();
    ImGui::PopStyleColor(3);
    if (!table) return clicked;

    // Row heights are only final when the row ends, so the highlight uses the height measured last frame: each
    // call records where its row starts, and the next call (next row of the same table) stores that row's height.
    struct Pending {
        ImGuiID table = 0;
        int frame = -1;
        ImGuiID row = 0;
        float y1 = 0.0f;
    };
    static Pending pending;
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const int frame = ImGui::GetFrameCount();
    if (pending.table == table->ID && pending.frame == frame && table->RowPosY1 > pending.y1) {
        const float h = table->RowPosY1 - pending.y1;
        storage->SetFloat(pending.row, h);
        storage->SetFloat(table->ID, h);  // fallback for rows not measured yet (e.g. the last one)
    }
    pending = {table->ID, frame, rowKey, table->RowPosY1};

    // Hover of the whole row (as tracked by the table, last frame) so the whole row reacts.
    const bool rowHovered = ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
    // Clicks on the row outside the Selectable's text-high box (rows with buttons/badges are taller) count too.
    if (!clicked && rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() &&
        !ImGui::IsAnyItemActive() && ImGui::IsWindowHovered()) {
        clicked = true;
    }
    if (rowHovered || selected) {
        const ImGuiCol c = rowHovered ? ((itemActive || ImGui::IsMouseDown(ImGuiMouseButton_Left)) ? ImGuiCol_HeaderActive
                                                                                                  : ImGuiCol_HeaderHovered)
                                      : ImGuiCol_Header;
        const float fallback = storage->GetFloat(table->ID, ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f);
        const float h = storage->GetFloat(rowKey, fallback);
        // Full row height, slightly inset so neighbouring highlights stay apart; rounded like dropdown rows.
        const float insetX = std::floor(style.CellPadding.x * 0.5f);
        const ImRect& clip = table->BgClipRect;
        const ImVec2 a(clip.Min.x + insetX, table->RowPosY1 + 1.0f);
        const ImVec2 b(clip.Max.x - insetX, table->RowPosY1 + h - 1.0f);
        ImGui::TablePushBackgroundChannel();  // behind the cell contents
        ImGui::GetWindowDrawList()->AddRectFilled(a, b, ImGui::GetColorU32(c), style.SelectableRounding);
        ImGui::TablePopBackgroundChannel();
    }
    return clicked;
}

bool ToggleSwitch(const char* label, bool* v) {
    const Palette& p = palette();
    const float h = ImGui::GetFrameHeight() * 0.72f;
    const float w = h * 1.8f;
    ImGui::PushID(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float yOff = (ImGui::GetFrameHeight() - h) * 0.5f;
    bool changed = false;
    if (ImGui::InvisibleButton("##switch", ImVec2(w, ImGui::GetFrameHeight()))) {
        *v = !*v;
        changed = true;
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(pos.x, pos.y + yOff);
    const ImVec2 b(pos.x + w, pos.y + yOff + h);
    ImVec4 track = *v ? p.accent : p.border;
    if (hovered) track = lerpColor(track, p.text, 0.08f);
    dl->AddRectFilled(a, b, col(track), h * 0.5f);
    const float r = h * 0.5f - 2.0f;
    const float cx = *v ? b.x - h * 0.5f : a.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, a.y + h * 0.5f), r, IM_COL32_WHITE, 20);
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (end != label) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label, end);
        if (ImGui::IsItemClicked()) {
            *v = !*v;
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

void InlineError(const std::string& error, std::size_t maxChars) {
    if (error.empty()) return;
    ImGui::SameLine();
    std::string e = error;
    if (const auto nl = e.find('\n'); nl != std::string::npos) e.resize(nl);
    if (e.size() > maxChars) e = e.substr(0, maxChars - 3) + "...";
    ImGui::AlignTextToFramePadding();
    TextWithColor(palette().danger, (ICON_FA_CIRCLE_XMARK " " + e).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", error.c_str());
}

float VSplitterWidth() { return std::max(8.0f, ImGui::GetFontSize() * 0.7f); }

bool VSplitter(const char* id, float* leftWidth, float minLeft, float maxLeft) {
    const float w = VSplitterWidth();
    const float h = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active && ImGui::GetIO().MouseDelta.x != 0.0f) {
        *leftWidth = std::clamp(*leftWidth + ImGui::GetIO().MouseDelta.x, minLeft, std::max(minLeft, maxLeft));
    }
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = std::floor(pos.x + w * 0.5f) + 0.5f;
    const ImVec4 lineCol = active ? p.accent : (hovered ? lerpColor(p.border, p.accent, 0.6f) : p.border);
    dl->AddLine(ImVec2(cx, pos.y), ImVec2(cx, pos.y + h), col(lineCol), active || hovered ? 2.0f : 1.0f);
    // Grip: a small pill with three dots in the middle, so the separator reads as draggable.
    const float gh = ImGui::GetFontSize() * 2.2f, gw = w - 2.0f;
    const ImVec2 g0(cx - gw * 0.5f, pos.y + (h - gh) * 0.5f), g1(cx + gw * 0.5f, pos.y + (h + gh) * 0.5f);
    dl->AddRectFilled(g0, g1, col(active || hovered ? p.accent : lerpColor(p.border, p.text, 0.15f)), gw * 0.5f);
    for (int i = -1; i <= 1; ++i) {
        dl->AddCircleFilled(ImVec2(cx, pos.y + h * 0.5f + i * gh * 0.25f), 1.6f, col(p.text, active || hovered ? 1.0f : 0.6f), 8);
    }
    return ImGui::IsItemDeactivated();
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void TextDim(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void TextWithColor(const ImVec4& c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void KeyValue(const char* key, const std::string& value, float width) {
    const float start = ImGui::GetCursorPosX();
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    TextDim(key);
    ImGui::SameLine();
    const float vw = ImGui::CalcTextSize(value.c_str()).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), start + width - vw));
    ImGui::TextUnformatted(value.c_str());
}

bool CopyIcon(const char* id, const std::string& text) {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));
    const bool r = IconButton(ICON_FA_COPY, "Copy to clipboard");
    ImGui::PopStyleVar();
    if (r) ImGui::SetClipboardText(text.c_str());
    ImGui::PopID();
    return r;
}

void SectionTitle(const char* icon, const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().accent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
}

bool BeginCard(const char* id, const ImVec2& size, bool hoverHighlight) {
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, hoverHighlight ? p.cardHover : p.card);
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                                        ImGuiWindowFlags_NoScrollbar);
    return open;
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void PageHeader(const char* icon, const char* title, const char* subtitle) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::PushStyleColor(ImGuiCol_Text, palette().accent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.3f);
        TextDim(subtitle);
    }
}

// ---- JSON ------------------------------------------------------------------

void JsonTree(const json& j, const char* rootLabel) {
    std::function<void(const std::string&, const json&)> node = [&](const std::string& key, const json& v) {
        if (v.is_object() || v.is_array()) {
            const std::string label = key + (v.is_array() ? " [" + std::to_string(v.size()) + "]" : " {}");
            if (ImGui::TreeNode(label.c_str())) {
                if (v.is_object()) {
                    for (const auto& [k, child] : v.items()) node(k, child);
                } else {
                    for (std::size_t i = 0; i < v.size(); ++i) node(std::to_string(i), v[i]);
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::BulletText("%s:", key.c_str());
            ImGui::SameLine();
            const std::string s = v.is_string() ? "\"" + v.get<std::string>() + "\"" : jsonutil::scalarToString(v);
            TextWithColor(v.is_string() ? palette().success : (v.is_number() ? palette().info : palette().warning),
                          s.c_str());
        }
    };
    node(rootLabel ? rootLabel : "root", j);
}

void JsonText(const char* id, const json& j, const ImVec2& size) { MonoText(id, j.dump(2), size); }

void MonoText(const char* id, const std::string& text, const ImVec2& size, bool autoScroll) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette().sidebar);
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (g_mono) ImGui::PushFont(g_mono, 0.0f);
        ImGuiListClipper clipper;
        // Clip line-by-line to keep large logs fast.
        std::vector<std::size_t> starts{0};
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') starts.push_back(i + 1);
        }
        clipper.Begin(static_cast<int>(starts.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const std::size_t b = starts[static_cast<std::size_t>(i)];
                const std::size_t e = static_cast<std::size_t>(i) + 1 < starts.size() ? starts[static_cast<std::size_t>(i) + 1] - 1 : text.size();
                ImGui::TextUnformatted(text.data() + b, text.data() + e);
            }
        }
        if (g_mono) ImGui::PopFont();
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        if (ImGui::BeginPopupContextWindow("##monoctx")) {
            if (ImGui::MenuItem(ICON_FA_COPY "  Copy all")) ImGui::SetClipboardText(text.c_str());
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void LogText(const char* id, const std::string& text, const ImVec2& size, bool autoScroll) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette().sidebar);
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (g_mono) ImGui::PushFont(g_mono, 0.0f);
        std::vector<std::size_t> starts{0};
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n' && i + 1 < text.size()) starts.push_back(i + 1);
        }
        const bool empty = text.empty();
        const int lines = empty ? 0 : static_cast<int>(starts.size());
        const std::string widest = std::to_string(std::max(lines, 1));
        const float gutterW = ImGui::CalcTextSize(widest.c_str()).x + ImGui::GetFontSize() * 0.9f;
        const ImU32 numCol = col(palette().textDim, 0.75f);
        const ImU32 sepCol = col(palette().border);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(lines);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const std::size_t b = starts[static_cast<std::size_t>(i)];
                std::size_t e = static_cast<std::size_t>(i) + 1 < starts.size() ? starts[static_cast<std::size_t>(i) + 1] - 1
                                                                                 : text.size();
                if (e > b && text[e - 1] == '\r') --e;
                if (e > b && text[e - 1] == '\n') --e;
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                char num[16];
                std::snprintf(num, sizeof(num), "%d", i + 1);
                const float nw = ImGui::CalcTextSize(num).x;
                // Numbers are drawn, not laid out as text, so they never end up in selections or copies.
                dl->AddText(ImVec2(pos.x + gutterW - ImGui::GetFontSize() * 0.6f - nw, pos.y), numCol, num);
                dl->AddLine(ImVec2(pos.x + gutterW - ImGui::GetFontSize() * 0.3f, pos.y),
                            ImVec2(pos.x + gutterW - ImGui::GetFontSize() * 0.3f, pos.y + ImGui::GetTextLineHeightWithSpacing()),
                            sepCol);
                ImGui::SetCursorScreenPos(ImVec2(pos.x + gutterW, pos.y));
                ImGui::TextUnformatted(text.data() + b, text.data() + e);
            }
        }
        if (g_mono) ImGui::PopFont();
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        if (ImGui::BeginPopupContextWindow("##logctx")) {
            if (ImGui::MenuItem(ICON_FA_COPY "  Copy all (without line numbers)")) ImGui::SetClipboardText(text.c_str());
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

namespace {

std::string cellString(const json& row, const std::string& colName) {
    const json* v = jsonutil::find(row, colName);
    if (!v) return {};
    if (v->is_object() || v->is_array()) {
        std::string s = v->dump();
        if (s.size() > 120) s = s.substr(0, 117) + "...";
        return s;
    }
    return jsonutil::scalarToString(*v);
}

}  // namespace

std::optional<int> JsonTable(const char* id, const json& rows, JsonTableState& state, const JsonTableOptions& opts) {
    std::optional<int> activated;
    ImGui::PushID(id);
    if (!rows.is_array()) {
        if (!rows.is_null()) JsonTree(rows, "result");
        ImGui::PopID();
        return activated;
    }

    if (state.lastSize != rows.size() || state.columns.empty()) {
        state.lastSize = rows.size();
        state.selected = -1;  // indices refer to the previous data set
        state.columns.clear();
        std::set<std::string> seen;
        std::vector<std::string> discovered;
        for (const auto& r : rows) {
            if (!r.is_object()) continue;
            for (const auto& [k, v] : r.items()) {
                if (seen.insert(k).second) discovered.push_back(k);
            }
        }
        for (const auto& c : opts.preferredColumns) {
            if (seen.contains(c)) state.columns.push_back(c);
        }
        for (const auto& c : discovered) {
            if (std::find(state.columns.begin(), state.columns.end(), c) == state.columns.end()) state.columns.push_back(c);
        }
        if (state.columns.size() > opts.maxColumns) state.columns.resize(opts.maxColumns);
        bool scalarRows = !rows.empty() && !rows[0].is_object();
        if (scalarRows) state.columns = {"value"};
        // Fixed-fit columns cannot measure a span-all-columns selectable, so size from sampled content.
        state.widths.clear();
        const float em = ImGui::GetFontSize();
        for (const auto& c : state.columns) {
            float w = ImGui::CalcTextSize(c.c_str()).x + em * 1.5f;
            const std::size_t samples = std::min<std::size_t>(rows.size(), 40);
            for (std::size_t r = 0; r < samples; ++r) {
                std::string s = scalarRows ? jsonutil::scalarToString(rows[r]) : cellString(rows[r], c);
                if (s.size() > 48) s.resize(48);
                w = std::max(w, ImGui::CalcTextSize(s.c_str()).x + em * 0.8f);
            }
            state.widths.push_back(std::clamp(w, em * 3.0f, em * 24.0f));
        }
    }

    if (opts.showFilter) {
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 22.0f));
        ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  Filter rows...", &state.filter);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu rows", rows.size());
    }

    std::vector<int> order;
    order.reserve(rows.size());
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (!state.filter.empty()) {
            const json& r = rows[static_cast<std::size_t>(i)];
            const std::string hay = r.is_object() || r.is_array() ? r.dump() : jsonutil::scalarToString(r);
            if (!fmt::containsCi(hay, state.filter)) continue;
        }
        order.push_back(i);
    }

    const int ncols = static_cast<int>(std::max<std::size_t>(1, state.columns.size()));
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    const float height = opts.height > 0 ? opts.height : ImGui::GetContentRegionAvail().y;
    // A separate table ID while empty: initial column widths are only applied when a table is
    // created, so the populated table must not inherit widths measured from headers alone.
    if (ImGui::BeginTable(rows.empty() ? "##tableEmpty" : "##table", ncols, flags, ImVec2(0, height))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        for (int c = 0; c < ncols; ++c) {
            const std::string& name = state.columns.empty() ? std::string("value") : state.columns[static_cast<std::size_t>(c)];
            ImGuiTableColumnFlags cf = ImGuiTableColumnFlags_WidthFixed;
            if (c >= 14) cf |= ImGuiTableColumnFlags_DefaultHide;
            const float w = static_cast<std::size_t>(c) < state.widths.size() ? state.widths[static_cast<std::size_t>(c)] : 0.0f;
            ImGui::TableSetupColumn(name.c_str(), cf, w);
        }
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            state.sortColumn = specs->Specs[0].ColumnIndex;
            state.sortDesc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        }
        if (state.sortColumn >= 0 && state.sortColumn < static_cast<int>(state.columns.size())) {
            const std::string& key = state.columns[static_cast<std::size_t>(state.sortColumn)];
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const json* va = jsonutil::find(rows[static_cast<std::size_t>(a)], key);
                const json* vb = jsonutil::find(rows[static_cast<std::size_t>(b)], key);
                if (!va || !vb) return va != nullptr && vb == nullptr;
                bool less;
                if (va->is_number() && vb->is_number()) {
                    less = va->get<double>() < vb->get<double>();
                    if (state.sortDesc) less = va->get<double>() > vb->get<double>();
                } else {
                    const auto sa = jsonutil::scalarToString(*va);
                    const auto sb = jsonutil::scalarToString(*vb);
                    less = state.sortDesc ? sa > sb : sa < sb;
                }
                return less;
            });
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(order.size()));
        while (clipper.Step()) {
            for (int oi = clipper.DisplayStart; oi < clipper.DisplayEnd; ++oi) {
                const int idx = order[static_cast<std::size_t>(oi)];
                const json& row = rows[static_cast<std::size_t>(idx)];
                ImGui::TableNextRow();
                ImGui::PushID(idx);
                for (int c = 0; c < ncols; ++c) {
                    if (!ImGui::TableSetColumnIndex(c)) continue;
                    const std::string& colName = state.columns.empty() ? std::string("value") : state.columns[static_cast<std::size_t>(c)];
                    std::string text;
                    if (!row.is_object()) {
                        text = jsonutil::scalarToString(row);
                    } else if (opts.formatCell) {
                        const json* v = jsonutil::find(row, colName);
                        text = opts.formatCell(colName, v ? *v : json());
                    } else {
                        text = cellString(row, colName);
                    }
                    if (c == 0) {
                        const bool sel = state.selected == idx;
                        if (ui::TableRowSelectable(text.empty() ? "##row" : text.c_str(), sel)) {
                            state.selected = idx;
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activated = idx;
                        if (ImGui::BeginPopupContextItem("##rowctx")) {
                            state.selected = idx;
                            if (opts.rowMenu) {
                                opts.rowMenu(row, idx);
                                ImGui::Separator();
                            }
                            if (ImGui::MenuItem(ICON_FA_COPY "  Copy row as JSON")) ImGui::SetClipboardText(row.dump(2).c_str());
                            if (ImGui::MenuItem(ICON_FA_COPY "  Copy first cell")) ImGui::SetClipboardText(text.c_str());
                            ImGui::EndPopup();
                        }
                    } else {
                        ImGui::TextUnformatted(text.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return activated;
}

bool ClickedOutsideCurrentWindow() {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) return false;
    if (ImGui::IsWindowAppearing()) return false;  // the click that opened the window
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGuiWindow* hovered = GImGui->HoveredWindow;
    // Child windows and popups opened from this window (combo lists, menus, nested dialogs) count as inside.
    return !(hovered && (hovered == win || ImGui::IsWindowChildOf(hovered, win, true, true)));
}

bool BeginDialog(const char* name, ImGuiWindowFlags flags, bool* closed) {
    if (closed) *closed = false;
    bool open = true;
    const bool wasOpen = ImGui::IsPopupOpen(name);
    if (!ImGui::BeginPopupModal(name, &open, flags | ImGuiWindowFlags_NoSavedSettings)) {
        if (closed && wasOpen && !open) *closed = true;  // x button
        return false;
    }
    if (ClickedOutsideCurrentWindow()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (closed) *closed = true;
        return false;
    }
    return true;
}

bool ConfirmModal(const char* id, const std::string& message, const char* confirmLabel, bool danger) {
    bool confirmed = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (BeginDialog(id, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const float w = ImGui::GetFontSize() * 7.0f;
        if (danger ? DangerButton(confirmLabel, ImVec2(w, 0)) : ImGui::Button(confirmLabel, ImVec2(w, 0))) {
            confirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (SecondaryButton("Cancel", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return confirmed;
}

}  // namespace vast::ui
