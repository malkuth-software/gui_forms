#pragma once

// The drawing vocabulary of effect programs (design note §4): every call an
// effect may make to draw, in part-local logical units (the part's top-left
// corner is the origin), clipped to the part and its declared outsets. The
// engine maps to device pixels. Each call declares its cost for the
// plugin's static bound (GUI_FORMS_EFFECT_VOCABULARY: fixed operations, and
// operations per device pixel covered); the canvas meters the per-pixel
// half as the frame runs, deterministically from the geometry, and refuses
// a call that would take the frame past its budget (owner decision 1(a)).
// A refused call draws nothing and is counted.
//
// Everything is drawn source-over in the parent's paint order, so what an
// effect draws in its outsets lies over the siblings painted before its
// parent and under those painted after, and never reads or replaces a
// sibling's pixels (decision 7(a): nothing additive).
//
// The vocabulary is what PlaySuite's 2D drawing needs (shared/render r2d
// and Canvas, shared/particles), reduced to what GUI.Forms' Painter draws.
// Version 1: rectangles, rounded rectangles and circles, lines, linear and
// radial gradients, soft dots and streaks, and a bounded save, clip and
// translate. Version 2 (current_effect_vocabulary_version) adds the
// Painter's batched marks over fixed-capacity arrays of at most
// maximum_effect_batch elements (polylines with joins, caps and dashes,
// whose phase an effect advances for marching ants or link flow; polygons;
// bands; rectangle batches; segments; markers), the conic gradient and the
// checkerboard. A batched call refuses, and counts, an array whose count is
// past its capacity or below the call's minimum, or any point that is not
// finite. Rotation, text, images and r3d's triangles are not in it.
//
// Execution: UI thread, inside the parent's paint. The canvas borrows the
// painter for the frame and is discarded with it.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gui_forms {

class Painter;
class PartRaster;

// The deepest the canvas's save stack goes; a deeper save is refused.
inline constexpr std::size_t maximum_effect_save_depth = 4U;

// A gradient's stops: the first `count` of `stops` are live, offsets in
// [0, 1] and non-decreasing.
struct GUI_FORMS_API EffectGradient final {
    std::array<GradientStop, maximum_effect_gradient_stops> stops{};
    std::uint32_t count{};

    friend constexpr bool operator==(const EffectGradient& left, const EffectGradient& right) noexcept {
        return left.count == right.count && left.stops == right.stops;
    }
};

// The arrays a batched call draws: effect state, so fixed capacity.
using EffectPoints = EffectArray<Point, maximum_effect_batch>;
using EffectRects = EffectArray<Rect, maximum_effect_batch>;
using EffectSegments = EffectArray<PaintSegment, maximum_effect_batch>;

// What one frame's canvas did: device pixels charged against the budget,
// and the calls refused for the budget or the save depth.
struct GUI_FORMS_API EffectCanvasMeter final {
    double budget{};
    double charged{};
    std::uint32_t calls{};
    std::uint32_t refused{};

    friend constexpr bool operator==(const EffectCanvasMeter& left, const EffectCanvasMeter& right) noexcept {
        return left.budget == right.budget && left.charged == right.charged && left.calls == right.calls &&
               left.refused == right.refused;
    }
};

class GUI_FORMS_API EffectCanvas final {
public:
    // A canvas over `painter` for a part of `part` logical size whose origin
    // is the painter's current origin, with `outsets` around it, at display
    // `scale`, and a budget of `per_device_pixel` operations for each device
    // pixel of the part and its outsets. The painter must already be clipped
    // to the part and its outsets and translated to the part's origin (the
    // host does both); the canvas leaves its save depth as it found it.
    // `corners` are the part's shape's corner radii (clip_part_shape).
    // `backdrop`, borrowed for the canvas's life, is the blurred backplane
    // under the part and its outsets, one raster pixel per device pixel
    // (draw_backdrop); null, or pointing at null, when there is none.
    EffectCanvas(Painter& painter, Size part, Insets outsets, double scale, std::uint32_t per_device_pixel,
                 PartCornerRadii corners = {},
                 const std::shared_ptr<const PartRaster>* backdrop = nullptr) noexcept;
    ~EffectCanvas();
    EffectCanvas(const EffectCanvas&) = delete;
    EffectCanvas& operator=(const EffectCanvas&) = delete;

    GUI_FORMS_EFFECT_VOCABULARY(4, 1)
    void fill_rect(Rect rect, Color color) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(6, 2)
    void fill_rounded_rect(Rect rect, double radius, Color color) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(6, 2)
    void fill_circle(Point center, double radius, Color color) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(6, 2)
    void stroke_rect(Rect rect, Color color, double width) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(8, 2)
    void stroke_circle(Point center, double radius, Color color, double width) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(6, 2)
    void stroke_line(Point from, Point to, Color color, double width) noexcept;
    // A linear gradient over `rect` from `start` to `end`; `repeat` repeats
    // it past them instead of extending its end colours.
    GUI_FORMS_EFFECT_VOCABULARY(10, 3)
    void fill_linear_gradient(Rect rect, Point start, Point end, const EffectGradient& gradient,
                              bool repeat) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(10, 4)
    void fill_radial_gradient(Rect rect, Point center, Size radii, const EffectGradient& gradient) noexcept;
    // A soft round dot: `color` at its centre fading to transparent at
    // `radius` (PlaySuite's mist puffs and glints, drawn source-over).
    GUI_FORMS_EFFECT_VOCABULARY(12, 4)
    void soft_dot(Point center, double radius, Color color) noexcept;
    // A streak: a line of `width` from `tail` to `head` with a soft dot at
    // the head (PlaySuite's particle streaks, simplified).
    GUI_FORMS_EFFECT_VOCABULARY(18, 4)
    void streak(Point head, Point tail, double width, Color color) noexcept;

    // Version 2. Each batched call's fixed cost covers maximum_effect_batch
    // elements; its pixels are charged on the batch's bounds, held to the
    // area its marks can cover.
    // An open polyline through 2 or more points with `style`'s joins, caps
    // and dashes (valid_stroke_style).
    GUI_FORMS_EFFECT_VOCABULARY(72, 2)
    void stroke_polyline(const EffectPoints& points, const StrokeStyle& style) noexcept;
    // A closed polygon through 3 or more points.
    GUI_FORMS_EFFECT_VOCABULARY(72, 2)
    void fill_polygon(const EffectPoints& points, Color color, PolygonFillRule rule) noexcept;
    // The area between two polylines of the same count, 2 or more.
    GUI_FORMS_EFFECT_VOCABULARY(140, 2)
    void fill_band(const EffectPoints& upper, const EffectPoints& lower, Color color) noexcept;
    // Each of 1 or more rectangles in one colour.
    GUI_FORMS_EFFECT_VOCABULARY(72, 1)
    void fill_rects(const EffectRects& rects, Color color) noexcept;
    // Each of 1 or more segments with `style`'s caps and dashes, no joins.
    GUI_FORMS_EFFECT_VOCABULARY(72, 2)
    void stroke_segments(const EffectSegments& segments, const StrokeStyle& style) noexcept;
    // A marker centred on each of 1 or more points (valid_marker_style).
    GUI_FORMS_EFFECT_VOCABULARY(140, 3)
    void draw_markers(const EffectPoints& centers, const MarkerStyle& style) noexcept;
    // A conic (sweep) gradient over `rect`: one turn clockwise from
    // `start_degrees` about `center` (piece C9).
    GUI_FORMS_EFFECT_VOCABULARY(12, 4)
    void fill_conic_gradient(Rect rect, Point center, double start_degrees, const EffectGradient& gradient) noexcept;
    // `rect` filled with a checkerboard (piece C7).
    GUI_FORMS_EFFECT_VOCABULARY(8, 2)
    void fill_checker(Rect rect, const CheckerPattern& pattern) noexcept;

    // Saves the clip and the offset; refused past maximum_effect_save_depth.
    GUI_FORMS_EFFECT_VOCABULARY(2, 0)
    void save() noexcept;
    // Restores the last save; a restore without a save does nothing.
    GUI_FORMS_EFFECT_VOCABULARY(2, 0)
    void restore() noexcept;
    // Narrows the clip to `rect` (in the current offset's coordinates).
    GUI_FORMS_EFFECT_VOCABULARY(4, 0)
    void clip_rect(Rect rect) noexcept;
    // Narrows the clip to `rect` with every corner rounded by `radius`
    // (clamped to half the shorter side; 0 is a plain clip_rect), in the
    // current offset's coordinates: a fill's inner shape, inside its rim.
    GUI_FORMS_EFFECT_VOCABULARY(6, 0)
    void clip_rounded_rect(Rect rect, double radius) noexcept;
    GUI_FORMS_EFFECT_VOCABULARY(2, 0)
    void translate(Point offset) noexcept;
    // Narrows the clip to the part's own shape (EP-6): its box with the
    // corners its host gives it (EffectInstance::paint), so an effect drawn
    // over a rounded part stays inside its corners. It gives up the
    // outsets until the next restore.
    GUI_FORMS_EFFECT_VOCABULARY(6, 0)
    void clip_part_shape() noexcept;
    // Draws the window's backplane under the part, blurred as the effect
    // declares (EffectDeclaration::backdrop, EP-7), within `rect` (in the
    // current coordinates) at `opacity` in [0, 1], device-aligned and never
    // resampled. Refused, and counted, for an effect that does not declare
    // a backdrop or a part whose host lends none (a part in the backplane
    // plane, a headless paint, or a backplane that could not be made).
    GUI_FORMS_EFFECT_VOCABULARY(10, 2)
    void draw_backdrop(Rect rect, double opacity) noexcept;

    // For the host: what the frame charged and refused.
    [[nodiscard]] const EffectCanvasMeter& meter() const noexcept { return meter_; }

private:
    // One saved level: the clip in part-local coordinates and the offset.
    struct Level final {
        Rect clip{};
        Point offset{};
        bool painter_saved{};
    };

    // Charges `per_pixel` operations for each device pixel of `bounds` (in
    // the current offset's coordinates) inside the clip; false, counting a
    // refusal, when that would pass the budget or the bounds are not finite.
    bool charge(Rect bounds, double coverage, std::uint32_t per_pixel) noexcept;
    // Counts one call and one refusal, for a batch refused before charging.
    void refuse() noexcept;
    [[nodiscard]] Rect to_part(Rect rect) const noexcept;

    Painter& painter_;
    double scale_;
    Size part_;
    PartCornerRadii corners_;
    // The part and its outsets, in part coordinates: where the backdrop lies.
    Rect layer_;
    const std::shared_ptr<const PartRaster>* backdrop_;
    std::array<Level, maximum_effect_save_depth + 1U> levels_{};
    std::size_t depth_{};
    std::size_t painter_saves_{};
    EffectCanvasMeter meter_{};
};

} // namespace gui_forms
