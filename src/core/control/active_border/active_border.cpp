// The control border's rules (active_border.hpp): validation, the flash
// limiter and rest poses, the colour over time and the stroke's path.

#include "gui_forms/control/active_border/active_border.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gui_forms {
namespace {

constexpr double full_turn = 6.283185307179586;

bool motion_known(ActiveBorderMotion motion) noexcept {
    switch (motion) {
    case ActiveBorderMotion::steady:
    case ActiveBorderMotion::flashing:
    case ActiveBorderMotion::cycling:
        return true;
    default:
        return false;
    }
}

// One channel mixed from `from` toward `to` by `amount` in [0, 1].
std::uint8_t mixed_channel(std::uint8_t from, std::uint8_t to, double amount) noexcept {
    const double value = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * amount;
    const double rounded = std::round(std::clamp(value, 0.0, 255.0));
    return static_cast<std::uint8_t>(rounded);
}

} // namespace

bool active_border_valid(const ActiveBorder& border) noexcept {
    const bool width = std::isfinite(border.width) && border.width > 0.0 && border.width <= maximum_active_border_width;
    const bool offset = std::isfinite(border.offset) && std::abs(border.offset) <= maximum_active_border_offset;
    const bool radius = std::isfinite(border.corner_radius) && border.corner_radius >= 0.0 &&
                        border.corner_radius <= maximum_active_border_radius;
    const bool frequency = std::isfinite(border.frequency) && border.frequency >= 0.0;
    const bool valid = width && offset && radius && frequency && motion_known(border.motion);
    return valid;
}

bool active_border_saturated_red(Color colour) noexcept {
    if (colour.alpha == 0U) return false;
    const std::uint32_t sum = static_cast<std::uint32_t>(colour.red) + static_cast<std::uint32_t>(colour.green) +
                              static_cast<std::uint32_t>(colour.blue);
    // red / sum >= 0.8, in integers: 5 red >= 4 sum. Black (sum 0) is not red.
    const bool red = sum > 0U && 5U * static_cast<std::uint32_t>(colour.red) >= 4U * sum;
    return red;
}

ActiveBorderPose resolve_active_border(const ActiveBorder& border, const ActiveBorderConditions& conditions) noexcept {
    ActiveBorderPose pose{};
    pose.width = border.width;
    pose.lit = border.colour.value_or(conditions.theme_colour);
    const Color off = Color::rgba(pose.lit.red, pose.lit.green, pose.lit.blue, 0U);
    pose.alternate = border.alternate.value_or(off);
    if (conditions.high_contrast) {
        // System colour, solid and still, never thinner than the minimum.
        pose.lit = conditions.high_contrast_colour;
        pose.alternate = conditions.high_contrast_colour;
        pose.width = std::max(border.width, minimum_high_contrast_active_border_width);
        return pose;
    }
    const bool moving = border.motion != ActiveBorderMotion::steady && border.frequency > 0.0;
    if (conditions.rest || !moving) return pose;
    // The red flash rule: a flash that shows saturated red does not flash.
    const bool red = active_border_saturated_red(pose.lit) || active_border_saturated_red(pose.alternate);
    if (border.motion == ActiveBorderMotion::flashing && red) return pose;
    pose.motion = border.motion;
    pose.frequency = std::min(border.frequency, maximum_active_border_frequency);
    return pose;
}

Color active_border_colour_at(const ActiveBorderPose& pose, double seconds) noexcept {
    const double time = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
    switch (pose.motion) {
    case ActiveBorderMotion::steady:
        return pose.lit;
    case ActiveBorderMotion::flashing: {
        const double halves = std::floor(time * pose.frequency * 2.0);
        const bool lit = std::fmod(halves, 2.0) < 1.0;
        return lit ? pose.lit : pose.alternate;
    }
    case ActiveBorderMotion::cycling: {
        const double amount = 0.5 - 0.5 * std::cos(full_turn * pose.frequency * time);
        const Color blended = Color::rgba(mixed_channel(pose.lit.red, pose.alternate.red, amount),
                                          mixed_channel(pose.lit.green, pose.alternate.green, amount),
                                          mixed_channel(pose.lit.blue, pose.alternate.blue, amount),
                                          mixed_channel(pose.lit.alpha, pose.alternate.alpha, amount));
        return blended;
    }
    default:
        return pose.lit;
    }
}

FrameInterval active_border_interval(const ActiveBorderPose& pose) noexcept {
    double seconds = 0.0;
    if (pose.motion == ActiveBorderMotion::flashing && pose.frequency > 0.0) seconds = 0.5 / pose.frequency;
    if (pose.motion == ActiveBorderMotion::cycling) seconds = 1.0 / active_border_cycling_frames_per_second;
    if (!(seconds > 0.0)) return FrameInterval::zero();
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(seconds));
    return interval;
}

double active_border_reach(const ActiveBorder& border) noexcept {
    const double reach = std::max(0.0, border.offset + border.width);
    return reach;
}

ActiveBorderPath active_border_path(const ActiveBorder& border, Size size) noexcept {
    // The centre line lies offset + width / 2 outside the box (inside when
    // negative); a box too small for an inside line collapses to its centre.
    const double distance = border.offset + border.width * 0.5;
    const double width = std::max(0.0, size.width + 2.0 * distance);
    const double height = std::max(0.0, size.height + 2.0 * distance);
    ActiveBorderPath path{};
    path.rect = Rect{(size.width - width) * 0.5, (size.height - height) * 0.5, width, height};
    path.radius = std::max(0.0, border.corner_radius + distance);
    return path;
}

} // namespace gui_forms
