#pragma once

// The code sheet's zoom (the code-zoom demonstrator, owner 2026-11-22): a
// scale between minimum_zoom (the whole file as a minimap) and maximum_zoom
// (a few characters filling the window), and the content point at the
// viewport's top-left. Pure: no control, no clock of its own; the sheet
// (code_sheet.hpp) hands it the viewport, the input and the frame times.
//
// Coordinates: content points are the sheet's logical pixels at zoom 1;
// screen points are the viewport's logical pixels. A content point c shows
// at (c - origin) * zoom.
//
// Two ways to change the zoom. At once (zoom_at, show): a precise wheel
// or a pinch, whose events arrive at the display's rate. Gliding (glide_to,
// glide_by): a key or a wheel's notch, eased over glide_seconds from the
// zoom shown to the target, interpolated in log scale (so every doubling
// takes the same share of the glide) with an ease-out cubic; a press
// during a glide retargets from the glide's target, so presses accumulate.
// Either keeps the content point under its anchor where it is, except where
// the origin's limits (clamp_origin) move it: content smaller than the
// viewport stays inside it, and a viewport smaller than the content stays
// over it.

#include "gui_forms/types/geometry/geometry.hpp"

namespace gui_forms::code_zoom {

inline constexpr double minimum_zoom = 0.1;
inline constexpr double maximum_zoom = 40.0;
// One key press or wheel notch.
inline constexpr double zoom_step = 1.25;
inline constexpr double glide_seconds = 0.18;

// `zoom` within [minimum_zoom, maximum_zoom]; a value that is not finite
// gives 1.
[[nodiscard]] double clamp_zoom(double zoom) noexcept;

// 1 - (1 - t)^3 for t clamped to [0, 1]: 0 at 0, 1 at 1, 0.875 at 0.5.
[[nodiscard]] double ease_out_cubic(double t) noexcept;

// The zoom `progress` (clamped to [0, 1]) of the way from `from` to `to` in
// log scale, after easing: from at 0, to at 1 exactly.
[[nodiscard]] double glide_zoom(double from, double to, double progress) noexcept;

class ZoomModel final {
public:
    // The content's size at zoom 1 and the viewport's; the origin is
    // clamped again.
    void set_extent(Size content, Size viewport) noexcept;

    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    // The glide's target while gliding, else the zoom.
    [[nodiscard]] double target_zoom() const noexcept { return gliding_ ? to_ : zoom_; }
    [[nodiscard]] Point origin() const noexcept { return origin_; }
    [[nodiscard]] bool gliding() const noexcept { return gliding_; }
    // The content point shown at `screen`.
    [[nodiscard]] Point content_at(Point screen) const noexcept;

    // Sets the zoom at once, keeping the content under `anchor` (a screen
    // point) there. Ends a glide.
    void zoom_at(double zoom, Point anchor) noexcept;
    // Shows `content` at `screen` at `zoom`, at once. Ends a glide.
    void show(double zoom, Point content, Point screen) noexcept;
    // Begins a glide to `zoom` about `anchor`. Its clock starts at the next
    // advance, whose time is the glide's first frame.
    void glide_to(double zoom, Point anchor) noexcept;
    // glide_to(target_zoom() * factor, anchor).
    void glide_by(double factor, Point anchor) noexcept;
    // Moves the origin by `content_delta`, clamped. Ends a glide.
    void scroll_by(Point content_delta) noexcept;
    // Advances the glide to `now_seconds` (any monotonic clock); true while
    // it goes on, false once it has reached its target (or none runs).
    bool advance(double now_seconds) noexcept;
    // Ends a glide at its target (reduced motion).
    void finish() noexcept;

private:
    void place(double zoom, Point content, Point screen) noexcept;
    void clamp_origin() noexcept;

    Size content_{};
    Size viewport_{};
    double zoom_{1.0};
    Point origin_{};
    bool gliding_{};
    bool start_pending_{};
    double started_{};
    double from_{1.0};
    double to_{1.0};
    Point anchor_screen_{};
    Point anchor_content_{};
};

} // namespace gui_forms::code_zoom
