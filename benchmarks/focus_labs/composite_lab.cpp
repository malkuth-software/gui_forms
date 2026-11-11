// Lab 1: the shared pixel compositor (src/core/text/composite/text_composite.cpp
// at 66a7f2e5b) against restructured kernels that must give the same bytes.
//
// The baseline is the project's own translation unit, compiled with the
// project's flags (-O3 -DNDEBUG -std=c++20, no -march). The candidates keep
// its arithmetic and change only where the decisions are made:
//   - edges go through the project's function unchanged (called with the
//     clip cut at whole pixels, which leaves every pixel's coverage as it
//     was);
//   - the interior is worked in fixed blocks: one pass prepares each block's
//     operands in scratch that stays in the first-level cache and classifies
//     the block, and one flat pass over bytes does the arithmetic in 16-bit
//     lanes with no decision inside it.
//
// Build: see build.sh. Not first-party code; it follows the house style only
// loosely.

#include "text_composite.hpp" // src/core/text/composite

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using gui_forms::Color;
using gui_forms::Rect;
using gui_forms::detail::PartCompositeBlend;
using gui_forms::detail::PartRasterSource;
using gui_forms::detail::TextCompositeClip;
using gui_forms::detail::TextCompositeMode;
using gui_forms::detail::TextCompositeStatus;
using gui_forms::detail::TextCompositeTarget;
using gui_forms::detail::TextCoverage;

namespace lab {

// round(v / 255) for 0 <= v <= 65025, without a wide multiply: fits 16 bits
// at every step (checked exhaustively in check_identities).
static inline unsigned div255(const unsigned v) noexcept {
    const unsigned t = v + 128U;
    return (t + (t >> 8U)) >> 8U;
}

static std::int64_t floor_pixel(const std::int64_t fixed) noexcept {
    std::int64_t quotient = fixed / 256;
    if (fixed % 256 != 0 && fixed < 0) --quotient;
    return quotient;
}

static std::int64_t ceil_pixel(const std::int64_t fixed) noexcept {
    std::int64_t quotient = fixed / 256;
    if (fixed % 256 != 0 && fixed > 0) ++quotient;
    return quotient;
}

struct Interior final {
    // The blit's device extent, and the part of it wholly inside the clip's
    // rectangles.
    std::int64_t left{}, top{}, right{}, bottom{};
    std::int64_t inner_left{}, inner_top{}, inner_right{}, inner_bottom{};
    // A rounded clip: rows [band_top, band_bottom) are clear of the corners
    // across their whole width; the others only in columns [band_left,
    // band_right). Without rounding the bands are the whole interior.
    std::int64_t band_top{}, band_bottom{}, band_left{}, band_right{};
};

static std::int64_t floor_div(const std::int64_t value, const std::int64_t by) noexcept {
    std::int64_t quotient = value / by;
    if (value % by != 0 && value < 0) --quotient;
    return quotient;
}

// The geometry composite_part_raster and composite_text_coverage derive.
// A pixel is interior when it lies wholly inside the clip rectangle and
// the rounded rectangle and its centre is in no corner square: its clip
// coverage is then exactly one unit (text_composite.cpp:95-113, :136-142).
static Interior interior_of(const TextCompositeClip& clip, const TextCompositeTarget& target,
                            const std::int64_t source_left, const std::int64_t source_top,
                            const std::uint32_t source_width, const std::uint32_t source_height) noexcept {
    std::int64_t fixed_left = std::llround(clip.rect.x * 256.0);
    std::int64_t fixed_top = std::llround(clip.rect.y * 256.0);
    std::int64_t fixed_right = std::llround((clip.rect.x + clip.rect.width) * 256.0);
    std::int64_t fixed_bottom = std::llround((clip.rect.y + clip.rect.height) * 256.0);
    std::int64_t round_left = 0, round_top = 0, round_right = 0, round_bottom = 0, radius = 0;
    if (clip.rounded) {
        round_left = std::llround(clip.rounded_rect.x * 256.0);
        round_top = std::llround(clip.rounded_rect.y * 256.0);
        round_right = std::llround((clip.rounded_rect.x + clip.rounded_rect.width) * 256.0);
        round_bottom = std::llround((clip.rounded_rect.y + clip.rounded_rect.height) * 256.0);
        const std::int64_t requested = std::llround(clip.radius * 256.0);
        radius = std::clamp<std::int64_t>(
            requested, 0, std::min({(round_right - round_left) / 2, (round_bottom - round_top) / 2, std::int64_t{4096 * 256}}));
        fixed_left = std::max(fixed_left, round_left);
        fixed_top = std::max(fixed_top, round_top);
        fixed_right = std::min(fixed_right, round_right);
        fixed_bottom = std::min(fixed_bottom, round_bottom);
    }
    Interior in{};
    in.left = std::max<std::int64_t>({0, floor_pixel(fixed_left), source_left});
    in.top = std::max<std::int64_t>({0, floor_pixel(fixed_top), source_top});
    in.right = std::min<std::int64_t>({static_cast<std::int64_t>(target.width), ceil_pixel(fixed_right),
                                       source_left + static_cast<std::int64_t>(source_width)});
    in.bottom = std::min<std::int64_t>({static_cast<std::int64_t>(target.height), ceil_pixel(fixed_bottom),
                                        source_top + static_cast<std::int64_t>(source_height)});
    in.inner_left = std::min(in.right, std::max(in.left, ceil_pixel(fixed_left)));
    in.inner_right = std::max(in.inner_left, std::min(in.right, floor_pixel(fixed_right)));
    in.inner_top = std::min(in.bottom, std::max(in.top, ceil_pixel(fixed_top)));
    in.inner_bottom = std::max(in.inner_top, std::min(in.bottom, floor_pixel(fixed_bottom)));
    in.band_top = in.inner_top;
    in.band_bottom = in.inner_bottom;
    in.band_left = in.inner_left;
    in.band_right = in.inner_right;
    if (clip.rounded && radius != 0) {
        // Centre x * 256 + 128 within [left + radius, right - radius].
        const std::int64_t first_column = -floor_div(-(round_left + radius - 128), 256);
        const std::int64_t end_column = floor_div(round_right - radius - 128, 256) + 1;
        const std::int64_t first_row = -floor_div(-(round_top + radius - 128), 256);
        const std::int64_t end_row = floor_div(round_bottom - radius - 128, 256) + 1;
        in.band_left = std::clamp(first_column, in.inner_left, in.inner_right);
        in.band_right = std::clamp(end_column, in.band_left, in.inner_right);
        in.band_top = std::clamp(first_row, in.inner_top, in.inner_bottom);
        in.band_bottom = std::clamp(end_row, in.band_top, in.inner_bottom);
    }
    return in;
}

static Rect intersect(const Rect a, const double x0, const double y0, const double x1, const double y1) noexcept {
    const double left = std::max(a.x, x0);
    const double top = std::max(a.y, y0);
    const double right = std::min(a.x + a.width, x1);
    const double bottom = std::min(a.y + a.height, y1);
    Rect result{};
    result.x = left;
    result.y = top;
    result.width = std::max(0.0, right - left);
    result.height = std::max(0.0, bottom - top);
    return result;
}

// Calls `draw(strip_clip)` for each part of the blit that is not interior:
// the rows above and below, the columns beside, and under a rounded clip
// the four corner pieces. Each is the clip cut at whole pixels, which leaves
// every remaining pixel's coverage as it was.
template <typename Draw>
static void edge_strips(const Interior& in, const TextCompositeClip& clip, const TextCompositeTarget& target,
                        Draw draw) {
    const double width = static_cast<double>(target.width);
    const double height = static_cast<double>(target.height);
    const double it = static_cast<double>(in.inner_top);
    const double ib = static_cast<double>(in.inner_bottom);
    const double il = static_cast<double>(in.inner_left);
    const double ir = static_cast<double>(in.inner_right);
    const double bt = static_cast<double>(in.band_top);
    const double bb = static_cast<double>(in.band_bottom);
    const double bl = static_cast<double>(in.band_left);
    const double br = static_cast<double>(in.band_right);
    TextCompositeClip strip = clip;
    const bool rows = in.inner_bottom > in.inner_top;
    if (in.inner_top > in.top) { strip.rect = intersect(clip.rect, 0.0, 0.0, width, it); draw(strip); }
    if (in.inner_bottom < in.bottom) { strip.rect = intersect(clip.rect, 0.0, ib, width, height); draw(strip); }
    if (rows && in.inner_left > in.left) { strip.rect = intersect(clip.rect, 0.0, it, il, ib); draw(strip); }
    if (rows && in.inner_right < in.right) { strip.rect = intersect(clip.rect, ir, it, width, ib); draw(strip); }
    if (in.band_top > in.inner_top) {
        if (in.band_left > in.inner_left) { strip.rect = intersect(clip.rect, il, it, bl, bt); draw(strip); }
        if (in.band_right < in.inner_right) { strip.rect = intersect(clip.rect, br, it, ir, bt); draw(strip); }
    }
    if (in.band_bottom < in.inner_bottom) {
        if (in.band_left > in.inner_left) { strip.rect = intersect(clip.rect, il, bb, bl, ib); draw(strip); }
        if (in.band_right < in.inner_right) { strip.rect = intersect(clip.rect, br, bb, ir, ib); draw(strip); }
    }
}

constexpr std::size_t block_pixels = 64U;

// t = min(255, s + round(t * rem / 255)), byte by byte.
static void blend_bytes(const std::uint8_t* __restrict source, const std::uint8_t* __restrict remaining,
                        std::uint8_t* __restrict target, const std::size_t bytes) noexcept {
    for (std::size_t index = 0; index < bytes; ++index) {
        const std::uint16_t product = static_cast<std::uint16_t>(static_cast<std::uint16_t>(target[index]) *
                                                                 static_cast<std::uint16_t>(remaining[index]));
        const std::uint16_t biased = static_cast<std::uint16_t>(product + 128U);
        const std::uint16_t share = static_cast<std::uint16_t>(static_cast<std::uint16_t>(biased + (biased >> 8U)) >> 8U);
        const std::uint16_t sum = static_cast<std::uint16_t>(source[index] + share);
        target[index] = static_cast<std::uint8_t>(sum > 255U ? 255U : sum);
    }
}

// out = round(in * opacity / 255), byte by byte.
static void weight_bytes(const std::uint8_t* __restrict in, std::uint8_t* __restrict out, const std::size_t bytes,
                         const std::uint16_t opacity) noexcept {
    for (std::size_t index = 0; index < bytes; ++index) {
        const std::uint16_t product = static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[index]) * opacity);
        const std::uint16_t biased = static_cast<std::uint16_t>(product + 128U);
        out[index] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(biased + (biased >> 8U)) >> 8U);
    }
}

struct BlockAlpha final {
    std::uint32_t every{}; // AND of the block's alphas
    std::uint32_t any{};   // OR of the block's alphas
};

// From premultiplied BGRA words: the source as the blend adds it (cleared
// where its alpha is 0, alpha forced to 255 on an opaque target) and each
// byte's remaining share 255 - alpha.
static BlockAlpha prepare_block(const std::uint8_t* __restrict source, std::uint8_t* __restrict added,
                                std::uint8_t* __restrict remaining, const std::size_t pixels,
                                const bool opaque) noexcept {
    std::uint32_t every = 255U;
    std::uint32_t any = 0U;
    const std::uint32_t forced = opaque ? 0xff000000U : 0U;
    for (std::size_t index = 0; index < pixels; ++index) {
        std::uint32_t pixel;
        std::memcpy(&pixel, source + index * 4U, 4U);
        const std::uint32_t alpha = pixel >> 24U;
        every &= alpha;
        any |= alpha;
        const std::uint32_t mask = alpha == 0U ? 0U : 0xffffffffU;
        const std::uint32_t add = (pixel | forced) & mask;
        const std::uint32_t rest = 255U - alpha;
        const std::uint32_t rest4 = rest | (rest << 8U) | (rest << 16U) | (rest << 24U);
        std::memcpy(added + index * 4U, &add, 4U);
        std::memcpy(remaining + index * 4U, &rest4, 4U);
    }
    const BlockAlpha result{every, any};
    return result;
}

// One interior row of a part raster, premultiplied source-over at `opacity`.
static void part_row(const std::uint8_t* source, std::uint8_t* target, std::size_t pixels, const unsigned opacity,
                     const bool opaque) noexcept {
    alignas(64) std::uint8_t weighted[block_pixels * 4U];
    alignas(64) std::uint8_t added[block_pixels * 4U];
    alignas(64) std::uint8_t remaining[block_pixels * 4U];
    while (pixels != 0U) {
        const std::size_t count = std::min(pixels, block_pixels);
        const std::uint8_t* from = source;
        if (opacity != 255U) {
            weight_bytes(source, weighted, count * 4U, static_cast<std::uint16_t>(opacity));
            from = weighted;
        }
        const BlockAlpha alpha = prepare_block(from, added, remaining, count, opaque);
        if (alpha.any != 0U) {
            if (alpha.every == 255U) {
                std::memcpy(target, from, count * 4U);
            } else {
                blend_bytes(added, remaining, target, count * 4U);
            }
        }
        source += count * 4U;
        target += count * 4U;
        pixels -= count;
    }
}

// Where each shown column samples the source: its first texel and the
// second texel's weight (text_composite.cpp:372-377), computed once per call.
struct Sample final {
    std::int32_t texel{};
    std::uint32_t after{};
};

static std::int64_t sample_position(const std::int64_t shown, const std::uint32_t source_length,
                                    const std::uint32_t shown_length) noexcept {
    const std::int64_t numerator = (2 * shown + 1) * static_cast<std::int64_t>(source_length) * 256;
    return numerator / (2 * static_cast<std::int64_t>(shown_length)) - 128;
}

// One row of bilinear samples, as premultiplied BGRA bytes: the layout the
// blend's row kernel takes. Every texel read is inside the source.
static void sample_row(const std::uint8_t* __restrict upper_row, const std::uint8_t* __restrict lower_row,
                       const Sample* __restrict samples, const std::size_t pixels, const std::uint32_t below,
                       std::uint8_t* __restrict out) noexcept {
    const std::uint32_t above = 256U - below;
    for (std::size_t index = 0; index < pixels; ++index) {
        const std::size_t at = static_cast<std::size_t>(samples[index].texel) * 4U;
        const std::uint32_t after = samples[index].after;
        const std::uint32_t before = 256U - after;
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const std::uint32_t upper = upper_row[at + channel] * before + upper_row[at + 4U + channel] * after;
            const std::uint32_t lower = lower_row[at + channel] * before + lower_row[at + 4U + channel] * after;
            out[index * 4U + channel] = static_cast<std::uint8_t>((upper * above + lower * below + 32768U) >> 16U);
        }
    }
}

// `whole_opaque`: the raster's maker has declared every pixel alpha 255 (a
// fact the bake can record once). Rows are then copied.
TextCompositeStatus fast_part(const PartRasterSource source, const TextCompositeTarget target,
                              const TextCompositeClip clip, const std::uint8_t opacity,
                              const bool whole_opaque) noexcept {
    const bool scaled = (source.shown_width != 0U && source.shown_width != source.width) ||
                        (source.shown_height != 0U && source.shown_height != source.height);
    if (source.blend != PartCompositeBlend::source_over || opacity == 0U || source.width == 0U ||
        source.height == 0U) {
        return gui_forms::detail::composite_part_raster(source, target, clip, opacity);
    }
    // The project's validation, unchanged, by way of an empty clip: a call
    // that draws nothing but refuses what the real one refuses.
    TextCompositeClip nothing = clip;
    nothing.rect.width = 0.0;
    nothing.rect.height = 0.0;
    const TextCompositeStatus status = gui_forms::detail::composite_part_raster(source, target, nothing, opacity);
    if (status != TextCompositeStatus::success) return status;
    const std::uint32_t shown_width = source.shown_width != 0U ? source.shown_width : source.width;
    const std::uint32_t shown_height = source.shown_height != 0U ? source.shown_height : source.height;
    Interior in = interior_of(clip, target, source.left, source.top, shown_width, shown_height);
    if (in.left >= in.right || in.top >= in.bottom) return TextCompositeStatus::success;
    std::vector<Sample> samples;
    if (scaled) {
        // Keep only the columns and rows whose four texels are all inside
        // the source; the rest go with the edges.
        std::int64_t first = in.inner_left;
        std::int64_t end = in.inner_right;
        while (first < end && (sample_position(first - source.left, source.width, shown_width) >> 8) < 0) ++first;
        while (end > first && (sample_position(end - 1 - source.left, source.width, shown_width) >> 8) + 1 >=
                                  static_cast<std::int64_t>(source.width)) --end;
        std::int64_t first_row = in.inner_top;
        std::int64_t end_row = in.inner_bottom;
        while (first_row < end_row && (sample_position(first_row - source.top, source.height, shown_height) >> 8) < 0) ++first_row;
        while (end_row > first_row && (sample_position(end_row - 1 - source.top, source.height, shown_height) >> 8) + 1 >=
                                          static_cast<std::int64_t>(source.height)) --end_row;
        in.inner_left = first;
        in.inner_right = std::max(first, end);
        in.inner_top = first_row;
        in.inner_bottom = std::max(first_row, end_row);
        in.band_left = std::clamp(in.band_left, in.inner_left, in.inner_right);
        in.band_right = std::clamp(in.band_right, in.band_left, in.inner_right);
        in.band_top = std::clamp(in.band_top, in.inner_top, in.inner_bottom);
        in.band_bottom = std::clamp(in.band_bottom, in.band_top, in.inner_bottom);
        samples.resize(static_cast<std::size_t>(in.inner_right - in.inner_left));
        for (std::int64_t x = in.inner_left; x < in.inner_right; ++x) {
            const std::int64_t across = sample_position(x - source.left, source.width, shown_width);
            samples[static_cast<std::size_t>(x - in.inner_left)] =
                Sample{static_cast<std::int32_t>(across >> 8), static_cast<std::uint32_t>(across & 255)};
        }
    }
    edge_strips(in, clip, target, [&](const TextCompositeClip& strip) {
        static_cast<void>(gui_forms::detail::composite_part_raster(source, target, strip, opacity));
    });
    if (in.inner_left >= in.inner_right) return TextCompositeStatus::success;
    const bool copy_rows = whole_opaque && opacity == 255U && !scaled;
    alignas(64) std::uint8_t sampled[block_pixels * 4U];
    for (std::int64_t y = in.inner_top; y < in.inner_bottom; ++y) {
        const bool whole_row = y >= in.band_top && y < in.band_bottom;
        const std::int64_t from_x = whole_row ? in.inner_left : in.band_left;
        const std::int64_t to_x = whole_row ? in.inner_right : in.band_right;
        if (from_x >= to_x) continue;
        const std::size_t pixels = static_cast<std::size_t>(to_x - from_x);
        std::uint8_t* to = reinterpret_cast<std::uint8_t*>(
            target.pixels.data() + static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(from_x));
        if (scaled) {
            const std::int64_t down = sample_position(y - source.top, source.height, shown_height);
            const std::uint8_t* const upper = source.pixels.data() + static_cast<std::size_t>(down >> 8) * source.row_bytes;
            const std::uint8_t* const lower = upper + source.row_bytes;
            const Sample* sample = samples.data() + static_cast<std::size_t>(from_x - in.inner_left);
            std::size_t remaining = pixels;
            while (remaining != 0U) {
                const std::size_t count = std::min(remaining, block_pixels);
                sample_row(upper, lower, sample, count, static_cast<std::uint32_t>(down & 255), sampled);
                part_row(sampled, to, count, opacity, target.opaque);
                sample += count;
                to += count * 4U;
                remaining -= count;
            }
            continue;
        }
        const std::uint8_t* const from = source.pixels.data() +
            static_cast<std::size_t>(y - source.top) * source.row_bytes +
            static_cast<std::size_t>(from_x - source.left) * 4U;
        if (copy_rows) {
            std::memcpy(to, from, pixels * 4U);
        } else {
            part_row(from, to, pixels, opacity, target.opaque);
        }
    }
    return TextCompositeStatus::success;
}

// t = round((colour * alpha + t * (255 - alpha)) / 255), byte by byte; the
// colour repeats every four bytes.
static void text_bytes(const std::uint8_t* __restrict colour, const std::uint8_t* __restrict alpha,
                       std::uint8_t* __restrict target, const std::size_t bytes) noexcept {
    for (std::size_t index = 0; index < bytes; ++index) {
        const std::uint16_t a = alpha[index];
        const std::uint16_t value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(colour[index]) * a +
            static_cast<std::uint16_t>(target[index]) * static_cast<std::uint16_t>(255U - a));
        const std::uint16_t biased = static_cast<std::uint16_t>(value + 128U);
        target[index] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(biased + (biased >> 8U)) >> 8U);
    }
}

// One interior row of glyph coverage, source-over in the block's colour.
static void text_row(const std::uint8_t* coverage, std::uint8_t* target, std::size_t pixels,
                     const std::uint8_t* pattern, const unsigned colour_alpha, const bool opaque) noexcept {
    alignas(64) std::uint8_t weighted[block_pixels];
    alignas(64) std::uint8_t alpha4[block_pixels * 4U];
    while (pixels != 0U) {
        const std::size_t count = std::min(pixels, block_pixels);
        const std::uint8_t* alpha = coverage;
        if (colour_alpha != 255U) {
            weight_bytes(coverage, weighted, count, static_cast<std::uint16_t>(colour_alpha));
            alpha = weighted;
        }
        std::uint32_t any = 0U;
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t a = alpha[index];
            any |= a;
            // On an opaque target the alpha byte becomes 255 wherever the
            // pixel is touched at all.
            const std::uint32_t top = opaque ? (a == 0U ? 0U : 255U) : a;
            const std::uint32_t a4 = a | (a << 8U) | (a << 16U) | (top << 24U);
            std::memcpy(alpha4 + index * 4U, &a4, 4U);
        }
        if (any != 0U) text_bytes(pattern, alpha4, target, count * 4U);
        coverage += count;
        target += count * 4U;
        pixels -= count;
    }
}

TextCompositeStatus fast_text(const TextCoverage coverage, const TextCompositeTarget target,
                              const TextCompositeClip clip, const Color color) noexcept {
    if (coverage.width == 0U || coverage.height == 0U || color.alpha == 0U) {
        return gui_forms::detail::composite_text_coverage(coverage, target, clip, color);
    }
    TextCompositeClip nothing = clip;
    nothing.rect.width = 0.0;
    nothing.rect.height = 0.0;
    const TextCompositeStatus status = gui_forms::detail::composite_text_coverage(coverage, target, nothing, color);
    if (status != TextCompositeStatus::success) return status;
    const Interior in = interior_of(clip, target, coverage.left, coverage.top, coverage.width, coverage.height);
    if (in.left >= in.right || in.top >= in.bottom) return TextCompositeStatus::success;
    edge_strips(in, clip, target, [&](const TextCompositeClip& strip) {
        static_cast<void>(gui_forms::detail::composite_text_coverage(coverage, target, strip, color));
    });
    if (in.inner_left >= in.inner_right) return TextCompositeStatus::success;
    // The colour as bytes, repeated across one block: blue, green, red and
    // 255 for the alpha lane (the same form gives the blended alpha).
    alignas(64) std::uint8_t pattern[block_pixels * 4U];
    for (std::size_t index = 0; index < block_pixels; ++index) {
        pattern[index * 4U + 0U] = color.blue;
        pattern[index * 4U + 1U] = color.green;
        pattern[index * 4U + 2U] = color.red;
        pattern[index * 4U + 3U] = 255U;
    }
    for (std::int64_t y = in.inner_top; y < in.inner_bottom; ++y) {
        const bool whole_row = y >= in.band_top && y < in.band_bottom;
        const std::int64_t from_x = whole_row ? in.inner_left : in.band_left;
        const std::int64_t to_x = whole_row ? in.inner_right : in.band_right;
        if (from_x >= to_x) continue;
        const std::uint8_t* const from = coverage.pixels.data() +
            static_cast<std::size_t>(y - coverage.top) * coverage.width + static_cast<std::size_t>(from_x - coverage.left);
        std::uint8_t* const to = reinterpret_cast<std::uint8_t*>(
            target.pixels.data() + static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(from_x));
        text_row(from, to, static_cast<std::size_t>(to_x - from_x), pattern, color.alpha, target.opaque);
    }
    return TextCompositeStatus::success;
}

// ---------------------------------------------------------------------------
// Checks and timing.
// ---------------------------------------------------------------------------

static bool check_identities() {
    for (unsigned v = 0; v <= 65025U; ++v) {
        if (div255(v) != (v + 127U) / 255U) {
            std::printf("div255 differs at %u\n", v);
            return false;
        }
    }
    // (c * o * 256 + 32640) / 65280 == round(c * o / 255) for bytes c, o.
    for (std::uint64_t c = 0; c <= 255U; ++c) {
        for (std::uint64_t o = 0; o <= 255U; ++o) {
            const unsigned wide = static_cast<unsigned>((c * (o * 256U) + 32640U) / 65280U);
            if (wide != div255(static_cast<unsigned>(c * o))) {
                std::printf("weight identity differs at %llu %llu\n", static_cast<unsigned long long>(c),
                            static_cast<unsigned long long>(o));
                return false;
            }
        }
    }
    return true;
}

struct Scene final {
    std::uint32_t target_width{}, target_height{};
    std::vector<std::uint32_t> target;
    std::uint32_t source_width{}, source_height{};
    std::vector<std::uint8_t> source;   // premultiplied BGRA
    std::vector<std::uint8_t> coverage; // one byte per pixel
};

enum class SourceKind { translucent, opaque, part_like, text_like };

static Scene make_scene(std::mt19937& rng, std::uint32_t tw, std::uint32_t th, std::uint32_t sw, std::uint32_t sh,
                        SourceKind kind) {
    Scene scene{};
    scene.target_width = tw;
    scene.target_height = th;
    scene.target.resize(static_cast<std::size_t>(tw) * th);
    for (std::uint32_t& pixel : scene.target) pixel = rng() | 0xff000000U;
    scene.source_width = sw;
    scene.source_height = sh;
    scene.source.resize(static_cast<std::size_t>(sw) * sh * 4U);
    scene.coverage.resize(static_cast<std::size_t>(sw) * sh);
    for (std::uint32_t y = 0; y < sh; ++y) {
        for (std::uint32_t x = 0; x < sw; ++x) {
            unsigned alpha = 255U;
            if (kind == SourceKind::translucent) alpha = rng() & 255U;
            if (kind == SourceKind::part_like) {
                // Opaque body, a two-pixel soft edge, clear corners.
                const std::uint32_t edge = std::min(std::min(x, sw - 1U - x), std::min(y, sh - 1U - y));
                const bool corner = (x < 6U || x >= sw - 6U) && (y < 6U || y >= sh - 6U);
                alpha = corner ? 0U : (edge == 0U ? 96U : (edge == 1U ? 200U : 255U));
            }
            const std::size_t offset = (static_cast<std::size_t>(y) * sw + x) * 4U;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const unsigned value = rng() & 255U;
                scene.source[offset + channel] = static_cast<std::uint8_t>((value * alpha + 127U) / 255U);
            }
            scene.source[offset + 3U] = static_cast<std::uint8_t>(alpha);
            // Coverage. text_like: runs as glyph stems make them (a gap, a
            // soft edge, a solid stem, a soft edge) with blank rows between
            // lines. Otherwise noise, the worst case for a per-pixel branch.
            unsigned cover = 0U;
            if (kind != SourceKind::text_like) {
                const unsigned roll = rng() % 100U;
                cover = roll < 70U ? 0U : (roll < 85U ? 255U : (rng() & 255U));
            } else {
                const std::uint32_t line = y % 20U;
                const std::uint32_t phase = (x + (y / 20U) * 7U) % 11U;
                if (line >= 4U && line < 16U) {
                    cover = phase == 3U || phase == 6U ? (rng() & 255U) : (phase == 4U || phase == 5U ? 255U : 0U);
                }
            }
            scene.coverage[static_cast<std::size_t>(y) * sw + x] = static_cast<std::uint8_t>(cover);
        }
    }
    return scene;
}

static TextCompositeTarget target_of(std::vector<std::uint32_t>& pixels, std::uint32_t w, std::uint32_t h, bool opaque) {
    TextCompositeTarget target{};
    target.pixels = std::span<std::uint32_t>(pixels.data(), pixels.size());
    target.width = w;
    target.height = h;
    target.stride = w;
    target.opaque = opaque;
    return target;
}

static bool check_equal(std::mt19937& rng) {
    std::size_t trials = 0;
    for (int round = 0; round < 20000; ++round) {
        const std::uint32_t tw = 40U + rng() % 260U;
        const std::uint32_t th = 30U + rng() % 40U;
        const std::uint32_t sw = 1U + rng() % 270U;
        const std::uint32_t sh = 1U + rng() % 50U;
        const SourceKind kind = static_cast<SourceKind>(rng() % 4U);
        Scene scene = make_scene(rng, tw, th, sw, sh, kind);
        if (rng() % 2U == 0U) {
            for (std::uint32_t& pixel : scene.target) pixel = rng();
        }
        const bool opaque_target = rng() % 2U == 0U;
        TextCompositeClip clip{};
        // Edges on 1/256 pixel, sometimes whole pixels, sometimes outside.
        const bool whole = rng() % 2U == 0U;
        const double step = whole ? 1.0 : 1.0 / 256.0;
        const unsigned scale = whole ? 1U : 256U;
        clip.rect.x = static_cast<double>(static_cast<int>(rng() % (tw * scale)) - 8) * step;
        clip.rect.y = static_cast<double>(static_cast<int>(rng() % (th * scale)) - 8) * step;
        clip.rect.width = static_cast<double>(rng() % (tw * scale)) * step;
        clip.rect.height = static_cast<double>(rng() % (th * scale)) * step;
        if (rng() % 2U == 0U) {
            clip.rounded = true;
            clip.rounded_rect.x = clip.rect.x + static_cast<double>(static_cast<int>(rng() % 9U) - 4) * step;
            clip.rounded_rect.y = clip.rect.y + static_cast<double>(static_cast<int>(rng() % 9U) - 4) * step;
            clip.rounded_rect.width = clip.rect.width + static_cast<double>(rng() % 9U) * step;
            clip.rounded_rect.height = clip.rect.height + static_cast<double>(rng() % 9U) * step;
            clip.radius = static_cast<double>(rng() % 3000U) / 100.0;
        }
        const std::int64_t left = static_cast<std::int64_t>(rng() % (tw + 20U)) - 20;
        const std::int64_t top = static_cast<std::int64_t>(rng() % (th + 20U)) - 20;
        const std::uint8_t opacity = (rng() % 3U == 0U) ? 255U : static_cast<std::uint8_t>(rng() & 255U);

        std::vector<std::uint32_t> expected = scene.target;
        std::vector<std::uint32_t> actual = scene.target;
        PartRasterSource source{};
        source.pixels = std::span<const std::uint8_t>(scene.source.data(), scene.source.size());
        source.width = sw;
        source.height = sh;
        source.row_bytes = static_cast<std::size_t>(sw) * 4U;
        source.left = left;
        source.top = top;
        if (rng() % 3U == 0U) {
            source.shown_width = 1U + rng() % 300U;
            source.shown_height = 1U + rng() % 80U;
        }
        const TextCompositeStatus a = gui_forms::detail::composite_part_raster(
            source, target_of(expected, tw, th, opaque_target), clip, opacity);
        const TextCompositeStatus b =
            fast_part(source, target_of(actual, tw, th, opaque_target), clip, opacity, kind == SourceKind::opaque);
        if (a != b || expected != actual) {
            std::printf("part mismatch in round %d (opacity %u, kind %d)\n", round, opacity, static_cast<int>(kind));
            return false;
        }

        expected = scene.target;
        actual = scene.target;
        TextCoverage coverage{};
        coverage.pixels = std::span<const std::uint8_t>(scene.coverage.data(), scene.coverage.size());
        coverage.width = sw;
        coverage.height = sh;
        coverage.left = left;
        coverage.top = top;
        Color colour{};
        colour.red = static_cast<std::uint8_t>(rng());
        colour.green = static_cast<std::uint8_t>(rng());
        colour.blue = static_cast<std::uint8_t>(rng());
        colour.alpha = (rng() % 2U == 0U) ? 255U : static_cast<std::uint8_t>(rng());
        const TextCompositeStatus c = gui_forms::detail::composite_text_coverage(
            coverage, target_of(expected, tw, th, opaque_target), clip, colour);
        const TextCompositeStatus d = fast_text(coverage, target_of(actual, tw, th, opaque_target), clip, colour);
        if (c != d || expected != actual) {
            std::printf("text mismatch in round %d (alpha %u)\n", round, colour.alpha);
            return false;
        }
        trials += 2U;
    }
    std::printf("equality: %zu randomised blits identical, byte for byte\n", trials);
    return true;
}

using Clock = std::chrono::steady_clock;

template <typename Work>
static double best_seconds(const int repeats, Work work) {
    double best = 1.0e30;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const Clock::time_point start = Clock::now();
        work();
        const Clock::time_point end = Clock::now();
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }
    return best;
}

static void report(const char* name, double base, double fast, double pixels) {
    std::printf("%-44s  main %7.3f ns/px %6.0f MB/s   lab %7.3f ns/px %6.0f MB/s   x%.1f\n", name,
                base * 1.0e9 / pixels, pixels * 4.0 / base / 1.0e6, fast * 1.0e9 / pixels,
                pixels * 4.0 / fast / 1.0e6, base / fast);
}

static void time_part(std::mt19937& rng, const char* name, SourceKind kind, std::uint8_t opacity, bool whole_opaque,
                      std::uint32_t sw, std::uint32_t sh, int calls, double radius = 0.0, std::uint32_t shown_w = 0U,
                      std::uint32_t shown_h = 0U) {
    Scene scene = make_scene(rng, 1920U, 1080U, sw, sh, kind);
    PartRasterSource source{};
    source.pixels = std::span<const std::uint8_t>(scene.source.data(), scene.source.size());
    source.width = sw;
    source.height = sh;
    source.row_bytes = static_cast<std::size_t>(sw) * 4U;
    source.shown_width = shown_w;
    source.shown_height = shown_h;
    const std::uint32_t out_w = shown_w != 0U ? shown_w : sw;
    const std::uint32_t out_h = shown_h != 0U ? shown_h : sh;
    TextCompositeClip clip{};
    clip.rect = Rect{0.0, 0.0, 1920.0, 1080.0};
    std::vector<std::uint32_t> pixels = scene.target;
    const TextCompositeTarget target = target_of(pixels, 1920U, 1080U, false);
    const double total = static_cast<double>(out_w) * out_h * calls;
    const double base = best_seconds(15, [&]() {
        for (int call = 0; call < calls; ++call) {
            PartRasterSource placed = source;
            placed.left = 37 + (call * 53) % (1920 - static_cast<int>(out_w) - 40);
            placed.top = 11 + (call * 29) % (1080 - static_cast<int>(out_h) - 20);
            TextCompositeClip placed_clip = clip;
            if (radius > 0.0) {
                placed_clip.rounded = true;
                placed_clip.rounded_rect = Rect{static_cast<double>(placed.left), static_cast<double>(placed.top),
                                                static_cast<double>(out_w), static_cast<double>(out_h)};
                placed_clip.radius = radius;
            }
            static_cast<void>(gui_forms::detail::composite_part_raster(placed, target, placed_clip, opacity));
        }
    });
    const double fast = best_seconds(15, [&]() {
        for (int call = 0; call < calls; ++call) {
            PartRasterSource placed = source;
            placed.left = 37 + (call * 53) % (1920 - static_cast<int>(out_w) - 40);
            placed.top = 11 + (call * 29) % (1080 - static_cast<int>(out_h) - 20);
            TextCompositeClip placed_clip = clip;
            if (radius > 0.0) {
                placed_clip.rounded = true;
                placed_clip.rounded_rect = Rect{static_cast<double>(placed.left), static_cast<double>(placed.top),
                                                static_cast<double>(out_w), static_cast<double>(out_h)};
                placed_clip.radius = radius;
            }
            static_cast<void>(fast_part(placed, target, placed_clip, opacity, whole_opaque));
        }
    });
    report(name, base, fast, total);
}

static void time_text(std::mt19937& rng, const char* name, SourceKind kind, std::uint8_t alpha, std::uint32_t sw,
                      std::uint32_t sh, int calls) {
    Scene scene = make_scene(rng, 1920U, 1080U, sw, sh, kind);
    TextCoverage coverage{};
    coverage.pixels = std::span<const std::uint8_t>(scene.coverage.data(), scene.coverage.size());
    coverage.width = sw;
    coverage.height = sh;
    TextCompositeClip clip{};
    clip.rect = Rect{0.0, 0.0, 1920.0, 1080.0};
    std::vector<std::uint32_t> pixels = scene.target;
    const TextCompositeTarget target = target_of(pixels, 1920U, 1080U, false);
    Color colour{};
    colour.red = 20U;
    colour.green = 30U;
    colour.blue = 40U;
    colour.alpha = alpha;
    const double total = static_cast<double>(sw) * sh * calls;
    const double base = best_seconds(15, [&]() {
        for (int call = 0; call < calls; ++call) {
            TextCoverage placed = coverage;
            placed.left = 37 + (call * 53) % (1920 - static_cast<int>(sw) - 40);
            placed.top = 11 + (call * 29) % (1080 - static_cast<int>(sh) - 20);
            static_cast<void>(gui_forms::detail::composite_text_coverage(placed, target, clip, colour));
        }
    });
    const double fast = best_seconds(15, [&]() {
        for (int call = 0; call < calls; ++call) {
            TextCoverage placed = coverage;
            placed.left = 37 + (call * 53) % (1920 - static_cast<int>(sw) - 40);
            placed.top = 11 + (call * 29) % (1080 - static_cast<int>(sh) - 20);
            static_cast<void>(fast_text(placed, target, clip, colour));
        }
    });
    report(name, base, fast, total);
}

} // namespace lab

int main() {
    std::mt19937 rng(20261111U);
    if (!lab::check_identities()) return 1;
    std::printf("identities: div255 exact on 0..65025; weight identity exact on all 65536 byte pairs\n");
    if (!lab::check_equal(rng)) return 1;
    std::printf("\n(ns per source pixel; MB/s of target written; best of 15)\n");
    using lab::SourceKind;
    lab::time_part(rng, "part 800x600 translucent, opacity 255", SourceKind::translucent, 255U, false, 800U, 600U, 4);
    lab::time_part(rng, "part 800x600 translucent, opacity 180", SourceKind::translucent, 180U, false, 800U, 600U, 4);
    lab::time_part(rng, "part 800x600 opaque, not declared", SourceKind::opaque, 255U, false, 800U, 600U, 4);
    lab::time_part(rng, "part 800x600 opaque, declared by its maker", SourceKind::opaque, 255U, true, 800U, 600U, 4);
    lab::time_part(rng, "part 800x600 opaque, opacity 180", SourceKind::opaque, 180U, true, 800U, 600U, 4);
    lab::time_part(rng, "part 120x28 button-like, opacity 255", SourceKind::part_like, 255U, false, 120U, 28U, 400);
    lab::time_part(rng, "part 120x28 button-like, opacity 180", SourceKind::part_like, 180U, false, 120U, 28U, 400);
    lab::time_part(rng, "part 24x24 glyph-box-like, opacity 255", SourceKind::part_like, 255U, false, 24U, 24U, 4000);
    lab::time_part(rng, "part 800x600 opaque, rounded clip r 12", SourceKind::opaque, 255U, false, 800U, 600U, 4, 12.0);
    lab::time_part(rng, "part 800x600 translucent, rounded r 12", SourceKind::translucent, 255U, false, 800U, 600U, 4, 12.0);
    lab::time_part(rng, "part 120x28 button-like, rounded r 6", SourceKind::part_like, 255U, false, 120U, 28U, 400, 6.0);
    lab::time_part(rng, "part 400x300 shown at 800x600 (scaled)", SourceKind::translucent, 255U, false, 400U, 300U, 4, 0.0, 800U, 600U);
    lab::time_part(rng, "part 800x600 shown at 600x450 (scaled)", SourceKind::opaque, 255U, false, 800U, 600U, 4, 0.0, 600U, 450U);
    lab::time_part(rng, "part 60x14 shown at 120x28 (scaled)", SourceKind::part_like, 255U, false, 60U, 14U, 400, 0.0, 120U, 28U);
    lab::time_text(rng, "text 600x400 glyph-like, colour alpha 255", SourceKind::text_like, 255U, 600U, 400U, 8);
    lab::time_text(rng, "text 600x400 glyph-like, colour alpha 160", SourceKind::text_like, 160U, 600U, 400U, 8);
    lab::time_text(rng, "text 600x400 noise, colour alpha 255", SourceKind::translucent, 255U, 600U, 400U, 8);
    lab::time_text(rng, "text 90x16 label, colour alpha 255", SourceKind::text_like, 255U, 90U, 16U, 2000);
    return 0;
}
