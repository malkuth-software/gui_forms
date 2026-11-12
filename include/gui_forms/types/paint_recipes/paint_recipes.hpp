#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// Part primitives composed from the Painter vocabulary
// (NODE_ENGINE_AND_VALUE_BAR_STUDY 1A): the masked proportional fill (C3),
// text over a fill (C4) with its contrast rule, and joined segments (C2).
// Each draws only through the painter it is given, so a recording keeps the
// commands it is made of and every painter draws it alike; none allocates.

// The edge a proportional fill grows from.
enum class FillDirection : std::uint8_t { left_to_right, right_to_left, bottom_to_top, top_to_bottom };

// The part of `track` a fill of `proportion` covers, growing from the edge
// `direction` names. The proportion is clamped to [0, 1]; NaN covers nothing
// (an empty rectangle at that edge).
[[nodiscard]] GUI_FORMS_API Rect proportion_rect(Rect track, double proportion, FillDirection direction) noexcept;

// Piece C3: fills proportion_rect(track, proportion, direction) with `fill`,
// clipped to the track's rounded shape (`track_radii`), so a short fill
// keeps the track's rounded end instead of poking out of it. Drawn as save,
// clip_rounded_rect_corners, fill_rect and restore; an empty portion draws
// nothing.
GUI_FORMS_API void paint_masked_fill(Painter& painter, Rect track, PaintCornerRadii track_radii, double proportion,
                       FillDirection direction, Color fill);

// The WCAG 2 contrast ratio of two colours' sRGB channels, from 1 (equal) to
// 21 (black on white). Alpha is ignored: composite a translucent colour
// first.
[[nodiscard]] GUI_FORMS_API double wcag_contrast_ratio(Color first, Color second) noexcept;

// The contrast rule for text over a fill: of `light` and `dark`, the one
// with the greater contrast ratio against `background` (dark on a tie).
[[nodiscard]] GUI_FORMS_API Color contrasting_text_color(Color background, Color light, Color dark) noexcept;

// Piece C4: one run of text drawn twice under complementary clips, in
// `on_fill_color` where it lies over `fill` and in `text_color` over the
// rest of `track` (a value bar's or progress bar's centred label). The fill
// part is `fill` within `track`; the rest is at most four bands of `track`
// around it, which share the fill part's edges exactly, so every pixel of
// the run is drawn once. `origin` is the baseline start, as draw_text_utf8
// takes it; text outside `track` is clipped. Usually
// `on_fill_color = contrasting_text_color(fill colour, light, dark)`.
GUI_FORMS_API void paint_text_over_fill(Painter& painter, Rect track, Rect fill, Point origin, std::string_view text,
                          FontSpec font, Color text_color, Color on_fill_color);

// Piece C2: where a segment sits in a joined row or column, which decides
// the corners it rounds: `only` all four, `first` the leading two, `last`
// the trailing two, `middle` none.
enum class JoinedSegmentPosition : std::uint8_t { only, first, middle, last };

// Whether joined segments run across (a row: leading is left) or down (a
// column: leading is top).
enum class JoinAxis : std::uint8_t { row, column };

// The position of segment `index` of `count` (an index past the end is
// `last`; a count of 0 or 1 is `only`).
[[nodiscard]] GUI_FORMS_API JoinedSegmentPosition joined_segment_position(std::size_t index, std::size_t count) noexcept;

// The corner radii a segment at `position` takes: `radius` on its outer
// corners and 0 on the corners it shares with a neighbour.
[[nodiscard]] GUI_FORMS_API PaintCornerRadii joined_segment_radii(JoinedSegmentPosition position, JoinAxis axis,
                                                    double radius) noexcept;

// How a joined row or column is outlined: one outer edge round the whole
// group with `radius` corners, and one shared seam between neighbours, both
// `edge_width` wide.
struct GUI_FORMS_API JoinedSegmentsStyle final {
    double radius{4.0};
    Color edge{};
    Color seam{};
    double edge_width{1.0};
};

// Draws a joined row or column: each segment's face in its colour (`faces`
// holds one per segment) with joined_segment_radii, then one seam centred
// in the gap between each pair of neighbours, then the outer edge round the
// segments' union. `segments` lie in order along `axis`; a count mismatch,
// no segments, or a non-finite or negative radius or width draws nothing.
GUI_FORMS_API void paint_joined_segments(Painter& painter, std::span<const Rect> segments, std::span<const Color> faces,
                           JoinAxis axis, const JoinedSegmentsStyle& style);

} // namespace gui_forms
