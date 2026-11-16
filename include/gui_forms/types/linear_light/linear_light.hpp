#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/linear_light/detail/linear_light_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace gui_forms {

// Gradients interpolate in linear light (owner, 2026-11-16;
// notes:build-notes/BLENDING_RULE.md, section 3). Every gradient GUI.Forms
// makes, in the theme materials and the material kernel, in Painter's
// gradient fills and the Skia executor's brushes, in GUI.Drawing's brush
// blends and in the tritone colour table, decodes its two neighbouring
// stops from sRGB to linear light, interpolates them premultiplied there,
// and encodes the result back to an sRGB byte. Alpha is not gamma-encoded:
// it interpolates as it is.
//
// The conversions are tables in integers (detail/linear_light_tables.hpp,
// written by tools/linear_light_tables.py from the exact sRGB curve), so no
// host libm is called and every host and CPU gives the same bytes, whatever
// the build's floating-point contraction (GUI_FORMS_REFERENCE_ARITHMETIC).
// The colour functions are inline, so GUI.Drawing's core library, which
// links no GUI.Forms library, uses them as Core does.

// Linear light in 16-bit fixed point: 0 black, 65535 full intensity. A
// type of its own so that it is never mistaken for an sRGB byte.
struct GUI_FORMS_API LinearLightLevel final {
    std::uint16_t value{};
    friend constexpr bool operator==(LinearLightLevel left, LinearLightLevel right) noexcept {
        return left.value == right.value;
    }
};

// The interpolation weight of the second colour, in 1/65536: 0 the first
// colour, linear_light_weight_one the second.
inline constexpr std::uint32_t linear_light_weight_one = 65536U;

// An sRGB byte's linear light (the table's entry, exact to half a step).
[[nodiscard]] constexpr LinearLightLevel linear_light_of_srgb(std::uint8_t encoded) noexcept {
    const LinearLightLevel level{detail::linear_light_decode_table[encoded]};
    return level;
}

// The sRGB byte nearest `linear` on the encoded scale, halves up: the number
// of encoding thresholds at or below it. Every byte survives the round trip:
// srgb_of_linear_light(linear_light_of_srgb(v)) is v.
[[nodiscard]] constexpr std::uint8_t srgb_of_linear_light(LinearLightLevel linear) noexcept {
    const std::uint16_t* const first = detail::linear_light_encode_thresholds.data();
    const std::uint16_t* const last = first + detail::linear_light_encode_thresholds.size();
    const std::uint16_t* const above = std::upper_bound(first, last, linear.value);
    const std::ptrdiff_t encoded = above - first;
    return static_cast<std::uint8_t>(encoded);
}

// `fraction` of the way as a weight, rounded to the nearest 1/65536 (halves
// up); clamped to [0, 1], NaN taken as 0. Scaling by 65536 is exact, so the
// weight is the same with or without fused multiply-add.
[[nodiscard]] constexpr std::uint32_t linear_light_weight(double fraction) noexcept {
    // !(fraction > 0.0) takes NaN to 0 as well.
    if (!(fraction > 0.0)) return 0U;
    if (fraction >= 1.0) return linear_light_weight_one;
    const double scaled = fraction * static_cast<double>(linear_light_weight_one);
    const double rounded = scaled + 0.5;
    const std::uint32_t weight = static_cast<std::uint32_t>(rounded);
    return std::min(weight, linear_light_weight_one);
}

namespace detail {

// One colour channel of interpolate_linear_light: `keep` and `weight` the
// two stops' weights (summing to 65536) and alpha_sum = from_alpha × keep +
// to_alpha × weight, not zero. The premultiplied linear sum is at most
// 65535 × 255 × 65536, below 2^41.
[[nodiscard]] constexpr std::uint8_t linear_light_channel(std::uint8_t from, std::uint8_t from_alpha,
                                                          std::uint8_t to, std::uint8_t to_alpha,
                                                          std::uint64_t keep, std::uint64_t weight,
                                                          std::uint64_t alpha_sum) noexcept {
    const std::uint64_t from_linear = linear_light_decode_table[from];
    const std::uint64_t to_linear = linear_light_decode_table[to];
    const std::uint64_t from_part = from_linear * from_alpha * keep;
    const std::uint64_t to_part = to_linear * to_alpha * weight;
    const std::uint64_t premultiplied = from_part + to_part;
    const std::uint64_t level = (premultiplied + alpha_sum / 2U) / alpha_sum;
    const std::uint16_t bounded = static_cast<std::uint16_t>(std::min<std::uint64_t>(level, 65535U));
    const std::uint8_t encoded = srgb_of_linear_light(LinearLightLevel{bounded});
    return encoded;
}

} // namespace detail

// The colour `weight` of the way from `from` to `to` (straight alpha in,
// straight alpha out), interpolated premultiplied in linear light: a stop
// fading to transparent fades the other stop's colour, never towards black.
// A weight of 0 is `from` and one of linear_light_weight_one or more `to`,
// exactly; equal colours give that colour. A result whose alpha is zero is
// transparent black. Alpha is rounded to the nearest byte, halves up.
[[nodiscard]] constexpr Color interpolate_linear_light(Color from, Color to, std::uint32_t weight) noexcept {
    if (weight == 0U) return from;
    if (weight >= linear_light_weight_one) return to;
    if (from == to) return from;
    const std::uint64_t later = weight;
    const std::uint64_t keep = linear_light_weight_one - weight;
    const std::uint64_t from_alpha = from.alpha;
    const std::uint64_t to_alpha = to.alpha;
    const std::uint64_t alpha_sum = from_alpha * keep + to_alpha * later;
    if (alpha_sum == 0U) return Color{0U, 0U, 0U, 0U};
    const std::uint8_t alpha = static_cast<std::uint8_t>((alpha_sum + 32768U) >> 16U);
    const std::uint8_t red = detail::linear_light_channel(from.red, from.alpha, to.red, to.alpha, keep, later,
                                                          alpha_sum);
    const std::uint8_t green = detail::linear_light_channel(from.green, from.alpha, to.green, to.alpha, keep,
                                                            later, alpha_sum);
    const std::uint8_t blue = detail::linear_light_channel(from.blue, from.alpha, to.blue, to.alpha, keep, later,
                                                           alpha_sum);
    const Color mixed{red, green, blue, alpha};
    return mixed;
}

// The capacity expand_linear_light_stops (linear_light_stops.hpp) needs for any valid stops
// (valid_gradient_stops: at most maximum_gradient_stops): the stops
// themselves and the stops added between them.
inline constexpr std::size_t maximum_linear_light_stops = 256U;

} // namespace gui_forms
