#pragma once

#include "imgui.h"

namespace vast::ui {

// Dual-handle slider over [lo, hi]. Right-click opens a popup for exact input.
// Returns true while either value changed this frame.
bool RangeSlider(const char* id, double* vmin, double* vmax, double lo, double hi, int decimals = 2,
                 bool logarithmic = false, float width = -1.0f);

}  // namespace vast::ui
