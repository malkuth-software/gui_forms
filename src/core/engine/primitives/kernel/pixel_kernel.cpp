#include "pixel_kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Identical bytes on every host: no fused multiply-add.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {

Unit premultiply(Color colour, double coverage) noexcept {
    const double alpha = static_cast<double>(colour.alpha) / 255.0 * coverage;
    const Unit unit{static_cast<double>(colour.red) / 255.0 * alpha, static_cast<double>(colour.green) / 255.0 * alpha,
                    static_cast<double>(colour.blue) / 255.0 * alpha, alpha};
    return unit;
}

void blend_over(Unit& target, Unit source) noexcept {
    const double keep = 1.0 - source.alpha;
    target.red = source.red + target.red * keep;
    target.green = source.green + target.green * keep;
    target.blue = source.blue + target.blue * keep;
    target.alpha = source.alpha + target.alpha * keep;
}

void blend_multiply(Unit& target, Unit source) noexcept {
    const double keep_target = 1.0 - source.alpha;
    const double keep_source = 1.0 - target.alpha;
    target.red = source.red * keep_source + target.red * keep_target + source.red * target.red;
    target.green = source.green * keep_source + target.green * keep_target + source.green * target.green;
    target.blue = source.blue * keep_source + target.blue * keep_target + source.blue * target.blue;
    target.alpha = source.alpha + target.alpha - source.alpha * target.alpha;
}

void blend_screen(Unit& target, Unit source) noexcept {
    target.red = source.red + target.red - source.red * target.red;
    target.green = source.green + target.green - source.green * target.green;
    target.blue = source.blue + target.blue - source.blue * target.blue;
    target.alpha = source.alpha + target.alpha - source.alpha * target.alpha;
}

double separable_blend_function(const double backdrop, const double source, const MaterialBlendMode mode) noexcept {
    switch (mode) {
    case MaterialBlendMode::darken: return std::min(backdrop, source);
    case MaterialBlendMode::lighten: return std::max(backdrop, source);
    case MaterialBlendMode::difference: return std::abs(backdrop - source);
    case MaterialBlendMode::exclusion: return backdrop + source - 2.0 * backdrop * source;
    case MaterialBlendMode::hard_light:
        if (source <= 0.5) return 2.0 * source * backdrop;
        return 1.0 - 2.0 * (1.0 - source) * (1.0 - backdrop);
    case MaterialBlendMode::overlay:
        if (backdrop <= 0.5) return 2.0 * source * backdrop;
        return 1.0 - 2.0 * (1.0 - source) * (1.0 - backdrop);
    case MaterialBlendMode::color_dodge:
        if (backdrop <= 0.0) return 0.0;
        if (source >= 1.0) return 1.0;
        return std::min(1.0, backdrop / (1.0 - source));
    case MaterialBlendMode::color_burn:
        if (backdrop >= 1.0) return 1.0;
        if (source <= 0.0) return 0.0;
        return 1.0 - std::min(1.0, (1.0 - backdrop) / source);
    case MaterialBlendMode::soft_light: {
        if (source <= 0.5) return backdrop - (1.0 - 2.0 * source) * backdrop * (1.0 - backdrop);
        double lifted = std::sqrt(backdrop);
        if (backdrop <= 0.25) lifted = ((16.0 * backdrop - 12.0) * backdrop + 4.0) * backdrop;
        return backdrop + (2.0 * source - 1.0) * (lifted - backdrop);
    }
    case MaterialBlendMode::normal: return source;
    case MaterialBlendMode::multiply: return backdrop * source;
    case MaterialBlendMode::screen: return backdrop + source - backdrop * source;
    }
    // A mode outside the enumeration replaces the backdrop, as normal does.
    return source;
}

namespace {

// The straight colour of premultiplied `channel` under `alpha`, held in
// [0, 1]; 0 for a transparent pixel.
double straight_channel(const double channel, const double alpha) noexcept {
    if (!(alpha > 0.0)) return 0.0;
    const double straight = std::clamp(channel / alpha, 0.0, 1.0);
    return straight;
}

double separable_channel(const double source, const double source_alpha, const double target,
                         const double target_alpha, const MaterialBlendMode mode) noexcept {
    const double blended = separable_blend_function(straight_channel(target, target_alpha),
                                              straight_channel(source, source_alpha), mode);
    const double joined =
        source * (1.0 - target_alpha) + target * (1.0 - source_alpha) + source_alpha * target_alpha * blended;
    return joined;
}

} // namespace

void blend_separable(Unit& target, Unit source, const MaterialBlendMode mode) noexcept {
    const double target_alpha = target.alpha;
    target.red = separable_channel(source.red, source.alpha, target.red, target_alpha, mode);
    target.green = separable_channel(source.green, source.alpha, target.green, target_alpha, mode);
    target.blue = separable_channel(source.blue, source.alpha, target.blue, target_alpha, mode);
    target.alpha = source.alpha + target_alpha - source.alpha * target_alpha;
}

Unit colour_filtered(const MaterialColourMatrix& matrix, Unit pixel) noexcept {
    const Unit filtered = colour_staged(matrix, nullptr, pixel);
    return filtered;
}

Unit colour_staged(const MaterialColourMatrix& matrix, const MaterialColourTable* table, Unit pixel) noexcept {
    const double alpha = pixel.alpha;
    const bool visible = alpha > 0.0;
    const std::array<double, 4> straight{visible ? pixel.red / alpha : 0.0, visible ? pixel.green / alpha : 0.0,
                                         visible ? pixel.blue / alpha : 0.0, alpha};
    std::array<double, 4> filtered{};
    for (std::size_t row = 0U; row < MaterialColourMatrix::rows; ++row) {
        const std::size_t first = row * MaterialColourMatrix::columns;
        double sum = matrix.values[first + 4U];
        for (std::size_t column = 0U; column < 4U; ++column) {
            sum = sum + matrix.values[first + column] * straight[column];
        }
        filtered[row] = std::clamp(sum, 0.0, 1.0);
    }
    if (table != nullptr) {
        for (std::size_t channel = 0U; channel < MaterialColourTable::channels; ++channel) {
            const std::size_t level = to_byte(filtered[channel]);
            const std::uint8_t entry = (*table).entries[channel * MaterialColourTable::levels + level];
            filtered[channel] = static_cast<double>(entry) / 255.0;
        }
    }
    const double shown = filtered[3];
    const Unit result{filtered[0] * shown, filtered[1] * shown, filtered[2] * shown, shown};
    return result;
}

std::uint8_t to_byte(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    const double scaled = std::floor(clamped * 255.0 + 0.5);
    const std::uint8_t byte = static_cast<std::uint8_t>(scaled);
    return byte;
}

void store(const PartRasterView& destination, std::size_t offset, const Unit& unit) noexcept {
    destination.pixels[offset] = to_byte(unit.blue);
    destination.pixels[offset + 1U] = to_byte(unit.green);
    destination.pixels[offset + 2U] = to_byte(unit.red);
    destination.pixels[offset + 3U] = to_byte(unit.alpha);
}

double keyline_width(double scale) noexcept {
    const double rounded = std::floor(std::min(scale, 2.0) + 0.5);
    const double width = std::max(1.0, rounded);
    return width;
}

double interval_overlap(double pixel, double start, double end) noexcept {
    const double low = std::max(pixel, start);
    const double high = std::min(pixel + 1.0, end);
    const double overlap = std::max(0.0, high - low);
    return overlap;
}

} // namespace gui_forms::engine_primitives
