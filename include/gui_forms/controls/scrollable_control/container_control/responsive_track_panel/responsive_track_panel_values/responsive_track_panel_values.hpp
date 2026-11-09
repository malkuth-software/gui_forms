#pragma once

#include <cstdint>

namespace gui_forms {

// The track axis of ResponsiveTrackPanel, apart from the class so its
// capabilities can name it.

enum class ResponsiveTrackOrientation : std::uint8_t {
    horizontal,
    vertical,
};

} // namespace gui_forms
