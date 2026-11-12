#pragma once

#include <cstdint>

namespace gui_forms {

// The flow vocabulary of FlowLayoutPanelCompat, apart from the class so its
// capabilities can name it.

enum class FlowDirection : std::uint8_t {
    left_to_right,
    right_to_left,
    top_down,
    bottom_up,
};

enum class FlowMainAlignment : std::uint8_t {
    start,
    center,
    end,
    space_between,
    space_around,
    space_evenly,
};

enum class FlowCrossAlignment : std::uint8_t {
    start,
    center,
    end,
    stretch,
};

} // namespace gui_forms
