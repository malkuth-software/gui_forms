#pragma once

#include <cstdint>

namespace gui_forms {

// How ScaledPanel and ScaledGroupBox map their design space onto their
// arranged size (stage VA, decision 10). Both are an explicit, recorded
// stretch of their design content, never an overflow.
enum class ScaledAspect : std::uint8_t {
    // Each axis at its own ratio: the design fills the whole extent, its
    // proportions changed (the default, as before).
    stretch = 0,
    // One scale, the smaller axis ratio, so the design keeps its
    // proportions; it is aligned in the leftover space by the container's
    // viewport_fit alignment (centred by default).
    uniform = 1,
};

} // namespace gui_forms
