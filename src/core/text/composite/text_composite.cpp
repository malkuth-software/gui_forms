#include "text_composite.hpp"

#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gui_forms::detail {
namespace {

// One device pixel in clip fixed point.
constexpr std::int64_t unit = 256;
constexpr std::int64_t half_unit = 128;
// Coordinates beyond this are refused rather than risking overflow.
constexpr double coordinate_limit = 100'000'000.0;
// Radii above this many device pixels are clamped; corner distance squares
// then stay well inside 64-bit integers.
constexpr std::int64_t radius_limit = 4096 * unit;

bool valid_coordinate(const double value) noexcept {
    const bool valid = std::isfinite(value) && std::abs(value) <= coordinate_limit;
    return valid;
}

bool valid_rect(const Rect rect) noexcept {
    const bool valid = valid_coordinate(rect.x) && valid_coordinate(rect.y) && valid_coordinate(rect.width) &&
        valid_coordinate(rect.height) && rect.width >= 0.0 && rect.height >= 0.0;
    return valid;
}

// Edges of a device rectangle in fixed point.
struct FixedRect final {
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
};

FixedRect fixed_rect(const Rect rect) noexcept {
    FixedRect result{};
    result.left = std::llround(rect.x * static_cast<double>(unit));
    result.top = std::llround(rect.y * static_cast<double>(unit));
    result.right = std::llround((rect.x + rect.width) * static_cast<double>(unit));
    result.bottom = std::llround((rect.y + rect.height) * static_cast<double>(unit));
    return result;
}

// Covered length of the pixel span [start, start + unit) inside [low, high).
std::int64_t span_coverage(const std::int64_t start, const std::int64_t low, const std::int64_t high) noexcept {
    const std::int64_t covered = std::min(high, start + unit) - std::max(low, start);
    return std::clamp<std::int64_t>(covered, 0, unit);
}

// Exact covered area of pixel (x, y) inside a rectangle, 0..unit.
std::int64_t area_coverage(const FixedRect rect, const std::int64_t x, const std::int64_t y) noexcept {
    const std::int64_t horizontal = span_coverage(x * unit, rect.left, rect.right);
    const std::int64_t vertical = span_coverage(y * unit, rect.top, rect.bottom);
    const std::int64_t area = (horizontal * vertical + half_unit) / unit;
    return area;
}

// Floor square root by binary digits: exact and identical on every machine.
std::uint64_t integer_sqrt(std::uint64_t value) noexcept {
    std::uint64_t result = 0;
    std::uint64_t bit = std::uint64_t{1} << 62U;
    while (bit > value) bit >>= 2U;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

// Coverage from the pixel centre's distance to one corner arc: half a pixel
// inside the arc is full coverage, half a pixel outside is none.
std::int64_t corner_coverage(const std::int64_t centre_x, const std::int64_t centre_y, const std::int64_t corner_x,
                             const std::int64_t corner_y, const std::int64_t radius) noexcept {
    const std::int64_t dx = centre_x - corner_x;
    const std::int64_t dy = centre_y - corner_y;
    const std::uint64_t squared = static_cast<std::uint64_t>(dx * dx) + static_cast<std::uint64_t>(dy * dy);
    const std::int64_t distance = static_cast<std::int64_t>(integer_sqrt(squared));
    const std::int64_t outside = distance - radius;
    return std::clamp<std::int64_t>(half_unit - outside, 0, unit);
}

std::int64_t rounded_coverage(const FixedRect rect, const std::int64_t radius, const std::int64_t x,
                              const std::int64_t y) noexcept {
    const std::int64_t area = area_coverage(rect, x, y);
    if (area == 0 || radius == 0) return area;
    const std::int64_t centre_x = x * unit + half_unit;
    const std::int64_t centre_y = y * unit + half_unit;
    const std::int64_t inner_left = rect.left + radius;
    const std::int64_t inner_top = rect.top + radius;
    const std::int64_t inner_right = rect.right - radius;
    const std::int64_t inner_bottom = rect.bottom - radius;
    std::int64_t corner_x = 0;
    std::int64_t corner_y = 0;
    bool corner = false;
    if (centre_x < inner_left && centre_y < inner_top) { corner_x = inner_left; corner_y = inner_top; corner = true; }
    else if (centre_x > inner_right && centre_y < inner_top) { corner_x = inner_right; corner_y = inner_top; corner = true; }
    else if (centre_x < inner_left && centre_y > inner_bottom) { corner_x = inner_left; corner_y = inner_bottom; corner = true; }
    else if (centre_x > inner_right && centre_y > inner_bottom) { corner_x = inner_right; corner_y = inner_bottom; corner = true; }
    if (!corner) return area;
    const std::int64_t arc = corner_coverage(centre_x, centre_y, corner_x, corner_y, radius);
    return std::min(area, arc);
}

struct PreparedClip final {
    FixedRect rect{};
    bool rounded{};
    FixedRect rounded_rect{};
    std::int64_t radius{};
    bool outer_rounded{};
    FixedRect outer_rounded_rect{};
    std::int64_t outer_radius{};
};

// A rounded clip's radius in fixed point, held to half its shorter side.
std::int64_t prepared_radius(const FixedRect rect, const double radius) noexcept {
    const std::int64_t width = rect.right - rect.left;
    const std::int64_t height = rect.bottom - rect.top;
    const std::int64_t requested = std::llround(radius * static_cast<double>(unit));
    const std::int64_t held = std::clamp<std::int64_t>(requested, 0, std::min({width / 2, height / 2, radius_limit}));
    return held;
}

PreparedClip prepare_clip(const TextCompositeClip clip) noexcept {
    PreparedClip result{};
    result.rect = fixed_rect(clip.rect);
    result.rounded = clip.rounded;
    if (clip.rounded) {
        result.rounded_rect = fixed_rect(clip.rounded_rect);
        result.radius = prepared_radius(result.rounded_rect, clip.radius);
    }
    // The outer clip is read only beside an inner one.
    result.outer_rounded = clip.rounded && clip.outer_rounded;
    if (result.outer_rounded) {
        result.outer_rounded_rect = fixed_rect(clip.outer_rounded_rect);
        result.outer_radius = prepared_radius(result.outer_rounded_rect, clip.outer_radius);
    }
    return result;
}

std::int64_t prepared_clip_coverage(const PreparedClip& clip, const std::int64_t x, const std::int64_t y) noexcept {
    const std::int64_t rectangular = area_coverage(clip.rect, x, y);
    if (!clip.rounded || rectangular == 0) return rectangular;
    const std::int64_t rounded = rounded_coverage(clip.rounded_rect, clip.radius, x, y);
    const std::int64_t combined = (rectangular * rounded + half_unit) / unit;
    if (!clip.outer_rounded || combined == 0) return combined;
    const std::int64_t outer = rounded_coverage(clip.outer_rounded_rect, clip.outer_radius, x, y);
    const std::int64_t nested = (combined * outer + half_unit) / unit;
    return nested;
}

std::int64_t floor_pixel(const std::int64_t fixed) noexcept {
    std::int64_t quotient = fixed / unit;
    if (fixed % unit != 0 && fixed < 0) --quotient;
    return quotient;
}

std::int64_t ceil_pixel(const std::int64_t fixed) noexcept {
    std::int64_t quotient = fixed / unit;
    if (fixed % unit != 0 && fixed > 0) ++quotient;
    return quotient;
}

struct ChannelShifts final {
    unsigned red{};
    unsigned green{};
    unsigned blue{};
    unsigned alpha{};
};

// Targets are premultiplied BGRA8: as a little-endian word, blue is the low
// byte and red is at bit 16.
constexpr ChannelShifts bgra_shifts{16U, 8U, 0U, 24U};

// The sum of the two products is at most 255 * 255.
unsigned blend_channel(const unsigned source, const unsigned destination, const unsigned alpha) noexcept {
    const unsigned blended = rounded_quotient_by_255(source * alpha + destination * (255U - alpha));
    return blended;
}

// The rows and columns of [left, right) x [top, bottom) wholly inside a
// rectangular clip, whose coverage is a whole unit: the interior a blit
// takes without the coverage arithmetic. A rounded clip has none (every
// pixel takes the arithmetic); an empty interior has inner_left ==
// inner_right or inner_top == inner_bottom.
struct ClipInterior final {
    std::int64_t inner_left{};
    std::int64_t inner_right{};
    std::int64_t inner_top{};
    std::int64_t inner_bottom{};
};

ClipInterior clip_interior(const PreparedClip& prepared, const FixedRect limit, const std::int64_t left,
                           const std::int64_t top, const std::int64_t right, const std::int64_t bottom) noexcept {
    ClipInterior interior{right, right, bottom, bottom};
    if (prepared.rounded) return interior;
    interior.inner_left = std::min(right, std::max(left, ceil_pixel(limit.left)));
    interior.inner_right = std::max(interior.inner_left, std::min(right, floor_pixel(limit.right)));
    interior.inner_top = std::min(bottom, std::max(top, ceil_pixel(limit.top)));
    interior.inner_bottom = std::max(interior.inner_top, std::min(bottom, floor_pixel(limit.bottom)));
    return interior;
}

// Text: the per-pixel state of one call, and one pixel blended at `alpha`.
struct TextBlend final {
    unsigned red{};
    unsigned green{};
    unsigned blue{};
    unsigned alpha{};
    bool opaque{};
};

void blend_text_pixel(std::uint32_t& pixel, const TextBlend& blend, const unsigned alpha) noexcept {
    constexpr ChannelShifts shifts = bgra_shifts;
    const std::uint32_t previous = pixel;
    const unsigned red = blend_channel(blend.red, (previous >> shifts.red) & 255U, alpha);
    const unsigned green = blend_channel(blend.green, (previous >> shifts.green) & 255U, alpha);
    const unsigned blue = blend_channel(blend.blue, (previous >> shifts.blue) & 255U, alpha);
    unsigned result_alpha = 255U;
    if (!blend.opaque) result_alpha = blend_channel(blend.alpha, (previous >> shifts.alpha) & 255U, alpha);
    pixel = (red << shifts.red) | (green << shifts.green) | (blue << shifts.blue) | (result_alpha << shifts.alpha);
}

// Text pixels [from, to) of row y, each weighted by the clip's coverage.
void composite_text_edge_span(const TextCoverage& coverage, const TextCompositeTarget& target,
                              const PreparedClip& prepared, const TextBlend& blend, const std::uint64_t colour_alpha,
                              const std::int64_t y, const std::int64_t from, const std::int64_t to) noexcept {
    const std::size_t source_row = static_cast<std::size_t>(y - coverage.top) * coverage.width;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    for (std::int64_t x = from; x < to; ++x) {
        const std::uint64_t text = coverage.pixels[source_row + static_cast<std::size_t>(x - coverage.left)];
        if (text == 0) continue;
        const std::uint64_t clipped = static_cast<std::uint64_t>(prepared_clip_coverage(prepared, x, y));
        if (clipped == 0) continue;
        // One rounding for coverage x colour alpha x clip (unit = 256).
        const unsigned alpha = static_cast<unsigned>((text * colour_alpha * clipped + 32640U) / 65280U);
        if (alpha == 0) continue;
        blend_text_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], blend, alpha);
    }
}

// As composite_text_edge_span, for pixels wholly inside a rectangular clip:
// the alpha depends on the coverage byte alone, read from `alphas`, the
// call's table of it at a whole unit of clip coverage.
void composite_text_interior_span(const TextCoverage& coverage, const TextCompositeTarget& target,
                                  const TextBlend& blend, const std::array<std::uint8_t, 256>& alphas,
                                  const std::int64_t y, const std::int64_t from, const std::int64_t to) noexcept {
    const std::size_t source_row = static_cast<std::size_t>(y - coverage.top) * coverage.width;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    for (std::int64_t x = from; x < to; ++x) {
        const std::uint8_t text = coverage.pixels[source_row + static_cast<std::size_t>(x - coverage.left)];
        const unsigned alpha = alphas[text];
        if (alpha == 0) continue;
        blend_text_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], blend, alpha);
    }
}

} // namespace

std::uint32_t text_clip_coverage(const TextCompositeClip clip, const std::int64_t x, const std::int64_t y) noexcept {
    if (!valid_rect(clip.rect) || (clip.rounded && (!valid_rect(clip.rounded_rect) || !valid_coordinate(clip.radius))) ||
        (clip.rounded && clip.outer_rounded &&
         (!valid_rect(clip.outer_rounded_rect) || !valid_coordinate(clip.outer_radius)))) {
        return 0U;
    }
    const PreparedClip prepared = prepare_clip(clip);
    const std::int64_t coverage = prepared_clip_coverage(prepared, x, y);
    return static_cast<std::uint32_t>(coverage);
}

TextCompositeStatus composite_text_coverage(const TextCoverage coverage, const TextCompositeTarget target,
                                            const TextCompositeClip clip, const Color color,
                                            const TextCompositeMode mode) noexcept {
    if (target.width == 0 || target.height == 0 || target.stride < target.width ||
        target.height > target.pixels.size() / target.stride || !valid_rect(clip.rect) ||
        (clip.rounded && (!valid_rect(clip.rounded_rect) || !valid_coordinate(clip.radius) || clip.radius < 0.0)) ||
        (clip.rounded && clip.outer_rounded &&
         (!valid_rect(clip.outer_rounded_rect) || !valid_coordinate(clip.outer_radius) || clip.outer_radius < 0.0)) ||
        std::abs(coverage.left) > static_cast<std::int64_t>(coordinate_limit) ||
        std::abs(coverage.top) > static_cast<std::int64_t>(coordinate_limit)) {
        return TextCompositeStatus::invalid_geometry;
    }
    const bool source = mode == TextCompositeMode::source;
    if (coverage.width == 0 || coverage.height == 0 || (color.alpha == 0 && !source)) return TextCompositeStatus::success;
    if (coverage.height > coverage.pixels.size() / coverage.width) return TextCompositeStatus::invalid_geometry;
    const PreparedClip prepared = prepare_clip(clip);
    FixedRect limit = prepared.rect;
    if (prepared.rounded) {
        limit.left = std::max(limit.left, prepared.rounded_rect.left);
        limit.top = std::max(limit.top, prepared.rounded_rect.top);
        limit.right = std::min(limit.right, prepared.rounded_rect.right);
        limit.bottom = std::min(limit.bottom, prepared.rounded_rect.bottom);
    }
    if (prepared.outer_rounded) {
        limit.left = std::max(limit.left, prepared.outer_rounded_rect.left);
        limit.top = std::max(limit.top, prepared.outer_rounded_rect.top);
        limit.right = std::min(limit.right, prepared.outer_rounded_rect.right);
        limit.bottom = std::min(limit.bottom, prepared.outer_rounded_rect.bottom);
    }
    const std::int64_t left = std::max<std::int64_t>({0, floor_pixel(limit.left), coverage.left});
    const std::int64_t top = std::max<std::int64_t>({0, floor_pixel(limit.top), coverage.top});
    const std::int64_t right = std::min<std::int64_t>({static_cast<std::int64_t>(target.width), ceil_pixel(limit.right),
                                                       coverage.left + static_cast<std::int64_t>(coverage.width)});
    const std::int64_t bottom = std::min<std::int64_t>({static_cast<std::int64_t>(target.height), ceil_pixel(limit.bottom),
                                                        coverage.top + static_cast<std::int64_t>(coverage.height)});
    // source_over weights coverage by the colour's alpha; source carries the
    // alpha in the premultiplied colour instead.
    const std::uint64_t colour_alpha = source ? 255U : color.alpha;
    unsigned source_red = color.red;
    unsigned source_green = color.green;
    unsigned source_blue = color.blue;
    unsigned source_alpha = 255U;
    if (source) {
        source_red = rounded_quotient_by_255(color.red * static_cast<unsigned>(color.alpha));
        source_green = rounded_quotient_by_255(color.green * static_cast<unsigned>(color.alpha));
        source_blue = rounded_quotient_by_255(color.blue * static_cast<unsigned>(color.alpha));
        source_alpha = color.alpha;
    }
    const TextBlend blend{source_red, source_green, source_blue, source_alpha, target.opaque};
    // The interior's alpha per coverage byte (clip coverage a whole unit).
    std::array<std::uint8_t, 256> alphas{};
    for (std::size_t text = 0U; text < alphas.size(); ++text) {
        const std::uint64_t product = static_cast<std::uint64_t>(text) * colour_alpha * static_cast<std::uint64_t>(unit);
        alphas[text] = static_cast<std::uint8_t>((product + 32640U) / 65280U);
    }
    const ClipInterior interior = clip_interior(prepared, limit, left, top, right, bottom);
    for (std::int64_t y = top; y < bottom; ++y) {
        if (y < interior.inner_top || y >= interior.inner_bottom) {
            composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, left, right);
            continue;
        }
        composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, left, interior.inner_left);
        composite_text_interior_span(coverage, target, blend, alphas, y, interior.inner_left, interior.inner_right);
        composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, interior.inner_right, right);
    }
    return TextCompositeStatus::success;
}

std::uint8_t part_composite_opacity(const double opacity) noexcept {
    if (!(opacity > 0.0)) return 0U;
    const double clamped = std::min(opacity, 1.0);
    const double scaled = std::floor(clamped * 255.0 + 0.5);
    const std::uint8_t byte = static_cast<std::uint8_t>(scaled);
    return byte;
}

namespace {

// d * (255 - a) / 255, rounded: the destination's share under coverage a.
unsigned destination_share(const unsigned destination, const unsigned remaining) noexcept {
    const unsigned share = rounded_quotient_by_255(destination * remaining);
    return share;
}

// s * w / 65280, rounded: one premultiplied channel at weight w.
unsigned weighted_channel(const std::uint64_t channel, const std::uint64_t weight) noexcept {
    const unsigned weighted = static_cast<unsigned>((channel * weight + 32640U) / 65280U);
    return weighted;
}

// One channel by multiply or screen (PartCompositeBlend), in bytes:
// `source` and `destination` premultiplied, with their pixels' alphas.
unsigned separable_channel(const unsigned source, const unsigned source_alpha, const unsigned destination,
                           const unsigned destination_alpha, const PartCompositeBlend blend) noexcept {
    const unsigned product = source * destination;
    if (blend == PartCompositeBlend::screen) {
        const unsigned screened = source + destination - (product + 127U) / 255U;
        return std::min(255U, screened);
    }
    const unsigned kept = source * (255U - destination_alpha) + destination * (255U - source_alpha) + product;
    const unsigned multiplied = (kept + 127U) / 255U;
    return std::min(255U, multiplied);
}

// A target pixel joined by multiply or screen to the weighted premultiplied
// `source` (red, green, blue, alpha).
std::uint32_t separable_pixel(const std::uint32_t previous, const std::array<unsigned, 4>& source,
                              const ChannelShifts shifts, const bool opaque, const PartCompositeBlend blend) noexcept {
    const unsigned destination_alpha = (previous >> shifts.alpha) & 255U;
    const unsigned out_red =
        separable_channel(source[0], source[3], (previous >> shifts.red) & 255U, destination_alpha, blend);
    const unsigned out_green =
        separable_channel(source[1], source[3], (previous >> shifts.green) & 255U, destination_alpha, blend);
    const unsigned out_blue =
        separable_channel(source[2], source[3], (previous >> shifts.blue) & 255U, destination_alpha, blend);
    unsigned out_alpha = 255U;
    if (!opaque) {
        const unsigned joined = source[3] + destination_alpha - (source[3] * destination_alpha + 127U) / 255U;
        out_alpha = std::min(255U, joined);
    }
    const std::uint32_t joined_pixel = (out_red << shifts.red) | (out_green << shifts.green) |
                                       (out_blue << shifts.blue) | (out_alpha << shifts.alpha);
    return joined_pixel;
}

// A target pixel replaced by the premultiplied source at `weight` (copy):
// each channel s × w + d × (unit − w), rounded once each and clamped, so
// that full weight writes the source exactly, transparent included, and a
// partly covered edge pixel keeps the rest of the target.
std::uint32_t copied_pixel(const std::uint32_t previous, const std::array<std::uint64_t, 4>& colour,
                           const std::uint64_t weight, const ChannelShifts shifts, const bool opaque) noexcept {
    const std::uint64_t kept = 65280U - weight;
    const unsigned out_red =
        std::min(255U, weighted_channel(colour[0], weight) + weighted_channel((previous >> shifts.red) & 255U, kept));
    const unsigned out_green = std::min(
        255U, weighted_channel(colour[1], weight) + weighted_channel((previous >> shifts.green) & 255U, kept));
    const unsigned out_blue =
        std::min(255U, weighted_channel(colour[2], weight) + weighted_channel((previous >> shifts.blue) & 255U, kept));
    unsigned out_alpha = 255U;
    if (!opaque) {
        out_alpha = std::min(
            255U, weighted_channel(colour[3], weight) + weighted_channel((previous >> shifts.alpha) & 255U, kept));
    }
    const std::uint32_t copied = (out_red << shifts.red) | (out_green << shifts.green) | (out_blue << shifts.blue) |
                                 (out_alpha << shifts.alpha);
    return copied;
}

// One premultiplied source pixel blended over a target pixel at `weight`
// (opacity 0..255 times clip coverage 0..256): see composite_part_raster.
void blend_part_pixel(std::uint32_t& pixel, const std::array<std::uint64_t, 4>& colour, const std::uint64_t weight,
                      const ChannelShifts shifts, const bool opaque, const PartCompositeBlend blend) noexcept {
    if (blend == PartCompositeBlend::copy) {
        pixel = copied_pixel(pixel, colour, weight, shifts, opaque);
        return;
    }
    const unsigned alpha = weighted_channel(colour[3], weight);
    if (alpha == 0) return;
    const unsigned red = weighted_channel(colour[0], weight);
    const unsigned green = weighted_channel(colour[1], weight);
    const unsigned blue = weighted_channel(colour[2], weight);
    const std::uint32_t previous = pixel;
    if (blend == PartCompositeBlend::multiply || blend == PartCompositeBlend::screen) {
        const std::array<unsigned, 4> source{red, green, blue, alpha};
        pixel = separable_pixel(previous, source, shifts, opaque, blend);
        return;
    }
    if (blend == PartCompositeBlend::plus) {
        const unsigned sum_red = std::min(255U, red + ((previous >> shifts.red) & 255U));
        const unsigned sum_green = std::min(255U, green + ((previous >> shifts.green) & 255U));
        const unsigned sum_blue = std::min(255U, blue + ((previous >> shifts.blue) & 255U));
        unsigned sum_alpha = 255U;
        if (!opaque) sum_alpha = std::min(255U, alpha + ((previous >> shifts.alpha) & 255U));
        pixel = (sum_red << shifts.red) | (sum_green << shifts.green) | (sum_blue << shifts.blue) |
                (sum_alpha << shifts.alpha);
        return;
    }
    const unsigned remaining = 255U - alpha;
    const unsigned out_red = std::min(255U, red + destination_share((previous >> shifts.red) & 255U, remaining));
    const unsigned out_green = std::min(255U, green + destination_share((previous >> shifts.green) & 255U, remaining));
    const unsigned out_blue = std::min(255U, blue + destination_share((previous >> shifts.blue) & 255U, remaining));
    unsigned out_alpha = 255U;
    if (!opaque) out_alpha = std::min(255U, alpha + destination_share((previous >> shifts.alpha) & 255U, remaining));
    pixel = (out_red << shifts.red) | (out_green << shifts.green) | (out_blue << shifts.blue) |
            (out_alpha << shifts.alpha);
}

// Where device pixel `shown` (counted from the shown edge) samples the
// source along one axis: its centre mapped onto `source_length` pixels,
// less half a pixel, in 1/256 of a source pixel. The first of its two
// texels is position >> 8 and the second's weight is position & 255
// (arithmetic shift and two's complement, as C++20 defines them).
std::int64_t sample_position(const std::int64_t shown, const std::uint32_t source_length,
                             const std::uint32_t shown_length) noexcept {
    const std::int64_t numerator = (2 * shown + 1) * static_cast<std::int64_t>(source_length) * 256;
    const std::int64_t position = numerator / (2 * static_cast<std::int64_t>(shown_length)) - 128;
    return position;
}

// sample_position along a row without a division per column: position is
// quotient - 128, where quotient and remainder are numerator / divisor and
// numerator % divisor, and each column adds `step` to the numerator, carried
// as whole quotients (step_quotient) and a remainder (step_remainder).
struct SampleStepper final {
    std::int64_t position{};
    std::int64_t remainder{};
    std::int64_t divisor{};
    std::int64_t step_quotient{};
    std::int64_t step_remainder{};
};

SampleStepper sample_stepper(const std::int64_t shown, const std::uint32_t source_length,
                             const std::uint32_t shown_length) noexcept {
    SampleStepper stepper{};
    stepper.divisor = 2 * static_cast<std::int64_t>(shown_length);
    const std::int64_t numerator = (2 * shown + 1) * static_cast<std::int64_t>(source_length) * 256;
    stepper.position = numerator / stepper.divisor - 128;
    stepper.remainder = numerator % stepper.divisor;
    const std::int64_t step = 2 * static_cast<std::int64_t>(source_length) * 256;
    stepper.step_quotient = step / stepper.divisor;
    stepper.step_remainder = step % stepper.divisor;
    return stepper;
}

void advance_sample(SampleStepper& stepper) noexcept {
    stepper.position += stepper.step_quotient;
    stepper.remainder += stepper.step_remainder;
    if (stepper.remainder >= stepper.divisor) {
        stepper.remainder -= stepper.divisor;
        ++stepper.position;
    }
}

// One source channel at texel (x, y): 0 beyond the source's edges.
std::uint64_t texel_channel(const PartRasterSource& source, const std::int64_t x, const std::int64_t y,
                            const std::size_t byte) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(source.width) ||
        y >= static_cast<std::int64_t>(source.height)) {
        return 0U;
    }
    const std::size_t offset = static_cast<std::size_t>(y) * source.row_bytes + static_cast<std::size_t>(x) * 4U + byte;
    const std::uint64_t channel = source.pixels[offset];
    return channel;
}

// Whether every row of the source lies inside its buffer: the last row
// starts at (height - 1) * row_bytes and needs width * 4 bytes.
bool part_source_fits(const PartRasterSource& source) noexcept {
    const std::size_t row_pixel_bytes = static_cast<std::size_t>(source.width) * 4U;
    if (source.row_bytes < row_pixel_bytes || source.pixels.size() < row_pixel_bytes) return false;
    const std::size_t last_row = static_cast<std::size_t>(source.height) - 1U;
    const std::size_t room = source.pixels.size() - row_pixel_bytes;
    const bool fits = last_row <= room / source.row_bytes;
    return fits;
}

// The source's red, green, blue and alpha bytes within a BGRA pixel.
constexpr std::array<std::size_t, 4> part_bytes{2U, 1U, 0U, 3U};

// Unscaled pixels [from, to) of target row y, each weighted by the clip's
// coverage of it. Every other blend than copy leaves the target as it is
// under a transparent source pixel.
void composite_part_edge_span(const PartRasterSource& source, const TextCompositeTarget& target,
                              const PreparedClip& prepared, const std::int64_t y, const std::int64_t from,
                              const std::int64_t to, const std::uint64_t opacity_weight) noexcept {
    const bool skip_transparent = source.blend != PartCompositeBlend::copy;
    const std::size_t source_row = static_cast<std::size_t>(y - source.top) * source.row_bytes;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    for (std::int64_t x = from; x < to; ++x) {
        const std::size_t offset = source_row + static_cast<std::size_t>(x - source.left) * 4U;
        const std::uint64_t source_alpha = source.pixels[offset + part_bytes[3]];
        if (skip_transparent && source_alpha == 0) continue;
        const std::uint64_t clipped = static_cast<std::uint64_t>(prepared_clip_coverage(prepared, x, y));
        if (clipped == 0) continue;
        // One weight for opacity x clip (unit = 256), applied to every
        // premultiplied channel with one rounding each.
        const std::array<std::uint64_t, 4> colour{source.pixels[offset + part_bytes[0]],
                                                source.pixels[offset + part_bytes[1]],
                                                source.pixels[offset + part_bytes[2]], source_alpha};
        blend_part_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], colour, opacity_weight * clipped,
                         bgra_shifts, target.opaque, source.blend);
    }
}

// As composite_part_edge_span, for pixels wholly inside a rectangular
// clip: their coverage is a whole unit. With `copy_opaque` (source-over at
// full opacity) an opaque source pixel is the result itself: its weighted
// channels are its own and the target keeps no share, so its BGRA bytes
// are stored as the target's BGRA word.
//
// Source-over (the common blend) reads each weighted channel from
// `weighted`, the call's table of weighted_channel(c, opacity x unit) per
// byte c, and blends inline; the other blends take blend_part_pixel.
void composite_part_interior_span(const PartRasterSource& source, const TextCompositeTarget& target,
                                  const std::int64_t y, const std::int64_t from, const std::int64_t to,
                                  const std::uint64_t opacity_weight, const bool copy_opaque,
                                  const std::array<std::uint8_t, 256>& weighted) noexcept {
    if (source.blend == PartCompositeBlend::source_over) {
        constexpr ChannelShifts shifts = bgra_shifts;
        const std::size_t source_row = static_cast<std::size_t>(y - source.top) * source.row_bytes;
        const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
        for (std::int64_t x = from; x < to; ++x) {
            const std::size_t offset = source_row + static_cast<std::size_t>(x - source.left) * 4U;
            const std::uint8_t source_alpha = source.pixels[offset + part_bytes[3]];
            if (source_alpha == 0U) continue;
            std::uint32_t& pixel = target.pixels[target_row + static_cast<std::size_t>(x)];
            if (copy_opaque && source_alpha == 255U) {
                std::memcpy(&pixel, &source.pixels[offset], sizeof(std::uint32_t));
                continue;
            }
            const unsigned alpha = weighted[source_alpha];
            if (alpha == 0U) continue;
            const unsigned remaining = 255U - alpha;
            const std::uint32_t previous = pixel;
            const unsigned red = weighted[source.pixels[offset + part_bytes[0]]];
            const unsigned green = weighted[source.pixels[offset + part_bytes[1]]];
            const unsigned blue = weighted[source.pixels[offset + part_bytes[2]]];
            const unsigned out_red = std::min(255U, red + destination_share((previous >> shifts.red) & 255U, remaining));
            const unsigned out_green =
                std::min(255U, green + destination_share((previous >> shifts.green) & 255U, remaining));
            const unsigned out_blue =
                std::min(255U, blue + destination_share((previous >> shifts.blue) & 255U, remaining));
            unsigned out_alpha = 255U;
            if (!target.opaque) {
                out_alpha = std::min(255U, alpha + destination_share((previous >> shifts.alpha) & 255U, remaining));
            }
            pixel = (out_red << shifts.red) | (out_green << shifts.green) | (out_blue << shifts.blue) |
                    (out_alpha << shifts.alpha);
        }
        return;
    }
    const bool skip_transparent = source.blend != PartCompositeBlend::copy;
    const std::uint64_t weight = opacity_weight * static_cast<std::uint64_t>(unit);
    const std::size_t source_row = static_cast<std::size_t>(y - source.top) * source.row_bytes;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    for (std::int64_t x = from; x < to; ++x) {
        const std::size_t offset = source_row + static_cast<std::size_t>(x - source.left) * 4U;
        const std::uint64_t source_alpha = source.pixels[offset + part_bytes[3]];
        if (skip_transparent && source_alpha == 0) continue;
        std::uint32_t& pixel = target.pixels[target_row + static_cast<std::size_t>(x)];
        if (copy_opaque && source_alpha == 255U) {
            std::memcpy(&pixel, &source.pixels[offset], sizeof(std::uint32_t));
            continue;
        }
        const std::array<std::uint64_t, 4> colour{source.pixels[offset + part_bytes[0]],
                                                source.pixels[offset + part_bytes[1]],
                                                source.pixels[offset + part_bytes[2]], source_alpha};
        blend_part_pixel(pixel, colour, weight, bgra_shifts, target.opaque, source.blend);
    }
}

} // namespace

TextCompositeStatus composite_part_raster(const PartRasterSource source, const TextCompositeTarget target,
                                          const TextCompositeClip clip, const std::uint8_t opacity) noexcept {
    if (target.width == 0 || target.height == 0 || target.stride < target.width ||
        target.height > target.pixels.size() / target.stride || !valid_rect(clip.rect) ||
        (clip.rounded && (!valid_rect(clip.rounded_rect) || !valid_coordinate(clip.radius) || clip.radius < 0.0)) ||
        (clip.rounded && clip.outer_rounded &&
         (!valid_rect(clip.outer_rounded_rect) || !valid_coordinate(clip.outer_radius) || clip.outer_radius < 0.0)) ||
        std::abs(source.left) > static_cast<std::int64_t>(coordinate_limit) ||
        std::abs(source.top) > static_cast<std::int64_t>(coordinate_limit) ||
        static_cast<double>(source.shown_width) > coordinate_limit ||
        static_cast<double>(source.shown_height) > coordinate_limit) {
        return TextCompositeStatus::invalid_geometry;
    }
    if (source.width == 0 || source.height == 0) return TextCompositeStatus::success;
    if (!part_source_fits(source)) return TextCompositeStatus::invalid_geometry;
    if (opacity == 0U) return TextCompositeStatus::success;
    const PreparedClip prepared = prepare_clip(clip);
    FixedRect limit = prepared.rect;
    if (prepared.rounded) {
        limit.left = std::max(limit.left, prepared.rounded_rect.left);
        limit.top = std::max(limit.top, prepared.rounded_rect.top);
        limit.right = std::min(limit.right, prepared.rounded_rect.right);
        limit.bottom = std::min(limit.bottom, prepared.rounded_rect.bottom);
    }
    if (prepared.outer_rounded) {
        limit.left = std::max(limit.left, prepared.outer_rounded_rect.left);
        limit.top = std::max(limit.top, prepared.outer_rounded_rect.top);
        limit.right = std::min(limit.right, prepared.outer_rounded_rect.right);
        limit.bottom = std::min(limit.bottom, prepared.outer_rounded_rect.bottom);
    }
    const bool scaled = (source.shown_width != 0U && source.shown_width != source.width) ||
                        (source.shown_height != 0U && source.shown_height != source.height);
    const std::uint32_t shown_width = source.shown_width != 0U ? source.shown_width : source.width;
    const std::uint32_t shown_height = source.shown_height != 0U ? source.shown_height : source.height;
    const std::int64_t left = std::max<std::int64_t>({0, floor_pixel(limit.left), source.left});
    const std::int64_t top = std::max<std::int64_t>({0, floor_pixel(limit.top), source.top});
    const std::int64_t right = std::min<std::int64_t>({static_cast<std::int64_t>(target.width), ceil_pixel(limit.right),
                                                       source.left + static_cast<std::int64_t>(shown_width)});
    const std::int64_t bottom = std::min<std::int64_t>({static_cast<std::int64_t>(target.height), ceil_pixel(limit.bottom),
                                                        source.top + static_cast<std::int64_t>(shown_height)});
    constexpr ChannelShifts shifts = bgra_shifts;
    // The source's red and blue bytes within a BGRA pixel.
    constexpr std::size_t red_byte = 2U;
    constexpr std::size_t blue_byte = 0U;
    constexpr std::array<std::size_t, 4> bytes{red_byte, 1U, blue_byte, 3U};
    const std::uint64_t opacity_weight = opacity;
    // Every other blend leaves the target as it is under a transparent
    // source pixel; copy writes it.
    const bool skip_transparent = source.blend != PartCompositeBlend::copy;
    if (!scaled) {
        // Under a rectangular clip, the pixels wholly inside it skip the
        // coverage arithmetic; the rows and columns it cuts take it. A
        // rounded clip leaves no interior: every pixel takes it.
        const ClipInterior interior = clip_interior(prepared, limit, left, top, right, bottom);
        const bool copy_opaque = source.blend == PartCompositeBlend::source_over && opacity == 255U;
        // The interior's weighted channel per byte (one weight per call).
        std::array<std::uint8_t, 256> weighted{};
        const std::uint64_t interior_weight = opacity_weight * static_cast<std::uint64_t>(unit);
        for (std::size_t channel = 0U; channel < weighted.size(); ++channel) {
            weighted[channel] = static_cast<std::uint8_t>(weighted_channel(channel, interior_weight));
        }
        for (std::int64_t y = top; y < bottom; ++y) {
            if (y < interior.inner_top || y >= interior.inner_bottom) {
                composite_part_edge_span(source, target, prepared, y, left, right, opacity_weight);
                continue;
            }
            composite_part_edge_span(source, target, prepared, y, left, interior.inner_left, opacity_weight);
            composite_part_interior_span(source, target, y, interior.inner_left, interior.inner_right, opacity_weight,
                                         copy_opaque, weighted);
            composite_part_edge_span(source, target, prepared, y, interior.inner_right, right, opacity_weight);
        }
        return TextCompositeStatus::success;
    }
    // Scaled: each device pixel samples its four texels bilinearly. Along
    // a row the sample position is stepped exactly (a quotient and a
    // remainder carried from one column to the next, as sample_position
    // divides), and the four texels of a sample wholly inside the source
    // are fetched without the edge tests.
    const SampleStepper first_column = sample_stepper(left - source.left, source.width, shown_width);
    for (std::int64_t y = top; y < bottom; ++y) {
        const std::int64_t down = sample_position(y - source.top, source.height, shown_height);
        const std::int64_t texel_y = down >> 8;
        const std::uint64_t below = static_cast<std::uint64_t>(down & 255);
        const std::uint64_t above = 256U - below;
        const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
        const bool rows_inside = texel_y >= 0 && texel_y + 1 < static_cast<std::int64_t>(source.height);
        SampleStepper column = first_column;
        for (std::int64_t x = left; x < right; ++x, advance_sample(column)) {
            const std::uint64_t clipped = static_cast<std::uint64_t>(prepared_clip_coverage(prepared, x, y));
            if (clipped == 0) continue;
            const std::int64_t across = column.position;
            const std::int64_t texel_x = across >> 8;
            const std::uint64_t after = static_cast<std::uint64_t>(across & 255);
            const std::uint64_t before = 256U - after;
            std::array<std::uint64_t, 4> colour{};
            if (rows_inside && texel_x >= 0 && texel_x + 1 < static_cast<std::int64_t>(source.width)) {
                const std::size_t upper_offset =
                    static_cast<std::size_t>(texel_y) * source.row_bytes + static_cast<std::size_t>(texel_x) * 4U;
                const std::size_t lower_offset = upper_offset + source.row_bytes;
                for (std::size_t channel = 0U; channel < 4U; ++channel) {
                    const std::size_t byte = bytes[channel];
                    const std::uint64_t upper = static_cast<std::uint64_t>(source.pixels[upper_offset + byte]) * before +
                                                static_cast<std::uint64_t>(source.pixels[upper_offset + 4U + byte]) * after;
                    const std::uint64_t lower = static_cast<std::uint64_t>(source.pixels[lower_offset + byte]) * before +
                                                static_cast<std::uint64_t>(source.pixels[lower_offset + 4U + byte]) * after;
                    colour[channel] = (upper * above + lower * below + 32768U) >> 16U;
                }
            } else {
                for (std::size_t channel = 0U; channel < 4U; ++channel) {
                    const std::size_t byte = bytes[channel];
                    const std::uint64_t upper = texel_channel(source, texel_x, texel_y, byte) * before +
                                                texel_channel(source, texel_x + 1, texel_y, byte) * after;
                    const std::uint64_t lower = texel_channel(source, texel_x, texel_y + 1, byte) * before +
                                                texel_channel(source, texel_x + 1, texel_y + 1, byte) * after;
                    colour[channel] = (upper * above + lower * below + 32768U) >> 16U;
                }
            }
            if (skip_transparent && colour[3] == 0U) continue;
            blend_part_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], colour, opacity_weight * clipped,
                             shifts, target.opaque, source.blend);
        }
    }
    return TextCompositeStatus::success;
}

PartCompositeBlend part_composite_blend(const PaintGroupComposite composite) noexcept {
    switch (composite) {
    case PaintGroupComposite::source_over: return PartCompositeBlend::source_over;
    case PaintGroupComposite::plus: return PartCompositeBlend::plus;
    case PaintGroupComposite::multiply: return PartCompositeBlend::multiply;
    case PaintGroupComposite::screen: return PartCompositeBlend::screen;
    case PaintGroupComposite::copy: return PartCompositeBlend::copy;
    }
    return PartCompositeBlend::source_over;
}

} // namespace gui_forms::detail
