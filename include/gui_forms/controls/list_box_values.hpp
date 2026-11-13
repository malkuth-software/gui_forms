#pragma once

#include <cstdint>

namespace gui_forms {

// How many rows a ListBox may select: one, or an extended selection that
// Shift and Ctrl grow (Windows Forms SelectionMode One and MultiExtended).
enum class ListSelectionMode : std::uint8_t {
    one,
    multiple_extended,
};

} // namespace gui_forms
