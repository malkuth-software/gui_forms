#pragma once

#include <cstdint>

namespace gui_forms {

// A component's lifecycle, stored as a code. The codes are far apart (any two
// differ in 16 of their 32 bits) and none is zero, so one flipped bit, or
// storage that was zeroed, is no state at all: Component checks the code
// where it reads it and fails fast on one that is no state (bit-flip run 7,
// F1), instead of reading "not alive" and refusing every later call, its
// disposal included, while the control stays in its tree.
enum class ComponentState : std::uint32_t {
    alive = 0xA5A55A5AU,
    disposing = 0x3C3CC3C3U,
    disposed = 0x69699696U,
};

} // namespace gui_forms
