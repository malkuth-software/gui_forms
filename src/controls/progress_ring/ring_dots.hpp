#pragma once

// The dot ring Spinner and ProgressRing are drawn with (W-4): `count` round
// dots on a circle inscribed in the control, the first at twelve o'clock and
// the rest clockwise. Each dot is a part the theme's engine paints in the
// progress family: an unlit dot is the `track` part (the family's ordinary
// recipe), a lit one the `fill` or `activity` part with the selected state
// (the family's selected recipe), as ProgressBar's track and fill. The
// floor sheet's :family(progress) blocks therefore style both controls; no
// family or recipe of their own is added.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <string_view>

namespace gui_forms {

class Control;
class Painter;

namespace detail {

// A ring placed in a rectangle: its centre, the radius of the circle the
// dots' centres lie on, and a dot's diameter. Empty (zero radius and dot)
// for a rectangle too small to hold a dot.
struct RingPlacement final {
    Point centre{};
    double radius{};
    double dot{};
};

// The ring inscribed in `bounds` with `count` dots (count at least 1): the
// dot's diameter is the ring's circumference over count times a spacing
// factor, at most a quarter of the ring's diameter.
[[nodiscard]] RingPlacement place_ring(Rect bounds, std::size_t count) noexcept;

// The square of dot `index` (taken modulo `count`; count at least 1).
[[nodiscard]] Rect ring_dot_bounds(const RingPlacement& ring, std::size_t index, std::size_t count) noexcept;

// What a paint of the ring lights: `lit` dots from `first` clockwise,
// wrapping (lit at most count), as the part `lit_part` of role `lit_role`.
struct RingLighting final {
    std::size_t count{1U};
    std::size_t first{};
    std::size_t lit{};
    PartRole lit_role{PartRole::fill};
    std::string_view lit_part{"fill"};
};

// Paints every dot of `ring` for `owner` with `states` (the owner's anatomy
// states; a lit dot adds selected). Nothing for an empty ring or count 0.
void paint_ring(Painter& painter, const Control& owner, const RingPlacement& ring, const RingLighting& lighting,
                AnatomyStateSet states);

} // namespace detail
} // namespace gui_forms
