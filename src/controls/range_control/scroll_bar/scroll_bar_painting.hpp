#pragma once

#include "gui_forms/controls/scroll_bar.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <string_view>

namespace gui_forms::scroll_bar_detail {

// One scroll bar's parts as they are drawn, by the standalone ScrollBar and
// by the bars built into every ScrollableControl, so that both draw the
// same parts under the same keys and one theme rule
// (`v-scroll-bar::part(thumb)`) styles both.
struct ScrollBarPaintParts final {
    Orientation orientation{Orientation::vertical};
    // The whole bar (the trough beneath everything), the track between the
    // arrow buttons, the two buttons (empty where the theme draws no
    // arrows) and the thumb (empty where the track is too short for one).
    Rect bar{};
    Rect track{};
    Rect decrement{};
    Rect increment{};
    Rect thumb{};
    // The painting control's states; the press, capture and hover are
    // moved from them onto the parts named below (a part is pressed or hot
    // only when it is the one named).
    AnatomyStateSet states{};
    ScrollBarPart pressed{ScrollBarPart::none};
    ScrollBarPart hot{ScrollBarPart::none};
    bool thumb_captured{};
    // The control type whose rules style the parts (PartPaint::control_type):
    // empty for the standalone bar, "v_scroll_bar" or "h_scroll_bar" for a
    // built-in one. Borrowed for the call.
    std::string_view control_type{};
    // The parts' keys' item, telling apart two bars one control draws
    // ("horizontal", "vertical"); empty for the standalone bar. Borrowed.
    std::string_view item{};
};

// The square an arrow button's mark fills, centred in `button` on whole
// units; empty when the button is narrower or shorter than
// scroll_arrow_mark_extent.
[[nodiscard]] Rect arrow_mark_bounds(Rect button) noexcept;

// Draws the trough, the track, the arrow buttons with their marks and the
// thumb of `drawn` for `control`, in that order. A button narrower or
// shorter than scroll_arrow_mark_extent draws no mark.
void paint_scroll_bar_parts(Painter& painter, const Control& control, const ScrollBarPaintParts& drawn);

// The thumb on a track `track_extent` long: its extent and its offset from
// the track's start for a value `fraction` (0 to 1) of the way along, with
// `proportion` the visible share of the content. A track shorter than
// `minimum_extent` shows no thumb: the extent is zero and the offset marks
// where the value lies, so presses either side still page toward it.
struct ThumbPlacement final {
    double offset{};
    double extent{};
};
[[nodiscard]] ThumbPlacement place_thumb(double track_extent, double proportion, double fraction,
                                         double minimum_extent) noexcept;

} // namespace gui_forms::scroll_bar_detail
