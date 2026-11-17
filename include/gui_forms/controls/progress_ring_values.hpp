#pragma once

#include <cstdint>

namespace gui_forms {

// How a ProgressRing draws its ring (claude/geometry-1; owner decision 3 of
// the 2026-11-16 capability review):
enum class ProgressRingStyle : std::uint8_t {
    theme, // as the theme's `ring-style` says (RecipeField::ring_style); every built-in theme draws dots
    dots,  // forty separate dots, each a part (the built-in look)
    arc,   // one continuous arc over a full-circle track, stroked with a trim
};

} // namespace gui_forms
