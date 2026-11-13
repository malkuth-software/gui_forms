#pragma once

// ToolStripPanel's value types, apart from the control so its capabilities
// header can name them.

#include <cstdint>

namespace gui_forms {

// The side of a ToolStripContainer a panel lines. It decides how the panel
// stacks its strips and which way they run.
enum class ToolStripPanelEdge : std::uint8_t { top, bottom, left, right };

} // namespace gui_forms
