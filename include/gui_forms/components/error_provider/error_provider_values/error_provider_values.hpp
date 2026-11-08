#pragma once

// The value types of ErrorProvider's properties, apart from the component
// so its capabilities header can name them.

#include <cstdint>

namespace gui_forms {

enum class ErrorBlinkStyle : std::uint8_t {
    blink_if_different_error,
    always_blink,
    never_blink,
};

} // namespace gui_forms
