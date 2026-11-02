#pragma once

#include "gui_forms/theme/part_painting/part_painting.hpp"

namespace gui_forms {

// The built-in recipe (T2 plan): paint_part's drawing from the theme's
// recipe records, as it was before the theme engine. The engine draws every
// part it does not draw with a primitive this way. Private to the core:
// controls paint through paint_part, never through the recipe directly.
void paint_part_with_built_in_recipe(Painter& painter, const Control& control, const PartPaint& part);

// Whether paint_part draws anything in `bounds`: finite and not empty.
[[nodiscard]] bool part_bounds_paintable(Rect bounds) noexcept;

// Whether `part` paints an authored background (owner decision T4-1): it
// carries one and its role paints a surface (PartPaint::authored_background).
// High contrast, which drops it, is the caller's to check.
[[nodiscard]] bool authored_background_applies(const PartPaint& part) noexcept;

} // namespace gui_forms
