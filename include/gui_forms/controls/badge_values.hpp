#pragma once

#include <cstdint>

namespace gui_forms {

// What a Badge shows (W-4).
enum class BadgeMode : std::uint8_t {
    count,  // a chip holding the count, or the maximum and "+" past it
    dot,    // a small dot without text: something is new, not how much
};

} // namespace gui_forms
