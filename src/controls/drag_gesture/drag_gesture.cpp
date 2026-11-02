#include "drag_gesture.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::drag_gesture {
namespace {

double between(double from, double to, double progress) noexcept {
    const double value = from + (to - from) * progress;
    return value;
}

} // namespace

MotionCurve stamp_curve() noexcept {
    const MotionCurve curve = MotionCurve::timed(stamp_duration, MotionEasing::smoothstep);
    return curve;
}

bool valid_threshold(double threshold) noexcept {
    const bool valid = std::isfinite(threshold) && threshold >= minimum_threshold && threshold <= maximum_threshold;
    return valid;
}

bool past_threshold(Point press, Point now, double threshold) noexcept {
    const bool moved = std::fabs(now.x - press.x) >= threshold || std::fabs(now.y - press.y) >= threshold;
    return moved;
}

bool suppresses_targets(Modifier modifiers) noexcept {
    const bool suppressed = has_modifier(modifiers, Modifier::control);
    return suppressed;
}

bool cancels(const KeyEvent& event) noexcept {
    const bool cancelled = event.action == KeyAction::down && event.physical_key == PhysicalKey::escape;
    return cancelled;
}

Point grab_of(Rect source, Point press) noexcept {
    if (source.width <= 0.0 || source.height <= 0.0) return {0.5, 0.5};
    const Point grab{std::clamp((press.x - source.x) / source.width, 0.0, 1.0),
                     std::clamp((press.y - source.y) / source.height, 0.0, 1.0)};
    return grab;
}

Rect stamp_at(Rect source, Point pointer, Point grab, double progress, double text_scale) noexcept {
    const double longer = std::max(1.0, std::max(source.width, source.height));
    const double ratio = std::min(1.0, stamp_side * text_scale / longer);
    const double width = source.width * ratio;
    const double height = source.height * ratio;
    const Rect small{pointer.x - grab.x * width, pointer.y - grab.y * height, width, height};
    const Rect stamp = interpolate(source, small, progress);
    return stamp;
}

Rect interpolate(Rect from, Rect to, double progress) noexcept {
    const Rect value{between(from.x, to.x, progress), between(from.y, to.y, progress),
                     between(from.width, to.width, progress), between(from.height, to.height, progress)};
    return value;
}

} // namespace gui_forms::drag_gesture
