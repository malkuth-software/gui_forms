#pragma once

#include <cstdint>

namespace gui_forms {

// TrackBar's visual styles, apart from the class so its capabilities can
// name them.
enum class TrackBarVisualStyle : std::uint8_t {
    classic,
    filled,
    compact,
};

} // namespace gui_forms
