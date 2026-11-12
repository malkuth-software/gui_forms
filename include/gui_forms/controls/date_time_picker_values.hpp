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

enum class DateTimeDropDownAlignment : std::uint8_t {
    left,
    right,
};

} // namespace gui_forms
