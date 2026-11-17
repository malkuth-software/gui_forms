#include "../src/core/text/composite/clip_mask.hpp"
#include "../src/core/text/composite/text_composite.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// ADR-004 shared text blending: one integer arithmetic, anti-aliased clips.
namespace {
using namespace gui_forms;
using detail::TextCompositeClip;
using detail::TextCompositeStatus;
using detail::TextCompositeTarget;
using detail::TextCoverage;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

std::uint32_t composite_one(const std::uint8_t coverage_value, const Color color, const std::uint32_t destination,
                            const bool opaque) {
    std::array<std::uint32_t, 1> pixels{destination};
    const std::array<std::uint8_t, 1> coverage{coverage_value};
    const TextCompositeStatus status = detail::composite_text_coverage(
        {.pixels = coverage, .width = 1, .height = 1, .left = 0, .top = 0},
        {.pixels = pixels, .width = 1, .height = 1, .stride = 1, .opaque = opaque},
        {.rect = {0.0, 0.0, 1.0, 1.0}}, color);
    require(status == TextCompositeStatus::success, "single pixel composite");
    return pixels[0];
}

// The unclipped alpha must equal the pre-ADR-004 Windows rule exactly, so
// existing Windows output is unchanged wherever clips are whole pixels.
void unclipped_alpha_matches_previous_rule() {
    for (unsigned coverage = 0; coverage < 256U; ++coverage) {
        for (unsigned alpha = 0; alpha < 256U; ++alpha) {
            const unsigned previous = (coverage * alpha + 127U) / 255U;
            const unsigned shared = static_cast<unsigned>((static_cast<std::uint64_t>(coverage) * alpha * 256U + 32640U) / 65280U);
            require(previous == shared, "unclipped alpha equals the previous Windows rule");
        }
    }
    // Black at alpha 128 over white: (255 * 127 + 127) / 255 = 127 per channel.
    const std::uint32_t grey = composite_one(255U, Color::rgba(0, 0, 0, 128), 0xffffffffU, true);
    require(grey == 0xff7f7f7fU, "previous Windows alpha/blend contract");
}

// Targets are BGRA8: as a little-endian word, blue is the low byte and red is
// at bit 16.
void pixels_are_bgra() {
    const std::uint32_t covered = composite_one(255U, Color::rgba(200, 40, 90, 255), 0U, false);
    require(covered == ((255U << 24U) | (200U << 16U) | (40U << 8U) | 90U), "red at bit 16, blue in the low byte");
    // Each destination channel blends with its own source channel.
    const std::uint32_t destination = (200U << 24U) | (12U << 16U) | (34U << 8U) | 56U;
    const std::uint32_t half = composite_one(128U, Color::rgba(255, 0, 0, 255), destination, false);
    require(((half >> 16U) & 255U) > 12U && (half & 255U) < 56U, "red rises and blue falls under a red source");
}

void alpha_channel_modes() {
    const std::uint32_t transparent = 0U;
    const std::uint32_t premultiplied = composite_one(255U, Color::rgba(255, 255, 255, 128), transparent, false);
    require((premultiplied >> 24U) == 128U, "premultiplied destination gains the source alpha");
    const std::uint32_t opaque = composite_one(255U, Color::rgba(255, 255, 255, 128), transparent, true);
    require((opaque >> 24U) == 255U, "opaque destinations stay opaque");
}

// D6: an inner rounded clip in the corner of an outer one keeps the outer
// arc. The inner clip alone covers the outer corner pixel fully; nested, it
// takes the outer clip's coverage there, and without an outer clip the
// result is the inner clip's alone.
void nested_rounded_clips_keep_both_arcs() {
    const TextCompositeClip outer_only{.rect = {0.0, 0.0, 40.0, 40.0}, .rounded = true,
                                       .rounded_rect = {0.0, 0.0, 40.0, 40.0}, .radius = 16.0};
    const TextCompositeClip inner_only{.rect = {0.0, 0.0, 20.0, 20.0}, .rounded = true,
                                       .rounded_rect = {0.0, 0.0, 20.0, 20.0}, .radius = 2.0};
    TextCompositeClip nested = inner_only;
    nested.outer_rounded = true;
    nested.outer_rounded_rect = outer_only.rounded_rect;
    nested.outer_radius = outer_only.radius;
    require(detail::text_clip_coverage(inner_only, 2, 2) == 256U, "the inner clip alone covers the outer corner");
    require(detail::text_clip_coverage(nested, 2, 2) == detail::text_clip_coverage(outer_only, 2, 2) &&
            detail::text_clip_coverage(outer_only, 2, 2) < 256U,
            "the nested clip keeps the outer arc");
    require(detail::text_clip_coverage(nested, 10, 10) == 256U, "the shared interior is fully covered");
    for (std::int64_t y = 0; y < 20; ++y) {
        for (std::int64_t x = 0; x < 20; ++x) {
            require(detail::text_clip_coverage(nested, x, y) <= detail::text_clip_coverage(inner_only, x, y) &&
                    detail::text_clip_coverage(nested, x, y) <= detail::text_clip_coverage(outer_only, x, y),
                    "nested coverage never exceeds either clip");
        }
    }
    TextCompositeClip ignored = outer_only;
    ignored.rounded = false;
    ignored.outer_rounded = true;
    ignored.outer_rounded_rect = {0.0, 0.0, 4.0, 4.0};
    require(detail::text_clip_coverage(ignored, 2, 2) == 256U, "an outer clip without an inner one is not read");
}

void fractional_rect_edges_are_area_coverage() {
    // A clip starting a quarter into pixel 0 covers three quarters of it.
    const TextCompositeClip quarter{.rect = {0.25, 0.0, 10.0, 10.0}};
    require(detail::text_clip_coverage(quarter, 0, 0) == 192U, "three-quarter edge coverage");
    require(detail::text_clip_coverage(quarter, 1, 0) == 256U, "interior full coverage");
    require(detail::text_clip_coverage(quarter, 11, 0) == 0U, "exterior zero coverage");
    const TextCompositeClip corner{.rect = {0.5, 0.5, 10.0, 10.0}};
    require(detail::text_clip_coverage(corner, 0, 0) == 64U, "corner pixel is the product of both edges");
    const TextCompositeClip whole{.rect = {2.0, 2.0, 4.0, 4.0}};
    require(detail::text_clip_coverage(whole, 1, 3) == 0U && detail::text_clip_coverage(whole, 2, 3) == 256U &&
            detail::text_clip_coverage(whole, 5, 3) == 256U && detail::text_clip_coverage(whole, 6, 3) == 0U,
            "whole-pixel edges stay hard");
}

void rounded_corners_are_antialiased() {
    const TextCompositeClip round{.rect = {0.0, 0.0, 40.0, 40.0}, .rounded = true,
                                  .rounded_rect = {0.0, 0.0, 40.0, 40.0}, .radius = 16.0};
    require(detail::text_clip_coverage(round, 0, 0) == 0U, "outer corner pixel is outside");
    require(detail::text_clip_coverage(round, 20, 20) == 256U, "centre is fully inside");
    require(detail::text_clip_coverage(round, 0, 20) == 256U, "straight edge is unaffected");
    // Along the diagonal coverage rises monotonically through partial values.
    std::uint32_t previous = 0U;
    bool partial = false;
    for (std::int64_t step = 0; step < 16; ++step) {
        const std::uint32_t value = detail::text_clip_coverage(round, step, step);
        require(value >= previous, "corner coverage is monotonic inward");
        if (value > 0U && value < 256U) partial = true;
        previous = value;
    }
    require(partial, "corner arc has anti-aliased pixels");
    // All four corners agree by symmetry.
    for (std::int64_t offset = 0; offset < 16; ++offset) {
        for (std::int64_t other = 0; other < 16; ++other) {
            const std::uint32_t top_left = detail::text_clip_coverage(round, offset, other);
            require(top_left == detail::text_clip_coverage(round, 39 - offset, other) &&
                    top_left == detail::text_clip_coverage(round, offset, 39 - other) &&
                    top_left == detail::text_clip_coverage(round, 39 - offset, 39 - other),
                    "rounded corners are symmetric");
        }
    }
}

void invalid_geometry_writes_nothing() {
    std::vector<std::uint32_t> pixels(16, 0x11223344U);
    const std::vector<std::uint32_t> before = pixels;
    const std::array<std::uint8_t, 4> coverage{255U, 255U, 255U, 255U};
    const TextCompositeStatus stride = detail::composite_text_coverage(
        {.pixels = coverage, .width = 2, .height = 2},
        {.pixels = pixels, .width = 4, .height = 4, .stride = 3}, {.rect = {0.0, 0.0, 4.0, 4.0}}, Color::rgba(0, 0, 0, 255));
    const TextCompositeStatus clip = detail::composite_text_coverage(
        {.pixels = coverage, .width = 2, .height = 2},
        {.pixels = pixels, .width = 4, .height = 4, .stride = 4}, {.rect = {0.0, 0.0, -1.0, 4.0}}, Color::rgba(0, 0, 0, 255));
    require(stride == TextCompositeStatus::invalid_geometry && clip == TextCompositeStatus::invalid_geometry && pixels == before,
            "invalid geometry leaves the target untouched");
}

void clipped_composite_blends_partially() {
    std::vector<std::uint32_t> pixels(4, 0xffffffffU);
    const std::array<std::uint8_t, 4> coverage{255U, 255U, 255U, 255U};
    const TextCompositeStatus status = detail::composite_text_coverage(
        {.pixels = coverage, .width = 4, .height = 1},
        {.pixels = pixels, .width = 4, .height = 1, .stride = 4, .opaque = true},
        {.rect = {0.5, 0.0, 3.0, 1.0}}, Color::rgba(0, 0, 0, 255));
    require(status == TextCompositeStatus::success, "clipped composite succeeds");
    // Half-covered edge pixels blend half way; covered pixels are black.
    require(pixels[0] == 0xff7f7f7fU && pixels[1] == 0xff000000U && pixels[2] == 0xff000000U && pixels[3] == 0xff7f7f7fU,
            "partially clipped pixels blend by clip area");
}

} // namespace

// SourceCopy replaces the destination with the premultiplied colour in
// proportion to coverage; it agrees with source-over for opaque colours.
void source_mode_replaces() {
    std::array<std::uint32_t, 1> pixels{0xff00ff00U};
    const std::array<std::uint8_t, 1> full{255U};
    const TextCompositeTarget target{.pixels = pixels, .width = 1, .height = 1, .stride = 1};
    const TextCompositeClip clip{.rect = {0.0, 0.0, 1.0, 1.0}};
    TextCompositeStatus status = detail::composite_text_coverage({.pixels = full, .width = 1, .height = 1}, target, clip,
                                                                 Color::rgba(255, 0, 0, 128), detail::TextCompositeMode::source);
    // Premultiplied red at alpha 128 replaces opaque green entirely.
    require(status == TextCompositeStatus::success && pixels[0] == 0x80800000U, "full coverage copies the premultiplied colour");
    pixels[0] = 0xff00ff00U;
    const std::array<std::uint8_t, 1> half{128U};
    status = detail::composite_text_coverage({.pixels = half, .width = 1, .height = 1}, target, clip, Color::rgba(0, 0, 0, 0),
                                             detail::TextCompositeMode::source);
    // Transparent colour erases in proportion to coverage: (255 * 127 + 127) / 255.
    require(status == TextCompositeStatus::success && pixels[0] == 0x7f007f00U, "transparent source erases by coverage");
    for (unsigned coverage = 0; coverage < 256U; coverage += 17U) {
        const std::array<std::uint8_t, 1> value{static_cast<std::uint8_t>(coverage)};
        std::array<std::uint32_t, 1> over{0xff336699U};
        std::array<std::uint32_t, 1> copy{0xff336699U};
        const Color colour = Color::rgba(200, 40, 90, 255);
        static_cast<void>(detail::composite_text_coverage({.pixels = value, .width = 1, .height = 1},
                                                          {.pixels = over, .width = 1, .height = 1, .stride = 1}, clip, colour));
        static_cast<void>(detail::composite_text_coverage({.pixels = value, .width = 1, .height = 1},
                                                          {.pixels = copy, .width = 1, .height = 1, .stride = 1}, clip, colour,
                                                          detail::TextCompositeMode::source));
        require(over[0] == copy[0], "opaque source equals source-over");
    }
}

// The blends' division by 255 is replaced by a shift identity: it gives
// (value + 127) / 255 for every value the blends produce, and beyond.
void quotient_identity_is_exact() {
    for (unsigned value = 0U; value <= 65535U; ++value) {
        if (detail::rounded_quotient_by_255(value) != (value + 127U) / 255U) {
            require(false, "the quotient identity differs from the division");
        }
    }
}

// The interior (clip coverage a whole unit) takes the block kernels, and a
// rounded clip of radius 0 has the same interior as a rectangular one: the
// two give the same bytes. part_composite_tests checks the interior against
// the per-pixel formula.
void interior_table_matches_coverage() {
    constexpr std::uint32_t width = 11U;
    constexpr std::uint32_t height = 7U;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0U; index < coverage.size(); ++index) {
        coverage[index] = static_cast<std::uint8_t>((index * 53U + 7U) % 256U);
    }
    constexpr std::array<Rect, 3> clips{{{1.25, 0.5, 9.5, 6.25}, {0.0, 0.0, 13.0, 9.0}, {3.0, 2.0, 4.0, 3.0}}};
    const std::array<Color, 3> colours{Color::rgba(200, 40, 90, 255), Color::rgba(10, 220, 130, 128),
                                       Color::rgba(0, 0, 0, 0)};
    constexpr std::array<detail::TextCompositeMode, 2> modes{detail::TextCompositeMode::source_over,
                                                             detail::TextCompositeMode::source};
    constexpr std::array<bool, 2> opacities{false, true};
    for (const Rect clip_rect : clips) {
        for (const Color colour : colours) {
            for (const detail::TextCompositeMode mode : modes) {
                for (const bool opaque : opacities) {
                    std::vector<std::uint32_t> table((width + 2U) * (height + 2U), 0x80406020U);
                    std::vector<std::uint32_t> covered = table;
                    const TextCoverage source{.pixels = coverage, .width = width, .height = height, .left = 1,
                                              .top = 1};
                    TextCompositeClip rectangular{.rect = clip_rect};
                    TextCompositeClip rounded = rectangular;
                    rounded.rounded = true;
                    rounded.rounded_rect = {0.0, 0.0, static_cast<double>(width + 2U), static_cast<double>(height + 2U)};
                    rounded.radius = 0.0;
                    const TextCompositeStatus first = detail::composite_text_coverage(
                        source, {.pixels = table, .width = width + 2U, .height = height + 2U, .stride = width + 2U,
                                 .opaque = opaque},
                        rectangular, colour, mode);
                    const TextCompositeStatus second = detail::composite_text_coverage(
                        source, {.pixels = covered, .width = width + 2U, .height = height + 2U, .stride = width + 2U,
                                 .opaque = opaque},
                        rounded, colour, mode);
                    require(first == TextCompositeStatus::success && second == TextCompositeStatus::success,
                            "both clips composite");
                    require(table == covered, "the rectangular and radius-0 rounded clips agree");
                }
            }
        }
    }
}

// -- path clip masks (clip_mask.hpp) ----------------------------------------------

using detail::ClipMaskBounds;
using detail::ClipMaskStatus;
using detail::ClipMaskWorkspace;

// Rasterizes one contour of `points` into `coverage` over `bounds`.
ClipMaskStatus rasterize(const std::vector<Point>& points, const bool closed, const PolygonFillRule rule,
                         const ClipMaskBounds bounds, std::vector<std::uint16_t>& coverage) {
    ClipMaskWorkspace workspace;
    const std::array<FlattenedContour, 1> contours{FlattenedContour{points.size(), closed}};
    const ClipMaskStatus status = detail::rasterize_clip_mask(points, contours, rule, bounds, workspace, coverage);
    return status;
}

std::vector<Point> box_points(const double left, const double top, const double right, const double bottom) {
    const std::vector<Point> points{{left, top}, {right, top}, {right, bottom}, {left, bottom}, {left, top}};
    return points;
}

// Edges across at quarters and down at multiples of 1/16 of a pixel: the
// mask's coverage equals the clip rectangle's exact area coverage.
void mask_of_a_box_is_its_area() {
    const ClipMaskBounds bounds{0, 0, 8U, 8U};
    std::vector<std::uint16_t> coverage(64U, 999U);
    const ClipMaskStatus status =
        rasterize(box_points(1.25, 2.5, 5.75, 6.0625), true, PolygonFillRule::nonzero, bounds, coverage);
    require(status == ClipMaskStatus::success, "a box rasterizes");
    const TextCompositeClip box{.rect = {1.25, 2.5, 4.5, 3.5625}};
    for (std::int64_t y = 0; y < 8; ++y) {
        for (std::int64_t x = 0; x < 8; ++x) {
            const std::uint32_t expected = detail::text_clip_coverage(box, x, y);
            const std::uint16_t value = coverage[static_cast<std::size_t>(y * 8 + x)];
            require(value == expected, "a box's mask must equal its exact area coverage");
        }
    }
}

// A diagonal through pixel corners: the pixels it crosses are half covered
// exactly, those beside it whole or empty.
void mask_of_a_diagonal_is_exact() {
    const ClipMaskBounds bounds{0, 0, 8U, 8U};
    std::vector<std::uint16_t> coverage(64U, 0U);
    const std::vector<Point> triangle{{0.0, 0.0}, {8.0, 0.0}, {8.0, 8.0}, {0.0, 0.0}};
    require(rasterize(triangle, true, PolygonFillRule::nonzero, bounds, coverage) == ClipMaskStatus::success,
            "a triangle rasterizes");
    for (std::size_t y = 0U; y < 8U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            std::uint16_t expected = 0U;
            if (x > y) expected = 256U;
            if (x == y) expected = 128U;
            require(coverage[y * 8U + x] == expected, "a diagonal's pixels must be half covered, exactly");
        }
    }
}

// Two overlapping boxes wound the same way: nonzero fills their overlap,
// even-odd leaves it open. Open contours are closed as a fill closes them.
void mask_fill_rules() {
    ClipMaskWorkspace workspace;
    const std::vector<Point> points{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0},
                                    {2.0, 2.0}, {6.0, 2.0}, {6.0, 6.0}, {2.0, 6.0}};
    const std::array<FlattenedContour, 2> open{FlattenedContour{4U, false}, FlattenedContour{8U, false}};
    const ClipMaskBounds bounds{0, 0, 6U, 6U};
    std::vector<std::uint16_t> nonzero(36U, 0U);
    std::vector<std::uint16_t> even_odd(36U, 0U);
    require(detail::rasterize_clip_mask(points, open, PolygonFillRule::nonzero, bounds, workspace, nonzero) ==
                    ClipMaskStatus::success &&
                detail::rasterize_clip_mask(points, open, PolygonFillRule::even_odd, bounds, workspace, even_odd) ==
                    ClipMaskStatus::success,
            "overlapping boxes rasterize under both rules");
    require(nonzero[3U * 6U + 3U] == 256U && even_odd[3U * 6U + 3U] == 0U,
            "nonzero must fill the overlap and even-odd leave it open");
    require(nonzero[1U * 6U + 1U] == 256U && even_odd[1U * 6U + 1U] == 256U && even_odd[5U * 6U + 5U] == 256U &&
                nonzero[0U * 6U + 5U] == 0U,
            "outside the overlap the rules agree");
}

void mask_refusals_write_nothing() {
    const ClipMaskBounds bounds{0, 0, 4U, 4U};
    std::vector<std::uint16_t> coverage(16U, 7U);
    const std::vector<Point> box = box_points(0.0, 0.0, 2.0, 2.0);
    require(rasterize(box, true, static_cast<PolygonFillRule>(9), bounds, coverage) == ClipMaskStatus::invalid_geometry,
            "an unknown rule is refused");
    std::vector<std::uint16_t> short_storage(15U, 7U);
    require(rasterize(box, true, PolygonFillRule::nonzero, bounds, short_storage) == ClipMaskStatus::invalid_geometry,
            "storage short of the bounds is refused");
    const std::vector<Point> far = box_points(0.0, 0.0, 5'000'000.0, 2.0);
    require(rasterize(far, true, PolygonFillRule::nonzero, bounds, coverage) == ClipMaskStatus::out_of_range,
            "a point beyond the coordinate limit is refused");
    ClipMaskWorkspace workspace;
    const std::array<FlattenedContour, 1> past_end{FlattenedContour{box.size() + 1U, true}};
    require(detail::rasterize_clip_mask(box, past_end, PolygonFillRule::nonzero, bounds, workspace, coverage) ==
                ClipMaskStatus::invalid_geometry,
            "a contour past the points is refused");
    for (const std::uint16_t value : coverage) require(value == 7U, "a refused mask writes nothing");
}

void masks_intersect() {
    // Enclosing: four pixels at x 0..3; inner: three at x 2..4.
    const std::vector<std::uint16_t> enclosing{0U, 0U, 128U, 200U};
    std::vector<std::uint16_t> inner{256U, 128U, 256U};
    require(detail::intersect_clip_mask(ClipMaskBounds{0, 0, 4U, 1U}, enclosing, ClipMaskBounds{2, 0, 3U, 1U}, inner),
            "two masks intersect");
    require(inner[0] == 128U && inner[1] == 100U && inner[2] == 0U,
            "the product is rounded once per pixel and empty beyond the enclosing mask");
}

// The red channel a composite of full text coverage in opaque red over
// opaque black writes at mask entry m: alpha (65025 m + 32640) / 65280.
std::uint32_t red_over_black(const std::uint32_t mask_entry) {
    const std::uint32_t alpha = (65025U * mask_entry + 32640U) / 65280U;
    return 0xFF000000U | (alpha << 16U);
}

void masked_text_composite() {
    constexpr std::uint32_t width = 80U;
    constexpr std::uint32_t height = 3U;
    std::vector<std::uint16_t> mask(width * height, 0U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::uint16_t value = static_cast<std::uint16_t>((x * 37U + y * 11U) % 257U);
            // A run of whole entries for the interior's block kernels.
            if (x >= 20U && x < 60U) value = 256U;
            mask[y * width + x] = value;
        }
    }
    const std::vector<std::uint8_t> text(width * height, 255U);
    std::vector<std::uint32_t> masked(width * height, 0xFF000000U);
    std::vector<std::uint32_t> whole(width * height, 0xFF000000U);
    TextCompositeClip clip{.rect = {0.0, 0.0, static_cast<double>(width), static_cast<double>(height)}};
    clip.masked = true;
    clip.mask = detail::TextClipMask{.coverage = mask, .width = width, .height = height, .left = 0, .top = 0};
    const TextCoverage coverage{.pixels = text, .width = width, .height = height, .left = 0, .top = 0};
    const Color opaque_red = Color::rgba(255, 0, 0);
    require(detail::composite_text_coverage(coverage, {.pixels = masked, .width = width, .height = height, .stride = width},
                                            clip, opaque_red) == TextCompositeStatus::success,
            "masked composite");
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::uint16_t entry = mask[y * width + x];
            require(detail::text_clip_coverage(clip, x, y) == entry, "a masked clip's coverage is the mask's entry");
            require(masked[y * width + x] == red_over_black(entry), "masked text blends at the mask's coverage");
        }
    }
    require(detail::text_clip_coverage(clip, 80, 1) == 0U && detail::text_clip_coverage(clip, 4, -1) == 0U,
            "beyond the mask the coverage is 0");
    // A wholly covering mask composites the unmasked bytes.
    std::vector<std::uint16_t> full(width * height, 256U);
    clip.mask.coverage = full;
    require(detail::composite_text_coverage(coverage, {.pixels = whole, .width = width, .height = height, .stride = width},
                                            clip, opaque_red) == TextCompositeStatus::success,
            "full-mask composite");
    std::vector<std::uint32_t> unmasked(width * height, 0xFF000000U);
    const TextCompositeClip plain{.rect = {0.0, 0.0, static_cast<double>(width), static_cast<double>(height)}};
    require(detail::composite_text_coverage(coverage, {.pixels = unmasked, .width = width, .height = height, .stride = width},
                                            plain, opaque_red) == TextCompositeStatus::success,
            "unmasked composite");
    require(whole == unmasked, "a whole mask must composite the unmasked bytes");
    // A mask with fewer entries than its extent is refused before writing.
    clip.mask.coverage = std::span<const std::uint16_t>(full.data(), full.size() - 1U);
    std::vector<std::uint32_t> untouched(width * height, 0xFF000000U);
    require(detail::composite_text_coverage(coverage, {.pixels = untouched, .width = width, .height = height,
                                                       .stride = width},
                                            clip, opaque_red) == TextCompositeStatus::invalid_geometry,
            "a short mask is refused");
    for (const std::uint32_t pixel : untouched) require(pixel == 0xFF000000U, "a refused composite writes nothing");
}

// An opaque raster at full opacity under a mask: copied where the mask is
// whole, blended by the entry elsewhere, untouched where it is 0.
void masked_part_composite() {
    constexpr std::uint32_t width = 72U;
    std::vector<std::uint8_t> source(width * 4U, 0U);
    for (std::uint32_t x = 0U; x < width; ++x) {
        source[x * 4U + 2U] = 255U;
        source[x * 4U + 3U] = 255U;
    }
    std::vector<std::uint16_t> mask(width, 256U);
    mask[0] = 0U;
    mask[1] = 128U;
    mask[70] = 64U;
    std::vector<std::uint32_t> target(width, 0xFF000000U);
    TextCompositeClip clip{.rect = {0.0, 0.0, static_cast<double>(width), 1.0}};
    clip.masked = true;
    clip.mask = detail::TextClipMask{.coverage = mask, .width = width, .height = 1U, .left = 0, .top = 0};
    const detail::PartRasterSource raster{.pixels = source, .width = width, .height = 1U, .row_bytes = width * 4U,
                                          .opaque = true};
    require(detail::composite_part_raster(raster, {.pixels = target, .width = width, .height = 1U, .stride = width},
                                          clip, 255U) == TextCompositeStatus::success,
            "masked part composite");
    for (std::uint32_t x = 0U; x < width; ++x) {
        require(target[x] == red_over_black(mask[x]), "a masked part raster blends at the mask's coverage");
    }
}

int main() {
    quotient_identity_is_exact();
    interior_table_matches_coverage();
    unclipped_alpha_matches_previous_rule();
    pixels_are_bgra();
    alpha_channel_modes();
    fractional_rect_edges_are_area_coverage();
    rounded_corners_are_antialiased();
    nested_rounded_clips_keep_both_arcs();
    invalid_geometry_writes_nothing();
    clipped_composite_blends_partially();
    source_mode_replaces();
    mask_of_a_box_is_its_area();
    mask_of_a_diagonal_is_exact();
    mask_fill_rules();
    mask_refusals_write_nothing();
    masks_intersect();
    masked_text_composite();
    masked_part_composite();
    return 0;
}
