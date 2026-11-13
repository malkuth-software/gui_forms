#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>

namespace gui_forms {

// The range vocabulary of RangeControl, apart from the class so its
// capabilities can name it.

enum class Orientation : std::uint8_t {
    horizontal,
    vertical,
};

enum class RangeAction : std::uint8_t {
    small_decrement,
    small_increment,
    large_decrement,
    large_increment,
    first,
    last,
    thumb_track,
    thumb_position,
};

struct GUI_FORMS_API RangeScrollEvent final {
    double old_value{};
    double new_value{};
    RangeAction action{RangeAction::thumb_position};
};

} // namespace gui_forms
