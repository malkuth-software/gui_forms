#pragma once

#include <cstdint>

namespace gui_forms {

// What an IndicatorLamp shows (skeudoindustrial surfaces, follow-up 4).
enum class IndicatorLampState : std::uint8_t {
    off,    // the lens dark
    on,     // the lens lit in the theme's on colour
    error,  // the lens lit in the theme's error colour
};

} // namespace gui_forms
