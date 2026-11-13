#pragma once

#include <cstdint>

namespace gui_forms {

// Button's visual styles, apart from the class so its capabilities can
// name them.
enum class ButtonVisualStyle : std::uint8_t {
    standard,
    flat,
    accent,
    command,
};

} // namespace gui_forms
