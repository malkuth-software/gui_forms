#pragma once

#include <cstdint>

namespace gui_forms {

// The check state vocabulary of CheckBox, apart from the class so its
// capabilities can name it.

enum class CheckState : std::uint8_t {
    unchecked,
    checked,
    indeterminate,
};

enum class CheckBoxAppearance : std::uint8_t {
    normal,
    button,
};

} // namespace gui_forms
