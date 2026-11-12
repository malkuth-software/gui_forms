#include "text_composite.hpp"

#include "gui_forms/types/painter/painter.hpp"

#include "../../types/restrict/restrict.hpp"

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

// The pixels of [left, right) x [top, bottom) whose clip coverage is a
// whole unit: the interior a blit takes without the coverage arithmetic.
// Such a pixel lies wholly inside the clip rectangle and every rounded
// rectangle (`limit` is their intersection), and under a rounded clip its
// centre lies in none of the corner squares, the only place
// rounded_coverage reads the arc. So rows [band_top, band_bottom) are
// interior across [inner_left, inner_right), and the other interior rows
// only across [band_left, band_right), the columns clear of the corners.
// Without a rounded clip, or at radius 0, the bands are the whole
// interior. Nested rounded clips narrow the bands to both clips' corners,
// which may leave out pixels that are interior; they take the exact
// per-pixel path. A range is empty when its end is at or before its start.
struct ClipInterior final {
    std::int64_t inner_left{};
    std::int64_t inner_right{};
    std::int64_t inner_top{};
    std::int64_t inner_bottom{};
    std::int64_t band_left{};
    std::int64_t band_right{};
    std::int64_t band_top{};
    std::int64_t band_bottom{};
};

// Narrows the bands to the columns whose pixel centres x * unit +
// half_unit lie within [left + radius, right - radius] of one rounded
// rectangle, and the rows likewise down: outside its corner squares.
void clear_of_corners(ClipInterior& interior, const FixedRect rect, const std::int64_t radius) noexcept {
    if (radius == 0) return;
    const std::int64_t first_column = ceil_pixel(rect.left + radius - half_unit);
    const std::int64_t end_column = floor_pixel(rect.right - radius - half_unit) + 1;
    const std::int64_t first_row = ceil_pixel(rect.top + radius - half_unit);
    const std::int64_t end_row = floor_pixel(rect.bottom - radius - half_unit) + 1;
    const std::int64_t band_left = std::max(interior.band_left, first_column);
    interior.band_left = std::clamp(band_left, interior.inner_left, interior.inner_right);
    const std::int64_t band_right = std::min(interior.band_right, end_column);
    interior.band_right = std::clamp(band_right, interior.band_left, interior.inner_right);
    const std::int64_t band_top = std::max(interior.band_top, first_row);
    interior.band_top = std::clamp(band_top, interior.inner_top, interior.inner_bottom);
    const std::int64_t band_bottom = std::min(interior.band_bottom, end_row);
    interior.band_bottom = std::clamp(band_bottom, interior.band_top, interior.inner_bottom);
}

ClipInterior clip_interior(const PreparedClip& prepared, const FixedRect limit, const std::int64_t left,
                           const std::int64_t top, const std::int64_t right, const std::int64_t bottom) noexcept {
    ClipInterior interior{};
    interior.inner_left = std::min(right, std::max(left, ceil_pixel(limit.left)));
    interior.inner_right = std::max(interior.inner_left, std::min(right, floor_pixel(limit.right)));
    interior.inner_top = std::min(bottom, std::max(top, ceil_pixel(limit.top)));
    interior.inner_bottom = std::max(interior.inner_top, std::min(bottom, floor_pixel(limit.bottom)));
    interior.band_left = interior.inner_left;
    interior.band_right = interior.inner_right;
    interior.band_top = interior.inner_top;
    interior.band_bottom = interior.inner_bottom;
    if (prepared.rounded) clear_of_corners(interior, prepared.rounded_rect, prepared.radius);
    if (prepared.outer_rounded) clear_of_corners(interior, prepared.outer_rounded_rect, prepared.outer_radius);
    return interior;
}

// The interior columns [from, to) of row y; empty (from >= to) for a row
// outside the interior.
struct InteriorSpan final {
    std::int64_t from{};
    std::int64_t to{};
};

InteriorSpan interior_span(const ClipInterior& interior, const std::int64_t y) noexcept {
    InteriorSpan span{};
    if (y < interior.inner_top || y >= interior.inner_bottom) return span;
    const bool whole_row = y >= interior.band_top && y < interior.band_bottom;
    span.from = whole_row ? interior.inner_left : interior.band_left;
    span.to = whole_row ? interior.inner_right : interior.band_right;
    return span;
}

// -- the interior's block kernels ---------------------------------------------------
//
// Interior pixels are worked in blocks of composite_block_pixels through
// scratch made once per call: one pass prepares a block's operands as
// bytes and notes whether the block is wholly clear or wholly opaque, then
// one flat pass over the bytes does the arithmetic in 16-bit lanes with no
// decision inside it, which the compiler vectorizes. The arithmetic is the
// per-pixel path's exactly, by two identities over bytes that
// part_composite_tests checks exhaustively:
//   rounded_quotient_by_255(v) == (v + 127) / 255 for v in 0..65025, with
//   every step inside 16 bits;
//   (c * o * 256 + 32640) / 65280 == rounded_quotient_by_255(c * o) for
//   bytes c and o: a channel weighted at opacity o and a whole unit of clip
//   coverage is the rounded product of the two bytes.
// Every kernel's ranges are a block of stack scratch and a source or target
// run, which never overlap; read-only ranges may share bytes.

constexpr std::size_t composite_block_pixels = 64U;
constexpr std::size_t composite_block_bytes = composite_block_pixels * 4U;

// rounded_quotient_by_255 in a 16-bit lane, for value in 0..65025.
std::uint16_t lane_quotient_by_255(const std::uint16_t value) noexcept {
    const std::uint16_t biased = static_cast<std::uint16_t>(value + 128U);
    const std::uint16_t carried = static_cast<std::uint16_t>(biased + (biased >> 8U));
    const std::uint16_t quotient = static_cast<std::uint16_t>(carried >> 8U);
    return quotient;
}

// out[i] = round(in[i] * weight / 255) over `bytes` bytes.
void weight_bytes(const std::uint8_t* GUI_FORMS_RESTRICT in, std::uint8_t* GUI_FORMS_RESTRICT out,
                  const std::size_t bytes, const std::uint16_t weight) noexcept {
    for (std::size_t index = 0U; index < bytes; ++index) {
        const std::uint16_t product = static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[index]) * weight);
        out[index] = static_cast<std::uint8_t>(lane_quotient_by_255(product));
    }
}

// target[i] = min(255, added[i] + round(target[i] * remaining[i] / 255))
// over `bytes` bytes: source-over of operands prepare_over_block made.
void over_bytes(const std::uint8_t* GUI_FORMS_RESTRICT added, const std::uint8_t* GUI_FORMS_RESTRICT remaining,
                std::uint8_t* GUI_FORMS_RESTRICT target, const std::size_t bytes) noexcept {
    for (std::size_t index = 0U; index < bytes; ++index) {
        const std::uint16_t product =
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(target[index]) * remaining[index]);
        const std::uint16_t share = lane_quotient_by_255(product);
        const std::uint16_t sum = static_cast<std::uint16_t>(added[index] + share);
        target[index] = static_cast<std::uint8_t>(std::min<std::uint16_t>(sum, 255U));
    }
}

// target[i] = round((colour[i] * alpha[i] + target[i] * (255 - alpha[i])) /
// 255) over `bytes` bytes: text's blend_channel, lane by lane.
void text_bytes(const std::uint8_t* GUI_FORMS_RESTRICT colour, const std::uint8_t* GUI_FORMS_RESTRICT alpha,
                std::uint8_t* GUI_FORMS_RESTRICT target, const std::size_t bytes) noexcept {
    for (std::size_t index = 0U; index < bytes; ++index) {
        const std::uint16_t weight = alpha[index];
        const std::uint16_t remaining = static_cast<std::uint16_t>(255U - weight);
        const std::uint16_t value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(colour[index]) * weight +
                                                               static_cast<std::uint16_t>(target[index]) * remaining);
        target[index] = static_cast<std::uint8_t>(lane_quotient_by_255(value));
    }
}

// A block's whole-block facts: the AND and the OR of its alphas, so 255 in
// `every` when every pixel is opaque and 0 in `any` when every pixel is
// clear.
struct BlockAlpha final {
    std::uint32_t every{};
    std::uint32_t any{};
};

// From `pixels` premultiplied BGRA words at `source` (weighted already),
// over_bytes' operands: `added`, the source as source-over adds it, cleared
// where its alpha is 0 (which leaves the target as it is) and with alpha
// byte 255 on an opaque target; and `remaining`, 255 - alpha in each byte.
BlockAlpha prepare_over_block(const std::uint8_t* GUI_FORMS_RESTRICT source, std::uint8_t* GUI_FORMS_RESTRICT added,
                              std::uint8_t* GUI_FORMS_RESTRICT remaining, const std::size_t pixels,
                              const bool opaque) noexcept {
    const std::uint32_t forced = opaque ? 0xff000000U : 0U;
    std::uint32_t every = 255U;
    std::uint32_t any = 0U;
    for (std::size_t index = 0U; index < pixels; ++index) {
        std::uint32_t pixel = 0U;
        std::memcpy(&pixel, source + index * 4U, sizeof(pixel));
        const std::uint32_t alpha = pixel >> bgra_shifts.alpha;
        every &= alpha;
        any |= alpha;
        const std::uint32_t kept = alpha == 0U ? 0U : 0xffffffffU;
        const std::uint32_t add = (pixel | forced) & kept;
        const std::uint32_t rest = (255U - alpha) * 0x01010101U;
        std::memcpy(added + index * 4U, &add, sizeof(add));
        std::memcpy(remaining + index * 4U, &rest, sizeof(rest));
    }
    BlockAlpha result{};
    result.every = every;
    result.any = any;
    return result;
}

// Scratch for the source-over blocks, made once per composite call.
struct OverScratch final {
    alignas(64) std::array<std::uint8_t, composite_block_bytes> weighted{};
    alignas(64) std::array<std::uint8_t, composite_block_bytes> added{};
    alignas(64) std::array<std::uint8_t, composite_block_bytes> remaining{};
    // A scaled source's samples of one block.
    alignas(64) std::array<std::uint8_t, composite_block_bytes> sampled{};
};

// `pixels` interior pixels of premultiplied BGRA `source` blended
// source-over at `opacity` onto `target`, block by block: a wholly clear
// block writes nothing, a wholly opaque one is copied. `source` is a part
// raster's row or scratch.sampled; it does not overlap the target
// (composite_part_raster's contract).
void over_row(const std::uint8_t* source, std::uint8_t* target, std::size_t pixels, const std::uint8_t opacity,
              const bool opaque, OverScratch& scratch) noexcept {
    while (pixels != 0U) {
        const std::size_t count = std::min(pixels, composite_block_pixels);
        const std::size_t bytes = count * 4U;
        const std::uint8_t* operand = source;
        if (opacity != 255U) {
            weight_bytes(source, scratch.weighted.data(), bytes, opacity);
            operand = scratch.weighted.data();
        }
        const BlockAlpha alpha = prepare_over_block(operand, scratch.added.data(), scratch.remaining.data(), count,
                                                    opaque);
        if (alpha.every == 255U) {
            std::memcpy(target, operand, bytes);
        } else if (alpha.any != 0U) {
            over_bytes(scratch.added.data(), scratch.remaining.data(), target, bytes);
        }
        source += bytes;
        target += bytes;
        pixels -= count;
    }
}

// Scratch for the text blocks, made once per composite call: the blend's
// colour as BGRA bytes repeated across a block (alpha byte 255 on an
// opaque target), a block's weighted coverage, and its alpha per byte.
struct TextScratch final {
    alignas(64) std::array<std::uint8_t, composite_block_bytes> pattern{};
    alignas(64) std::array<std::uint8_t, composite_block_pixels> weighted{};
    alignas(64) std::array<std::uint8_t, composite_block_bytes> alphas{};
};

// `pixels` interior coverage bytes blended in scratch.pattern's colour onto
// `target`, block by block, at alpha round(coverage * colour_alpha / 255);
// a wholly clear block writes nothing. On an opaque target the alpha byte
// becomes 255 wherever the pixel is touched at all.
void text_row(const std::uint8_t* coverage, std::uint8_t* target, std::size_t pixels, const std::uint8_t colour_alpha,
              const bool opaque, TextScratch& scratch) noexcept {
    while (pixels != 0U) {
        const std::size_t count = std::min(pixels, composite_block_pixels);
        const std::uint8_t* weights = coverage;
        if (colour_alpha != 255U) {
            weight_bytes(coverage, scratch.weighted.data(), count, colour_alpha);
            weights = scratch.weighted.data();
        }
        std::uint32_t any = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            const std::uint32_t alpha = weights[index];
            any |= alpha;
            std::uint32_t top = alpha;
            if (opaque) top = alpha == 0U ? 0U : 255U;
            const std::uint32_t lanes = (alpha * 0x00010101U) | (top << bgra_shifts.alpha);
            std::memcpy(scratch.alphas.data() + index * 4U, &lanes, sizeof(lanes));
        }
        if (any != 0U) text_bytes(scratch.pattern.data(), scratch.alphas.data(), target, count * 4U);
        coverage += count;
        target += count * 4U;
        pixels -= count;
    }
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

// As composite_text_edge_span, for interior pixels (clip coverage a whole
// unit), through the block kernels.
void composite_text_interior_span(const TextCoverage& coverage, const TextCompositeTarget& target,
                                  const std::uint8_t colour_alpha, const std::int64_t y, const InteriorSpan span,
                                  TextScratch& scratch) noexcept {
    const std::size_t source_offset = static_cast<std::size_t>(y - coverage.top) * coverage.width +
                                      static_cast<std::size_t>(span.from - coverage.left);
    const std::size_t target_offset = static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(span.from);
    const std::uint8_t* const run = coverage.pixels.data() + source_offset;
    // The target's words as bytes, BGRA in memory (unsigned char may alias them).
    std::uint8_t* const destination = reinterpret_cast<std::uint8_t*>(target.pixels.data() + target_offset);
    const std::size_t pixels = static_cast<std::size_t>(span.to - span.from);
    text_row(run, destination, pixels, colour_alpha, target.opaque, scratch);
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
    // The interior's colour across one block, BGRA; the alpha byte is the
    // blend's alpha, or 255 on an opaque target (text_row's alpha lane then
    // reaches 255 wherever coverage touches the pixel).
    TextScratch scratch{};
    const std::uint8_t pattern_alpha = target.opaque ? std::uint8_t{255U} : static_cast<std::uint8_t>(blend.alpha);
    for (std::size_t pixel = 0U; pixel < composite_block_pixels; ++pixel) {
        scratch.pattern[pixel * 4U + bgra_shifts.blue / 8U] = static_cast<std::uint8_t>(blend.blue);
        scratch.pattern[pixel * 4U + bgra_shifts.green / 8U] = static_cast<std::uint8_t>(blend.green);
        scratch.pattern[pixel * 4U + bgra_shifts.red / 8U] = static_cast<std::uint8_t>(blend.red);
        scratch.pattern[pixel * 4U + bgra_shifts.alpha / 8U] = pattern_alpha;
    }
    const std::uint8_t interior_alpha = static_cast<std::uint8_t>(colour_alpha);
    const ClipInterior interior = clip_interior(prepared, limit, left, top, right, bottom);
    for (std::int64_t y = top; y < bottom; ++y) {
        const InteriorSpan span = interior_span(interior, y);
        if (span.from >= span.to) {
            composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, left, right);
            continue;
        }
        composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, left, span.from);
        composite_text_interior_span(coverage, target, interior_alpha, y, span, scratch);
        composite_text_edge_span(coverage, target, prepared, blend, colour_alpha, y, span.to, right);
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

// As composite_part_edge_span, for interior pixels (clip coverage a whole
// unit) under every blend but source-over, which takes the block kernels.
void composite_part_blend_span(const PartRasterSource& source, const TextCompositeTarget& target,
                               const std::int64_t y, const std::int64_t from, const std::int64_t to,
                               const std::uint64_t opacity_weight) noexcept {
    const bool skip_transparent = source.blend != PartCompositeBlend::copy;
    const std::uint64_t weight = opacity_weight * static_cast<std::uint64_t>(unit);
    const std::size_t source_row = static_cast<std::size_t>(y - source.top) * source.row_bytes;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    for (std::int64_t x = from; x < to; ++x) {
        const std::size_t offset = source_row + static_cast<std::size_t>(x - source.left) * 4U;
        const std::uint64_t source_alpha = source.pixels[offset + part_bytes[3]];
        if (skip_transparent && source_alpha == 0) continue;
        const std::array<std::uint64_t, 4> colour{source.pixels[offset + part_bytes[0]],
                                                source.pixels[offset + part_bytes[1]],
                                                source.pixels[offset + part_bytes[2]], source_alpha};
        blend_part_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], colour, weight, bgra_shifts,
                         target.opaque, source.blend);
    }
}

// The interior run `span` of row y, source-over at `opacity` through the
// block kernels; with `copy_rows` (an opaque raster at full opacity, which
// is its own result) the run is copied.
void composite_over_span(const PartRasterSource& source, const TextCompositeTarget& target, const std::int64_t y,
                         const InteriorSpan span, const std::uint8_t opacity, const bool copy_rows,
                         OverScratch& scratch) noexcept {
    const std::size_t source_offset = static_cast<std::size_t>(y - source.top) * source.row_bytes +
                                      static_cast<std::size_t>(span.from - source.left) * 4U;
    const std::size_t target_offset = static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(span.from);
    const std::uint8_t* const run = source.pixels.data() + source_offset;
    // The target's words as bytes, BGRA in memory (unsigned char may alias them).
    std::uint8_t* const destination = reinterpret_cast<std::uint8_t*>(target.pixels.data() + target_offset);
    const std::size_t pixels = static_cast<std::size_t>(span.to - span.from);
    if (copy_rows) {
        std::memcpy(destination, run, pixels * 4U);
        return;
    }
    over_row(run, destination, pixels, opacity, target.opaque, scratch);
}

// One target row's vertical sample (sample_position): its upper texel row,
// the lower row's weight `below` and the upper's `above`, and whether both
// rows lie inside the source.
struct ScaledRow final {
    std::int64_t texel_y{};
    std::uint64_t below{};
    std::uint64_t above{};
    bool rows_inside{};
};

ScaledRow scaled_row(const PartRasterSource& source, const std::uint32_t shown_height, const std::int64_t y) noexcept {
    const std::int64_t down = sample_position(y - source.top, source.height, shown_height);
    ScaledRow row{};
    row.texel_y = down >> 8;
    row.below = static_cast<std::uint64_t>(down & 255);
    row.above = 256U - row.below;
    row.rows_inside = row.texel_y >= 0 && row.texel_y + 1 < static_cast<std::int64_t>(source.height);
    return row;
}

// Scaled pixels [from, to) of target row y, each sampling its four texels
// bilinearly and weighted by the clip's coverage. Along the row the sample
// position is stepped exactly (SampleStepper), and the four texels of a
// sample wholly inside the source are fetched without the edge tests.
void composite_scaled_edge_span(const PartRasterSource& source, const TextCompositeTarget& target,
                                const PreparedClip& prepared, const ScaledRow& row, const std::int64_t y,
                                const std::int64_t from, const std::int64_t to, const std::uint32_t shown_width,
                                const std::uint64_t opacity_weight) noexcept {
    if (from >= to) return;
    const bool skip_transparent = source.blend != PartCompositeBlend::copy;
    const std::size_t target_row = static_cast<std::size_t>(y) * target.stride;
    SampleStepper column = sample_stepper(from - source.left, source.width, shown_width);
    for (std::int64_t x = from; x < to; ++x, advance_sample(column)) {
        const std::uint64_t clipped = static_cast<std::uint64_t>(prepared_clip_coverage(prepared, x, y));
        if (clipped == 0) continue;
        const std::int64_t across = column.position;
        const std::int64_t texel_x = across >> 8;
        const std::uint64_t after = static_cast<std::uint64_t>(across & 255);
        const std::uint64_t before = 256U - after;
        std::array<std::uint64_t, 4> colour{};
        if (row.rows_inside && texel_x >= 0 && texel_x + 1 < static_cast<std::int64_t>(source.width)) {
            const std::size_t upper_offset =
                static_cast<std::size_t>(row.texel_y) * source.row_bytes + static_cast<std::size_t>(texel_x) * 4U;
            const std::size_t lower_offset = upper_offset + source.row_bytes;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::size_t byte = part_bytes[channel];
                const std::uint64_t upper = static_cast<std::uint64_t>(source.pixels[upper_offset + byte]) * before +
                                            static_cast<std::uint64_t>(source.pixels[upper_offset + 4U + byte]) * after;
                const std::uint64_t lower = static_cast<std::uint64_t>(source.pixels[lower_offset + byte]) * before +
                                            static_cast<std::uint64_t>(source.pixels[lower_offset + 4U + byte]) * after;
                colour[channel] = (upper * row.above + lower * row.below + 32768U) >> 16U;
            }
        } else {
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::size_t byte = part_bytes[channel];
                const std::uint64_t upper = texel_channel(source, texel_x, row.texel_y, byte) * before +
                                            texel_channel(source, texel_x + 1, row.texel_y, byte) * after;
                const std::uint64_t lower = texel_channel(source, texel_x, row.texel_y + 1, byte) * before +
                                            texel_channel(source, texel_x + 1, row.texel_y + 1, byte) * after;
                colour[channel] = (upper * row.above + lower * row.below + 32768U) >> 16U;
            }
        }
        if (skip_transparent && colour[3] == 0U) continue;
        blend_part_pixel(target.pixels[target_row + static_cast<std::size_t>(x)], colour, opacity_weight * clipped,
                         bgra_shifts, target.opaque, source.blend);
    }
}

// Narrows a scaled source's interior to the columns and rows whose samples'
// four texels all lie inside the source (sample positions only grow along
// each axis, so those columns and rows are contiguous); the rest take the
// edge path, which reads transparent texels beyond the source.
void keep_whole_samples(ClipInterior& interior, const PartRasterSource& source, const std::uint32_t shown_width,
                        const std::uint32_t shown_height) noexcept {
    const std::int64_t source_width = static_cast<std::int64_t>(source.width);
    const std::int64_t source_height = static_cast<std::int64_t>(source.height);
    std::int64_t first = interior.inner_left;
    std::int64_t end = interior.inner_right;
    while (first < end && (sample_position(first - source.left, source.width, shown_width) >> 8) < 0) ++first;
    while (end > first && (sample_position(end - 1 - source.left, source.width, shown_width) >> 8) + 1 >= source_width) {
        --end;
    }
    std::int64_t first_row = interior.inner_top;
    std::int64_t end_row = interior.inner_bottom;
    while (first_row < end_row && (sample_position(first_row - source.top, source.height, shown_height) >> 8) < 0) {
        ++first_row;
    }
    while (end_row > first_row &&
           (sample_position(end_row - 1 - source.top, source.height, shown_height) >> 8) + 1 >= source_height) {
        --end_row;
    }
    interior.inner_left = first;
    interior.inner_right = end;
    interior.inner_top = first_row;
    interior.inner_bottom = end_row;
    interior.band_left = std::clamp(interior.band_left, interior.inner_left, interior.inner_right);
    interior.band_right = std::clamp(interior.band_right, interior.band_left, interior.inner_right);
    interior.band_top = std::clamp(interior.band_top, interior.inner_top, interior.inner_bottom);
    interior.band_bottom = std::clamp(interior.band_bottom, interior.band_top, interior.inner_bottom);
}

// Where a column samples the source (sample_position), computed once per
// call: the byte offset of its first texel within a source row, and the
// second texel's weight.
struct ColumnSample final {
    std::size_t offset{};
    std::uint32_t after{};
};

// The columns whose samples are made at once: 4 KiB of samples.
constexpr std::size_t sample_chunk_columns = 256U;

// `pixels` bilinear samples of one target row as premultiplied BGRA bytes
// in `out`, from the source rows at `upper` and `lower` (both inside the
// source, as are every sample's two texels). The two rows are read only.
void sample_row(const std::uint8_t* GUI_FORMS_RESTRICT upper, const std::uint8_t* GUI_FORMS_RESTRICT lower,
                const ColumnSample* GUI_FORMS_RESTRICT samples, const std::size_t pixels, const std::uint32_t below,
                std::uint8_t* GUI_FORMS_RESTRICT out) noexcept {
    const std::uint32_t above = 256U - below;
    for (std::size_t index = 0U; index < pixels; ++index) {
        const std::size_t at = samples[index].offset;
        const std::uint32_t after = samples[index].after;
        const std::uint32_t before = 256U - after;
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            const std::uint32_t upper_mix = upper[at + channel] * before + upper[at + 4U + channel] * after;
            const std::uint32_t lower_mix = lower[at + channel] * before + lower[at + 4U + channel] * after;
            const std::uint32_t mixed = (upper_mix * above + lower_mix * below + 32768U) >> 16U;
            out[index * 4U + channel] = static_cast<std::uint8_t>(mixed);
        }
    }
}

// A scaled source's interior, source-over at `opacity`: column chunk by
// chunk, the chunk's samples made once, then each interior row's run
// sampled block by block into scratch.sampled and blended.
void composite_scaled_interior(const PartRasterSource& source, const TextCompositeTarget& target,
                               const ClipInterior& interior, const std::uint32_t shown_width,
                               const std::uint32_t shown_height, const std::uint8_t opacity,
                               OverScratch& scratch) noexcept {
    std::array<ColumnSample, sample_chunk_columns> samples{};
    const std::int64_t chunk_columns = static_cast<std::int64_t>(sample_chunk_columns);
    for (std::int64_t chunk_left = interior.inner_left; chunk_left < interior.inner_right; chunk_left += chunk_columns) {
        const std::int64_t chunk_right = std::min(chunk_left + chunk_columns, interior.inner_right);
        SampleStepper column = sample_stepper(chunk_left - source.left, source.width, shown_width);
        for (std::int64_t x = chunk_left; x < chunk_right; ++x, advance_sample(column)) {
            ColumnSample& sample = samples[static_cast<std::size_t>(x - chunk_left)];
            sample.offset = static_cast<std::size_t>(column.position >> 8) * 4U;
            sample.after = static_cast<std::uint32_t>(column.position & 255);
        }
        for (std::int64_t y = interior.inner_top; y < interior.inner_bottom; ++y) {
            const InteriorSpan span = interior_span(interior, y);
            const std::int64_t from = std::max(span.from, chunk_left);
            const std::int64_t to = std::min(span.to, chunk_right);
            if (from >= to) continue;
            const std::int64_t down = sample_position(y - source.top, source.height, shown_height);
            const std::uint8_t* const upper =
                source.pixels.data() + static_cast<std::size_t>(down >> 8) * source.row_bytes;
            const std::uint8_t* const lower = upper + source.row_bytes;
            const std::uint32_t below = static_cast<std::uint32_t>(down & 255);
            const std::size_t target_offset = static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(from);
            // The target's words as bytes, BGRA in memory (unsigned char may alias them).
            std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(target.pixels.data() + target_offset);
            const ColumnSample* sample = samples.data() + static_cast<std::size_t>(from - chunk_left);
            std::size_t pixels = static_cast<std::size_t>(to - from);
            while (pixels != 0U) {
                const std::size_t count = std::min(pixels, composite_block_pixels);
                sample_row(upper, lower, sample, count, below, scratch.sampled.data());
                over_row(scratch.sampled.data(), destination, count, opacity, target.opaque, scratch);
                sample += count;
                destination += count * 4U;
                pixels -= count;
            }
        }
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
    const std::uint64_t opacity_weight = opacity;
    ClipInterior interior = clip_interior(prepared, limit, left, top, right, bottom);
    OverScratch scratch{};
    if (!scaled) {
        // Interior pixels skip the coverage arithmetic: source-over through
        // the block kernels, the other blends pixel by pixel; the rows and
        // columns the clip cuts take the per-pixel path.
        const bool over = source.blend == PartCompositeBlend::source_over;
        // A raster recorded wholly opaque is, at full opacity, its own
        // source-over result.
        const bool copy_rows = over && source.opaque && opacity == 255U;
        for (std::int64_t y = top; y < bottom; ++y) {
            const InteriorSpan span = interior_span(interior, y);
            if (span.from >= span.to) {
                composite_part_edge_span(source, target, prepared, y, left, right, opacity_weight);
                continue;
            }
            composite_part_edge_span(source, target, prepared, y, left, span.from, opacity_weight);
            if (over) {
                composite_over_span(source, target, y, span, opacity, copy_rows, scratch);
            } else {
                composite_part_blend_span(source, target, y, span.from, span.to, opacity_weight);
            }
            composite_part_edge_span(source, target, prepared, y, span.to, right, opacity_weight);
        }
        return TextCompositeStatus::success;
    }
    // Scaled: each device pixel samples its four texels bilinearly. Under
    // source-over the interior whose samples lie wholly inside the source
    // takes the block kernels; every other pixel, and every pixel of the
    // other blends, the per-pixel path.
    if (source.blend == PartCompositeBlend::source_over) {
        keep_whole_samples(interior, source, shown_width, shown_height);
    } else {
        interior = ClipInterior{};
    }
    for (std::int64_t y = top; y < bottom; ++y) {
        const ScaledRow row = scaled_row(source, shown_height, y);
        const InteriorSpan span = interior_span(interior, y);
        if (span.from >= span.to) {
            composite_scaled_edge_span(source, target, prepared, row, y, left, right, shown_width, opacity_weight);
            continue;
        }
        composite_scaled_edge_span(source, target, prepared, row, y, left, span.from, shown_width, opacity_weight);
        composite_scaled_edge_span(source, target, prepared, row, y, span.to, right, shown_width, opacity_weight);
    }
    composite_scaled_interior(source, target, interior, shown_width, shown_height, opacity, scratch);
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
