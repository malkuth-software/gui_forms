#pragma once

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace gui_forms::detail {

// The colour arithmetic the base painter's fallbacks share: straight 8-bit
// channels interpolated in sRGB and rounded to the nearest value.

inline std::uint8_t interpolate_channel(std::uint8_t left, std::uint8_t right, double amount) noexcept {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<double>(left) + (static_cast<double>(right) - static_cast<double>(left)) * amount));
}

inline Color interpolate(Color first, Color second, double amount) noexcept {
    amount = std::clamp(amount, 0.0, 1.0);
    return Color::rgba(interpolate_channel(first.red, second.red, amount),
                       interpolate_channel(first.green, second.green, amount),
                       interpolate_channel(first.blue, second.blue, amount),
                       interpolate_channel(first.alpha, second.alpha, amount));
}

// The stops' colour at `offset`, clamped to [0, 1]; transparent for no stops.
inline Color sample_gradient(std::span<const GradientStop> stops, double offset) noexcept {
    if (stops.empty()) return {};
    offset = std::clamp(offset, 0.0, 1.0);
    for (std::size_t index = 1U; index < stops.size(); ++index) {
        if (offset <= stops[index].offset) {
            const double width = stops[index].offset - stops[index - 1U].offset;
            const double local = width <= 0.0 ? 1.0 : (offset - stops[index - 1U].offset) / width;
            return interpolate(stops[index - 1U].color, stops[index].color, local);
        }
    }
    return stops.back().color;
}

} // namespace gui_forms::detail
