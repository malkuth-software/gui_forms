#include "gui_forms/plots/axis/axis.hpp"

#include <cmath>

namespace gui_forms::plots {

Scale axis_scale(const Axis& axis, double device_low, double device_high) noexcept {
    Scale scale{};
    scale.kind = axis.kind;
    scale.domain = axis.range;
    scale.symlog_width = axis.symlog_width;
    // A vertical axis grows upwards on a screen whose y grows downwards.
    const bool minimum_at_low = horizontal_side(axis.side) != axis.inverted;
    scale.device_start = minimum_at_low ? device_low : device_high;
    scale.device_end = minimum_at_low ? device_high : device_low;
    return scale;
}

bool admissible_range(const Axis& axis, PlotRange range) noexcept {
    if (!range.valid()) return false;
    if (axis.kind == ScaleKind::log10 && !(range.minimum > 0.0)) return false;
    const double magnitude = std::fmax(std::fabs(range.minimum), std::fabs(range.maximum));
    if (range.extent() < magnitude * minimum_relative_extent) return false;
    return true;
}

bool AxisLink::set_range(PlotRange range) noexcept {
    if (!range.valid() || range == range_) return false;
    range_ = range;
    revision_ = revision_ + 1U;
    return true;
}

} // namespace gui_forms::plots
