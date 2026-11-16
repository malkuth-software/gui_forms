#pragma once

#include <cstdint>

namespace gui_forms {

// DateTimePicker's enumerations, apart from the control so its property
// capabilities can name them.

enum class DateTimePickerFormat : std::uint8_t {
    long_date,
    short_date,
    time,
    custom,
};

// Which edge of the picker the drop-down calendar lines up with: CSS's
// logical start and end, so `start` is the left edge in left to right and
// the right edge in right to left (Windows Forms' LeftRightAlignment, which
// mirrors the same way).
enum class DateTimeDropDownAlignment : std::uint8_t {
    start,
    end,
};

} // namespace gui_forms
