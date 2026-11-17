#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <stxlib/optional.hpp>

namespace gui_forms {

// Curved paths for the Painter vocabulary (exhibit gap E7): contours of
// lines, quadratic and cubic Bezier curves and elliptical arcs, closed or
// open, filled by a PolygonFillRule (Painter::fill_path) or stroked with a
// StrokeStyle (Painter::stroke_path). A path is two parallel arrays the
// caller owns: the verbs, and the points they consume in order. The painter
// reads them during the call only; a recording copies them once.
//
// Points each verb consumes:
// - move_to: 1, the start of a new contour;
// - line_to: 1, the end of a straight segment;
// - quad_to: 2, the control point and the end;
// - cubic_to: 3, the two control points and the end;
// - arc_to: 3 slots that are not all positions: the ellipse's centre, its
//   radii (x the horizontal radius, y the vertical, both positive), and its
//   angles (x the start, y the sweep, in degrees, measured clockwise in the
//   painter's y-down coordinates from the +x axis; a sweep beyond a full
//   turn either way is held to one turn). The ellipse's axes are the
//   painter's axes. In an open contour a straight segment joins the current
//   point to the arc's start; with no current point the arc starts a
//   contour;
// - close: 0; joins the current point to the contour's start and ends the
//   contour (a stroke joins there instead of capping).
//
// A contour must start with move_to or arc_to; a curve or line verb with no
// current point makes the path invalid. Every coordinate is finite.
enum class PaintPathVerb : std::uint8_t { move_to, line_to, quad_to, cubic_to, arc_to, close };

// The point slots `verb` consumes (above); 0 for close and for a value
// outside the enumeration.
[[nodiscard]] GUI_FORMS_API std::size_t paint_path_points_for(PaintPathVerb verb) noexcept;

// A borrowed path: the verbs and their points, both owned by the caller and
// read only for the duration of the call given the view.
struct GUI_FORMS_API PaintPathView final {
    std::span<const PaintPathVerb> verbs{};
    std::span<const Point> points{};
};

// The most verbs and the most points a path holds; a longer path is
// refused (valid_paint_path), as a batched mark of more than a million
// elements is.
inline constexpr std::size_t maximum_paint_path_verbs = 1'000'000U;
inline constexpr std::size_t maximum_paint_path_points = 3'000'000U;

// Whether `path` can be drawn: at least one verb, every verb known, the
// points exactly as many as the verbs consume, all finite, every contour
// starting with move_to or arc_to, arc radii positive, and the counts
// within the maxima above.
[[nodiscard]] GUI_FORMS_API bool valid_paint_path(PaintPathView path) noexcept;

// A bounding box of a valid path's geometry: the box of its positions and
// control points (a Bezier curve lies inside its control points' hull) and
// of each arc's whole ellipse. It may be larger than the drawn geometry,
// never smaller. Empty for an invalid path. A stroke's bounds add half its
// width (more where miters reach: stroke_bounds_outset).
[[nodiscard]] GUI_FORMS_API Rect paint_path_bounds(PaintPathView path) noexcept;

// How far a stroke `width` wide with miter limit `miter_limit` may reach
// beyond the path's geometry, whatever its joins and caps: half the width
// times the larger of the miter limit and the square cap's diagonal (the
// square root of 2). Meant for damage and bounds; finite for a valid
// StrokeStyle.
[[nodiscard]] GUI_FORMS_API double stroke_bounds_outset(double width, double miter_limit) noexcept;

// One contour of a flattened path: its points are those of the flattened
// point array from the previous contour's `end` (0 for the first) to its
// own `end`, exclusive. A closed contour repeats its start point last.
struct GUI_FORMS_API FlattenedContour final {
    std::size_t end{};
    bool closed{};
};

// What flatten_paint_path wrote: the points and contours, and whether the
// storage ran out (the path is then flattened from its start only).
struct GUI_FORMS_API FlattenedPath final {
    std::size_t point_count{};
    std::size_t contour_count{};
    bool truncated{};
};

// The default flattening tolerance, in the painter's units: the farthest a
// flattened chord may lie from the curve (the base painter's fallbacks use
// a quarter unit, below a device pixel at scale 2 and above).
inline constexpr double paint_path_flattening_tolerance = 0.25;

// Fills `points` and `contours`, which the caller owns, with `path`
// flattened to polylines: lines as they are, curves and arcs subdivided
// into chords no farther than `tolerance` (positive, finite) from the
// curve. Writes nothing and returns zeros for an invalid path or a
// tolerance that is not positive and finite. When either array fills, the
// last contour is ended where the storage ran out and `truncated` is set.
// The arrays must not overlap the path's (they cannot: different types).
[[nodiscard]] GUI_FORMS_API FlattenedPath flatten_paint_path(PaintPathView path, double tolerance,
                                                             std::span<Point> points,
                                                             std::span<FlattenedContour> contours) noexcept;

// Path length and trimming (owner, 2026-11-16: trim paths in the core).
// Both measure a path on chords, in + - * / and sqrt only (engine_math), so
// the same path and tolerance give the same bits on every host. The chord
// counts are fixed by the geometry: a Bezier curve takes Wang's count for
// `tolerance` (at most maximum_measured_curve_chords), an arc enough equal
// angular steps that no chord's sagitta exceeds `tolerance` and none turns
// more than a quarter (at most maximum_measured_arc_chords). These are
// counts of their own, finer for arcs than flatten_paint_path's.
inline constexpr std::size_t maximum_measured_curve_chords = 1024U;
inline constexpr std::size_t maximum_measured_arc_chords = 4096U;

// The tolerance the painters measure a trimmed stroke at
// (StrokeStyle::trim), in painter units: a sixteenth, so a cut lies within
// a small fraction of a device pixel of its exact arc-length position at
// any display scale up to 4.
inline constexpr double stroke_trim_tolerance = 1.0 / 16.0;

// The length of `path`: each contour's segments end to end, in order,
// including the straight joins arc_to makes and each close's closing
// segment, measured on its chords (above) at `tolerance` (positive,
// finite). A chord never exceeds the curve it spans, so the result is at
// most the exact length. Empty for an invalid path or tolerance; 0 for a
// path of points only.
[[nodiscard]] GUI_FORMS_API stxlib::Optional<double> paint_path_length(PaintPathView path, double tolerance) noexcept;

// The part of a path a stroke keeps (Lottie's and ThorVG's trim path):
// `start` and `end` are fractions of the path's whole length (every
// contour end to end, paint_path_length), each in [0, 1], and either may
// be the larger: the kept part runs from the smaller to the larger.
// `offset` (any finite value, in the same fractions) moves that part along
// the path and wraps it across the path's start. Start 0 and end 1 (in
// either order) keep the whole path whatever the offset; equal start and
// end keep nothing.
struct GUI_FORMS_API StrokeTrim final {
    double start{0.0};
    double end{1.0};
    double offset{0.0};

    friend bool operator==(const StrokeTrim& left, const StrokeTrim& right) noexcept {
        return left.start == right.start && left.end == right.end && left.offset == right.offset;
    }
};

// Whether `trim` is valid: start and end finite and within [0, 1], offset
// finite.
[[nodiscard]] GUI_FORMS_API bool valid_stroke_trim(StrokeTrim trim) noexcept;

// Whether a valid `trim` keeps the whole path (start and end are 0 and 1).
[[nodiscard]] GUI_FORMS_API bool stroke_trim_keeps_whole(StrokeTrim trim) noexcept;

// What trim_paint_path did.
enum class PaintPathTrimResult : std::uint8_t {
    trimmed, // `kept` holds the kept part
    whole,   // the trim keeps the whole path: draw `path` itself; `kept` is cleared
    empty,   // nothing is kept (equal start and end, or a path of length 0); `kept` is cleared
    refused, // an invalid path, trim or tolerance; `kept` is unchanged
};

// The fixed storage of the base Painter's path fallbacks (Painter::fill_path,
// Painter::stroke_path): a flattened path longer than this is drawn from its
// start.
inline constexpr std::size_t maximum_fallback_path_points = 2048U;
inline constexpr std::size_t maximum_fallback_path_contours = 64U;

// An owned path built verb by verb, for callers that do not keep their own
// arrays. Each builder call appends one verb and its points; nothing is
// validated until it is drawn (valid_paint_path). reserve() allocates once
// before a long build; clear() keeps the storage for the next build.
class GUI_FORMS_API PaintPath final {
public:
    void reserve(std::size_t verb_capacity, std::size_t point_capacity);
    void clear() noexcept;
    void move_to(Point point);
    void line_to(Point point);
    void quad_to(Point control, Point end);
    void cubic_to(Point first_control, Point second_control, Point end);
    // An elliptical arc as arc_to above: `radii` x and y the horizontal and
    // vertical radius, angles in degrees, clockwise (y down) from +x.
    void arc_to(Point center, Size radii, double start_degrees, double sweep_degrees);
    void close();

    // The path, borrowed from this object: valid until the next builder
    // call, clear() or destruction. Not for a temporary path.
    [[nodiscard]] PaintPathView view() const& noexcept;
    PaintPathView view() const&& = delete;

    friend bool operator==(const PaintPath& left, const PaintPath& right) noexcept {
        return left.verbs_ == right.verbs_ && left.points_ == right.points_;
    }

private:
    std::vector<PaintPathVerb> verbs_{};
    std::vector<Point> points_{};
};

// Writes into `kept` the part of `path` that `trim` keeps, measured at
// `tolerance` (positive, finite; the painters use stroke_trim_tolerance).
//
// The kept part is cut from the path's own verbs: a whole segment is
// copied as it is, and a cut segment becomes the same kind of verb over a
// sub-range of its parameter (a line's end, a Bezier curve's control
// points by de Casteljau's blossoms, an arc's start and sweep). Only the
// cut positions come from the chords: the parameter where the measured
// length reaches the cut, interpolated within its chord. A contour the
// kept part covers wholly keeps its close (a stroke joins there); a cut
// contour is open. Each kept piece starts with move_to.
//
// A kept part that wraps across the path's start is two pieces, written in
// path order (the one from the start first), except for a path of one
// closed contour, where the piece up to the contour's end continues
// through its start into the other as one open contour, so a ring's stroke
// has no cut where it passes the start.
//
// Validates everything before clearing `kept`. Reserves `kept` once for
// the common case (twice the path's verbs and points); a cut into many
// pieces may grow it. Throws only what PaintPath's builder throws
// (allocation), leaving `kept` partial.
[[nodiscard]] GUI_FORMS_API PaintPathTrimResult trim_paint_path(PaintPathView path, StrokeTrim trim, double tolerance,
                                                                PaintPath& kept);

} // namespace gui_forms
