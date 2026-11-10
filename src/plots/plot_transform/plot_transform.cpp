#include "gui_forms/plots/plot_transform/plot_transform.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::plots {

PlotTransformResult PlotTransform::make(const Axis& x_axis, const Axis& y_axis, Rect plot,
                                        double device_scale) noexcept {
    PlotTransformResult result{};
    if (plot.empty() || !plot.finite() || !(std::isfinite(device_scale) && device_scale > 0.0)) {
        result.check = PlotTransformCheck::plot_invalid;
        return result;
    }
    const ScaleMappingResult x = ScaleMapping::make(axis_scale(x_axis, plot.left(), plot.right()));
    if (x.check != ScaleCheck::valid) {
        result.check = PlotTransformCheck::x_axis_invalid;
        return result;
    }
    const ScaleMappingResult y = ScaleMapping::make(axis_scale(y_axis, plot.top(), plot.bottom()));
    if (y.check != ScaleCheck::valid) {
        result.check = PlotTransformCheck::y_axis_invalid;
        return result;
    }
    result.transform.x_ = x.mapping;
    result.transform.y_ = y.mapping;
    result.transform.plot_ = plot;
    result.transform.device_scale_ = device_scale;
    return result;
}

Point PlotTransform::to_device(double x, double y) const noexcept {
    const Point point{x_.forward(x), y_.forward(y)};
    return point;
}

double PlotTransform::x_from_device(double device_x) const noexcept {
    const double result = x_.inverse(device_x);
    return result;
}

double PlotTransform::y_from_device(double device_y) const noexcept {
    const double result = y_.inverse(device_y);
    return result;
}

DeviceColumn PlotTransform::column_of(double device_x) const noexcept {
    const double pixels = std::floor((device_x - plot_.x) * device_scale_);
    DeviceColumn column{};
    column.value = static_cast<std::int64_t>(std::clamp(pixels, -9.0e15, 9.0e15));
    return column;
}

std::size_t PlotTransform::column_count() const noexcept {
    const double columns = std::ceil(plot_.width * device_scale_);
    const std::size_t result = columns < 1.0 ? 1U : static_cast<std::size_t>(columns);
    return result;
}

std::size_t PlotTransform::map_points(std::span<const double> xs, std::span<const double> ys,
                                      std::span<Point> out) const noexcept {
    if (xs.size() != ys.size() || out.size() < xs.size()) return 0U;
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        out[index] = Point{x_.forward(xs[index]), y_.forward(ys[index])};
    }
    return xs.size();
}

double PlotTransform::snap_line(double coordinate, double width) const noexcept {
    const double device_width = std::max(1.0, std::round(width * device_scale_));
    const double device = coordinate * device_scale_;
    const bool odd = std::fmod(device_width, 2.0) == 1.0;
    const double snapped = odd ? std::floor(device) + 0.5 : std::round(device);
    const double result = snapped / device_scale_;
    return result;
}

} // namespace gui_forms::plots
