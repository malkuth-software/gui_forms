#pragma once

// PlotTransform: data to device over one plot rectangle (IMGUI_IMPLOT_STUDY
// B.3, P5), composed from an x axis and a y axis. "Device" here is the
// painter's logical coordinate space, in which the plot rectangle is given;
// `device_scale` (device pixels per logical unit) places pixel boundaries,
// for decimation columns and for the snap rule.
//
// Snap rule: a stroke of odd device-pixel width is crisp when its centre
// lies on a device pixel's centre; snap_line() moves a coordinate there.
//
// Spans are mapped into caller storage (Point arrays reserved at the
// decimated maximum, about four per device column, by the caller).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/axis/axis.hpp"
#include "gui_forms/plots/scale/scale.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::plots {

enum class PlotTransformCheck : std::uint8_t {
    valid,
    x_axis_invalid,
    y_axis_invalid,
    // The rectangle is empty or not finite, or the scale is not finite and
    // positive.
    plot_invalid,
};

class PlotTransform;

struct PlotTransformResult;

class GUI_FORMS_API PlotTransform final {
public:
    PlotTransform() noexcept = default;
    [[nodiscard]] static PlotTransformResult make(const Axis& x_axis, const Axis& y_axis, Rect plot,
                                                  double device_scale) noexcept;

    [[nodiscard]] const ScaleMapping& x() const noexcept { return x_; }
    [[nodiscard]] const ScaleMapping& y() const noexcept { return y_; }
    [[nodiscard]] Rect plot() const noexcept { return plot_; }
    // The rectangle marks are clipped to: the plot rectangle.
    [[nodiscard]] Rect clip() const noexcept { return plot_; }
    [[nodiscard]] double device_scale() const noexcept { return device_scale_; }

    [[nodiscard]] Point to_device(double x, double y) const noexcept;
    // The data point at a device point.
    [[nodiscard]] double x_from_device(double device_x) const noexcept;
    [[nodiscard]] double y_from_device(double device_y) const noexcept;
    // The device pixel column holding device x (floor of x times the scale,
    // relative to the plot's left edge).
    [[nodiscard]] DeviceColumn column_of(double device_x) const noexcept;
    // The number of device pixel columns across the plot (at least 1).
    [[nodiscard]] std::size_t column_count() const noexcept;
    // Maps `xs` and `ys` (equal lengths) into `out` (at least as long);
    // returns the count written (0 when the sizes do not fit). A point with
    // a NaN coordinate stays NaN: it is a gap.
    std::size_t map_points(std::span<const double> xs, std::span<const double> ys, std::span<Point> out) const noexcept;
    // `coordinate` moved to the nearest device pixel centre for a stroke of
    // `width` logical units whose device width rounds to an odd number of
    // pixels; to the nearest pixel boundary for an even one.
    [[nodiscard]] double snap_line(double coordinate, double width) const noexcept;

private:
    ScaleMapping x_{};
    ScaleMapping y_{};
    Rect plot_{};
    double device_scale_{1.0};
};

struct GUI_FORMS_API PlotTransformResult final {
    PlotTransformCheck check{PlotTransformCheck::valid};
    PlotTransform transform{};
};

} // namespace gui_forms::plots
