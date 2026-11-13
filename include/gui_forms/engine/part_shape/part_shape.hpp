#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace gui_forms {

// A part's shape (T2 plan, phase E8): what a primitive draws within the
// part's box and what its hit region derives from. Themes choose among
// shapes through a primitive's `shape` parameter and never set hit regions
// (design language §7.4 rule 1).
enum class PartShapeKind : std::uint8_t {
    // A rectangle with all four corners rounded by the shape's radius,
    // clamped to half the shorter side.
    rounded_rectangle,
    // Both ends of the long axis rounded by half the shorter side.
    pill,
    // The circle inscribed in the box, centred.
    circle,
    // Only the start (left, or top for an upright box) or the end of the
    // long axis rounded by half the shorter side: one end of a pill, as
    // an actuator attached to its neighbour has.
    cap_start,
    cap_end,
    count,
};

inline constexpr std::size_t part_shape_kind_count = static_cast<std::size_t>(PartShapeKind::count);

// The shapes' registered names, in PartShapeKind order: the choices of a
// primitive's `shape` parameter.
inline constexpr std::array<std::string_view, part_shape_kind_count> part_shape_names{
    "rounded_rectangle", "pill", "circle", "cap_start", "cap_end"};

// The four corners' radii of a shape drawn in a box, in the box's units:
// top-left, top-right, bottom-right and bottom-left.
struct GUI_FORMS_API PartCornerRadii final {
    double top_left{};
    double top_right{};
    double bottom_right{};
    double bottom_left{};

    friend constexpr bool operator==(PartCornerRadii left, PartCornerRadii right) noexcept {
        return left.top_left == right.top_left && left.top_right == right.top_right &&
               left.bottom_right == right.bottom_right && left.bottom_left == right.bottom_left;
    }
};

struct GUI_FORMS_API PartShape final {
    PartShapeKind kind{PartShapeKind::rounded_rectangle};
    // The corner radius of a rounded rectangle, in logical units.
    double corner_radius{};
    // A rounded rectangle whose corners differ (E9.6): when `per_corner` is
    // set, `corners` replaces `corner_radius`, each clamped to half the
    // shorter side. Other kinds ignore both.
    bool per_corner{};
    PartCornerRadii corners{};

    friend constexpr bool operator==(PartShape left, PartShape right) noexcept {
        return left.kind == right.kind && left.corner_radius == right.corner_radius &&
               left.per_corner == right.per_corner && left.corners == right.corners;
    }
};

// The radii of `shape`'s corners drawn in `box`, clamped to half its
// shorter side. A circle reports half the shorter side at every corner,
// though it is the inscribed circle, not a rounded box.
[[nodiscard]] GUI_FORMS_API PartCornerRadii part_corner_radii(PartShape shape, Rect box) noexcept;

// The signed distance from `point` to the edge of `shape` drawn in `box`,
// in the box's units: negative inside, positive outside.
[[nodiscard]] GUI_FORMS_API double shape_signed_distance(PartShape shape, Rect box, Point point) noexcept;

// Whether `point` lies inside `shape` drawn in `box`, all in the same
// units. The box's left and top edges are inside, its right and bottom
// edges outside.
[[nodiscard]] GUI_FORMS_API bool shape_contains(PartShape shape, Rect box, Point point) noexcept;

// How much of the device pixel (x, y) `shape` covers when drawn in `box`
// (logical units) at `scale` device pixels per unit, from 0 to 1: one half
// minus the signed distance of the pixel's centre in device pixels,
// clamped. The same inputs give the same coverage on every host.
[[nodiscard]] GUI_FORMS_API double shape_coverage(PartShape shape, Rect box, double scale, std::int64_t x,
                                    std::int64_t y) noexcept;

// The pointer target minimum of dense chrome (design language §7.4, 20 × 20
// logical units, proposed).
inline constexpr double minimum_hit_target = 20.0;

// A part's hit region (T1's hand-off): its shape in its bounds, never less
// than the minimum target. A point hits when it lies in the bounds and
// either inside the shape or inside the minimum target centred in the
// bounds (clipped to them), so a hit region may exceed what is drawn but
// never falls below the minimum.
struct GUI_FORMS_API PartHitRegion final {
    Rect bounds{};
    PartShape shape{};
};

[[nodiscard]] GUI_FORMS_API bool part_hit(const PartHitRegion& region, Point point) noexcept;

} // namespace gui_forms
