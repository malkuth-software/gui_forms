#pragma once

// The view and gesture model of a plot (IMGUI_IMPLOT_STUDY B.3, P10),
// renderer- and control-free so it is tested headless. It holds the plot's
// axes (their visible ranges are the view), the plot rectangle, and the
// pointer gesture in progress; every change goes through it and is
// reported, so the control raises value_changed once per change.
//
// Gestures (each with a keyboard alternative):
//
// | Gesture   | Pointer                                   | Keyboard                       |
// |-----------|-------------------------------------------|--------------------------------|
// | Pan       | left drag in the plot; on an axis, that   | arrows (Shift: a page)         |
// |           | axis only                                 |                                |
// | Zoom      | wheel about the pointer; on an axis, that | + and -; Ctrl with arrows per  |
// |           | axis only                                 | axis                           |
// | Box zoom  | right drag (owner decision 3, ImPlot's    | none (axis range entry)        |
// |           | binding; configurable to Ctrl + left)     |                                |
// | Fit       | double click                              | Home                           |
// | Query     | Ctrl + right drag, kept as the selection  | the selection's handles        |
// | Follow    | -                                         | End resumes following          |
//
// Locks: a locked end never moves. Pan moves both ends, so an axis with
// either end locked refuses pan. A zoom with one end locked zooms about
// that end; with both locked it is refused. A follow-tail axis re-anchors
// on the newest data (follow_tail) until the user pans or zooms it, then
// stops following until a fit or End.

#include "gui_forms/plots/axis/axis.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gui_forms::plots {

enum class PlotPointerButton : std::uint8_t { primary, secondary, middle };

struct PlotModifiers final {
    bool shift{};
    bool control{};
    bool alt{};
};

// Where the pointer is: in the plot, over one of the axes (its orientation
// and position in the model), or elsewhere.
enum class PlotZoneKind : std::uint8_t { outside, plot, x_axis, y_axis };

struct PlotZone final {
    PlotZoneKind kind{PlotZoneKind::outside};
    std::size_t axis{};
};

enum class BoxZoomBinding : std::uint8_t { secondary_drag, control_primary_drag };

enum class PlotKey : std::uint8_t { left, right, up, down, zoom_in, zoom_out, home, end };

// The gesture in progress.
enum class PlotGesture : std::uint8_t { none, pan, box_zoom, query };

// What one input changed.
struct PlotViewChange final {
    // Some axis's range (the view).
    bool view{};
    // The query selection.
    bool selection{};
    // The box being dragged, the crosshair or another transient that only
    // repaints.
    bool transient{};
};

// The data extents fit() fits to: one range per axis, in model order; an
// invalid range leaves its axis as it is.
struct PlotDataBounds final {
    std::array<PlotRange, maximum_axes_per_orientation> x{};
    std::array<PlotRange, maximum_axes_per_orientation> y{};
};

// A rectangle in data units of the primary axes: the query selection.
struct PlotSelection final {
    PlotRange x{};
    PlotRange y{};

    friend bool operator==(const PlotSelection& left, const PlotSelection& right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

// One step of each gesture's size.
inline constexpr double wheel_zoom_per_notch = 1.1;
inline constexpr double key_zoom_factor = 1.25;
inline constexpr double key_pan_fraction = 0.1;
// The padding fit() leaves on each side, as a fraction of the extent.
inline constexpr double fit_padding = 0.02;
// A drag shorter than this (logical units) is a click, not a gesture.
inline constexpr double drag_threshold = 3.0;

class PlotViewModel final {
public:
    PlotViewModel() = default;

    // -- configuration ---------------------------------------------------------
    // Replaces the axes (at most maximum_axes_per_orientation each; the
    // excess is ignored). The first of each is the primary axis.
    void set_axes(std::span<const Axis> x_axes, std::span<const Axis> y_axes) noexcept;
    void set_plot(Rect plot) noexcept { plot_ = plot; }
    void set_box_zoom_binding(BoxZoomBinding binding) noexcept { binding_ = binding; }

    [[nodiscard]] std::span<const Axis> x_axes() const noexcept;
    [[nodiscard]] std::span<const Axis> y_axes() const noexcept;
    [[nodiscard]] Rect plot() const noexcept { return plot_; }
    [[nodiscard]] PlotGesture gesture() const noexcept { return gesture_; }
    // The box being dragged in logical units (box zoom or query); none
    // otherwise.
    [[nodiscard]] std::optional<Rect> drag_box() const noexcept;
    [[nodiscard]] const std::optional<PlotSelection>& selection() const noexcept { return selection_; }
    // Whether axis `index` of its orientation still follows the tail.
    [[nodiscard]] bool x_following(std::size_t index) const noexcept;

    // -- direct changes (rules, the axis menu, tests) ------------------------------
    // Sets an axis's range; refused (false) when inadmissible or locked at a
    // moving end.
    bool set_x_range(std::size_t index, PlotRange range) noexcept;
    bool set_y_range(std::size_t index, PlotRange range) noexcept;
    void clear_selection() noexcept;

    // -- input ------------------------------------------------------------------
    PlotViewChange pointer_pressed(Point position, PlotPointerButton button, PlotModifiers modifiers,
                                   PlotZone zone) noexcept;
    PlotViewChange pointer_moved(Point position) noexcept;
    PlotViewChange pointer_released(Point position) noexcept;
    // Ends a gesture without applying it (capture lost, Escape).
    PlotViewChange cancel_gesture() noexcept;
    // `notches` > 0 zooms in.
    PlotViewChange wheel(Point position, double notches, PlotZone zone) noexcept;
    PlotViewChange key(PlotKey key, PlotModifiers modifiers, const PlotDataBounds& bounds) noexcept;
    // Fits every axis that is not locked at both ends to `bounds`, and
    // resumes following.
    PlotViewChange fit(const PlotDataBounds& bounds) noexcept;
    // Moves each following x axis so its maximum is `newest` (its extent
    // kept at its follow span). Returns whether a range changed.
    bool follow_tail(double newest) noexcept;

private:
    // Pans `axis` by `delta` logical units along its device extent
    // `extent` (positive: the content moves right or down).
    static bool pan_axis(Axis& axis, double delta, double extent, bool horizontal) noexcept;
    // Zooms `axis` by `factor` (> 1 zooms in) about the transformed value
    // at `anchor_fraction` of its range (0 the minimum, 1 the maximum).
    static bool zoom_axis(Axis& axis, double factor, double anchor_fraction) noexcept;
    static bool fit_axis(Axis& axis, PlotRange data) noexcept;
    // The fraction of axis `axis`'s range at logical coordinate
    // `coordinate` along the plot.
    [[nodiscard]] double fraction_at(const Axis& axis, double coordinate, bool horizontal) const noexcept;
    PlotViewChange zoom_all(double factor, Point anchor, PlotZone zone) noexcept;
    PlotViewChange pan_all(Point delta, PlotZone zone) noexcept;
    // The data range of logical interval [low, high] along axis `axis`.
    [[nodiscard]] PlotRange data_interval(const Axis& axis, double low, double high, bool horizontal) const noexcept;
    PlotViewChange apply_box() noexcept;

    std::array<Axis, maximum_axes_per_orientation> x_axes_{};
    std::array<Axis, maximum_axes_per_orientation> y_axes_{};
    std::size_t x_count_{};
    std::size_t y_count_{};
    // Following state per x axis: cleared by a user pan or zoom.
    std::array<bool, maximum_axes_per_orientation> x_following_{};
    Rect plot_{};
    BoxZoomBinding binding_{BoxZoomBinding::secondary_drag};
    PlotGesture gesture_{PlotGesture::none};
    PlotZone gesture_zone_{};
    Point gesture_start_{};
    Point gesture_last_{};
    bool gesture_moved_{};
    std::optional<PlotSelection> selection_{};
};

} // namespace gui_forms::plots
