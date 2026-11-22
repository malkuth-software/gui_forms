#pragma once

#include <cstdint>

namespace gui_forms {

// What an IndicatorLamp shows (skeudoindustrial surfaces, follow-up 4).
enum class IndicatorLampState : std::uint8_t {
    off,    // the lens dark
    on,     // the lens lit in the theme's on colour
    error,  // the lens lit in the theme's error colour
    // The lens lit in the error colour, failing (owner, 2026-11-22): it
    // flickers and stutters as a lamp breaking down does, deterministically
    // (engine_light::lamp_fault_flicker, at most two dropouts a second), and
    // stays steadily lit under reduced motion. A theme without rules for it
    // draws it as an error: the lamp takes both states.
    fault,
};

// How an IndicatorLamp draws (owner, 2026-11-21).
enum class IndicatorLampStyle : std::uint8_t {
    lens,      // a round lens in a bezel
    pinpoint,  // a tiny accent light for status: a dot, no bezel
};

} // namespace gui_forms
