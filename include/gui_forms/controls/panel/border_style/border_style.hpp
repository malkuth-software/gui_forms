#pragma once

#include <cstdint>

namespace gui_forms {

// A control's border (Windows Forms' BorderStyle, with raised), apart from
// Panel so property capabilities can name it.
enum class BorderStyle : std::uint8_t {
    none,
    line,
    sunken,
    raised,
};

} // namespace gui_forms
