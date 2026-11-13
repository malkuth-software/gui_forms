#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gui_forms {

// A CSS gradient as a theme wrote it (T3 P6b, CSS_COVERAGE section 1): its
// geometry and stop positions may depend on the part's box, as CSS's do,
// and are resolved into a MaterialFillLayer for one box at a time. A
// gradient whose layer is the same for every box
// (authored_gradient_fixed) is resolved once when the theme compiles; the
// others are resolved when the material is assembled for a part.

// A length along one axis of the part's box: `fraction` of the box's
// extent on that axis plus `units` logical units, as CSS's
// <length-percentage> (a percentage, a length, or the calc() of both).
struct GUI_FORMS_API BoxLength final {
    double fraction{};
    double units{};

    friend constexpr bool operator==(BoxLength left, BoxLength right) noexcept {
        return left.fraction == right.fraction && left.units == right.units;
    }
};

// A linear gradient's `to <corner>`, whose angle depends on the box's
// proportions; `none` for an angle or a side.
enum class GradientCorner : std::uint8_t { none, top_left, top_right, bottom_right, bottom_left };

// A radial gradient's size: CSS's <radial-extent> keywords, or `lengths`,
// the radii as written.
enum class RadialExtent : std::uint8_t { lengths, closest_side, farthest_side, closest_corner, farthest_corner };

// One colour stop. `position`, when `positioned`, is along the gradient:
// - linear: a fraction of the gradient line plus logical units along it;
// - radial: a fraction of the gradient ray (the horizontal radius) plus
//   logical units along it;
// - conic: a fraction of a turn (`units` is 0).
// A stop without a position is placed as CSS places it.
struct GUI_FORMS_API AuthoredGradientStop final {
    Color colour{};
    bool positioned{};
    BoxLength position{};

    friend constexpr bool operator==(const AuthoredGradientStop& left, const AuthoredGradientStop& right) noexcept {
        return left.colour == right.colour && left.positioned == right.positioned && left.position == right.position;
    }
};

// The most stops one gradient may have: resolving adds at most two, the
// colours at the ends of a clipped or rotated stop list.
inline constexpr std::size_t maximum_authored_gradient_stops = maximum_gradient_stops - 2U;

// A gradient as written:
// - `kind`: linear_gradient, radial_gradient or conic_gradient;
// - `repeating`: CSS's repeating- form;
// - `angle_degrees`: a linear gradient's CSS angle (0 up, 90 right),
//   unless `corner` names one, or a conic gradient's `from`;
// - `centre_x`, `centre_y`: a radial or conic gradient's centre, across
//   and down the box;
// - `circle`, `extent`, `radius_x`, `radius_y`: a radial gradient's shape
//   and size; with `lengths`, an ellipse's radii, each along its own axis,
//   or a circle's radius, radius_x's units;
// - `stops`: at least one, at most maximum_authored_gradient_stops.
struct GUI_FORMS_API AuthoredGradient final {
    MaterialFillKind kind{MaterialFillKind::linear_gradient};
    bool repeating{};
    double angle_degrees{180.0};
    GradientCorner corner{GradientCorner::none};
    BoxLength centre_x{0.5, 0.0};
    BoxLength centre_y{0.5, 0.0};
    bool circle{};
    RadialExtent extent{RadialExtent::farthest_corner};
    BoxLength radius_x{};
    BoxLength radius_y{};
    std::vector<AuthoredGradientStop> stops{};

    friend bool operator==(const AuthoredGradient& left, const AuthoredGradient& right) noexcept {
        return left.kind == right.kind && left.repeating == right.repeating &&
               left.angle_degrees == right.angle_degrees && left.corner == right.corner &&
               left.centre_x == right.centre_x && left.centre_y == right.centre_y && left.circle == right.circle &&
               left.extent == right.extent && left.radius_x == right.radius_x && left.radius_y == right.radius_y &&
               left.stops == right.stops;
    }
};

// Whether `gradient` is one a layer can draw: a gradient kind; one to
// maximum_authored_gradient_stops stops; finite numbers; radii not
// negative; a circle's radius a length, without a fraction; a conic stop
// without units.
[[nodiscard]] GUI_FORMS_API bool valid_authored_gradient(const AuthoredGradient& gradient) noexcept;

// Whether a valid `gradient` resolves to the same layer for every box: no
// corner and no units anywhere, no circle (whose radius is the same in
// both directions, so a different fraction of each), and, for a repeating
// linear gradient, stops that run from 0 to 1 once placed. Such a layer
// is in normalized coordinates or a CSS angle.
[[nodiscard]] GUI_FORMS_API bool authored_gradient_fixed(const AuthoredGradient& gradient) noexcept;

// Writes a valid `gradient`, for a part's box `box` in logical units, into
// `layer`, which is reset first: its kind, geometry, spread and stops, the
// other fields at MaterialFillLayer's defaults. A fixed gradient
// (authored_gradient_fixed) ignores `box`. The layer:
// - places the stops as CSS does: a first stop without a position at 0, a
//   last one at 1, a stop before an earlier one at the earlier one, the
//   others evenly between their neighbours;
// - for a gradient that does not repeat, keeps the stops within the drawn
//   range and adds the colours at its ends, interpolated as the engine
//   interpolates (straight colour, rounded to 8 bits: at most half a step
//   from the exact colour);
// - for a repeating one, scales its geometry to the span from the first
//   stop to the last; a radial one's stops are rotated to begin at its
//   centre. A span of 0 draws CSS's average colour, a solid;
// - separates coincident stops (CSS's hard stops) by 2^-20 of the
//   gradient, which the layer's validity requires: under a tenth of a
//   device pixel on a gradient shorter than 100,000 device pixels;
// - is a solid for a single stop.
GUI_FORMS_API void resolve_authored_gradient(const AuthoredGradient& gradient, Size box, MaterialFillLayer& layer);

} // namespace gui_forms
