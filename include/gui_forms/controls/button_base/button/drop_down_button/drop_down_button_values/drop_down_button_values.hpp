#pragma once

// DropDownButton's value types, apart from the control so its capabilities
// header can name them.

#include <cstdint>

namespace gui_forms {

enum class DropDownButtonMode : std::uint8_t {
    menu,
    split,
};

enum class DropDownButtonEdge : std::uint8_t { right, bottom };

} // namespace gui_forms
