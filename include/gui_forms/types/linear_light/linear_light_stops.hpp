#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/linear_light/linear_light.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>

#include <stxlib/view.hpp>

namespace gui_forms {

// The stop expansion is apart from linear_light.hpp because it borrows
// through STXlib's views (D15: the C++ surface's owned vocabulary), and
// GUI.Drawing's core library, which uses linear_light.hpp's inline colour
// functions, links no STXlib. Its callers are the Skia renderers.

// For a renderer that interpolates its stops in sRGB-encoded space (Skia's
// gradient shaders, set to premultiplied sRGB interpolation): writes into
// `expanded` the stops of `stops` with stops added between each two of
// different colour, so that interpolating the expanded stops in sRGB follows
// interpolating `stops` in linear light. Returns the number written.
//
// Between two stops, the span is halved where its middle colour (exact, by
// interpolate_linear_light) differs from the premultiplied sRGB chord of its
// ends by more than half a step in any colour channel, at most eight times
// deep; added stops lie strictly between the two and keep their order, so
// hard stops (two at one offset), solid spans and the spread outside [0, 1]
// are kept. A black-to-white span takes 17 stops, red to blue 29; sampled
// at 1001 points the expanded gradient stays within 1.3 steps of the exact
// colour (half a step of which is the exact colour's own rounding). Stops are added
// only while `expanded` keeps room for every remaining original stop, from
// the first span on; with maximum_linear_light_stops of room only gradients
// of very many contrasting stops reach that bound, and their later spans
// then follow less closely. Writes nothing and returns 0 when `stops` is
// empty or `expanded` is shorter than `stops`. `stops` and `expanded` must
// not overlap.
[[nodiscard]] GUI_FORMS_API std::size_t expand_linear_light_stops(stxlib::View<GradientStop> stops,
                                                                  stxlib::MutableView<GradientStop> expanded) noexcept;

} // namespace gui_forms
