#include "text_mask_native.hpp"

#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cmath>

namespace gui_forms::detail::mask_native {
namespace {

// The widest or tallest ink a mask may have, in pixels.
constexpr std::int64_t maximum_mask_dimension = 4096;

// How the options' raster profile is drawn (ADR-004): its fit and size, and
// whether x keeps its 1/64 pixel phase.
struct RasterConfiguration final {
    double scale{1.0};
    stxlib::glyphs::DrawStyle style{};
    // Horizontal placement keeps its 1/64 pixel phase (ADR-004 integer 26.6
    // placement); vertical placement stays on whole pixels. Mono is whole-pixel.
    bool subpixel_x{true};
    bool monochrome{false};
};

RasterConfiguration make_raster_configuration(const MaskOptions& options) {
    RasterConfiguration result{};
    result.scale = options.scale;
    const double scaled_fixed = static_cast<double>(options.size_64) * options.scale;
    if (!std::isfinite(scaled_fixed) || scaled_fixed < 1.0 || scaled_fixed > 1073741824.0) {
        throw NativeFailure(TextMaskStatus::unsupported_profile);
    }
    result.style.size = static_cast<stxlib::i64>(std::floor(scaled_fixed + 0.5));
    // ADR-004: outline_gray is the shared light auto-hinted grey profile.
    result.style.fit = stxlib::glyphs::Fit::light;
    if (options.raster == TextMaskRaster::true_mono) {
        result.style.fit = stxlib::glyphs::Fit::monochrome;
        result.subpixel_x = false;
        result.monochrome = true;
    }
    return result;
}

// One glyph as STXlib takes it: its device position in 26.6 from the mask
// origin, y downward. Throws for a glyph its face has not and for a
// position beyond the mask's coordinate range.
stxlib::glyphs::RunGlyph place_glyph(const NativeFonts& fonts, const RasterConfiguration& configuration,
                                     const NativeGlyph& glyph) {
    if (glyph.face >= fonts.count) throw NativeFailure(TextMaskStatus::native_failure);
    const stxlib::glyphs::Font* const font = fonts.raster_faces[glyph.face].font();
    if (font == nullptr) throw NativeFailure(TextMaskStatus::native_failure);
    if (glyph.glyph == 0 || glyph.glyph >= (*font).glyph_count) throw NativeFailure(TextMaskStatus::missing_font_coverage);
    const double device_x = glyph.x * configuration.scale;
    const double device_y = glyph.y * configuration.scale;
    if (!std::isfinite(device_x) || !std::isfinite(device_y) || std::abs(device_x) > 16'000'000.0 || std::abs(device_y) > 16'000'000.0) {
        throw MaskLimitFailure(TextMaskLimit::mask_dimension);
    }
    std::int64_t fixed_x = std::llround(device_x) * 64;
    if (configuration.subpixel_x) fixed_x = std::llround(device_x * 64.0);
    const std::int64_t fixed_y = std::llround(device_y) * 64;
    stxlib::glyphs::RunGlyph placed{};
    placed.glyph = glyph.glyph;
    placed.x = static_cast<std::int32_t>(fixed_x);
    placed.y = static_cast<std::int32_t>(fixed_y);
    return placed;
}

// The face run starting at `begin`: its glyphs, placed, into work.glyphs.
// Returns the index after the run.
std::size_t place_face_run(const NativeFonts& fonts, const RasterConfiguration& configuration,
                           const std::span<const NativeGlyph> glyphs, const std::size_t begin, NativeRasterWork& work) {
    work.glyphs.clear();
    const std::uint32_t face = glyphs[begin].face;
    std::size_t end = begin;
    while (end < glyphs.size() && glyphs[end].face == face) {
        work.glyphs.push_back(place_glyph(fonts, configuration, glyphs[end]));
        ++end;
    }
    return end;
}

// Blends one face run's grey mask into the target at (left, top) by
// c + p * (255 - c) / 255.
void blend_gray_run(const stxlib::glyphs::RunCoverage& coverage, const std::size_t left, const std::size_t top,
                    TextMaskStorage& target) {
    const stxlib::View<stxlib::u8> pixels = coverage.pixels.view();
    const std::size_t stride = target.metrics.stride_bytes;
    for (std::size_t row = 0; row < coverage.height; ++row) {
        const std::size_t source_row = row * coverage.width;
        const std::size_t target_row = (top + row) * stride + left;
        for (std::size_t column = 0; column < coverage.width; ++column) {
            const unsigned value = pixels[source_row + column];
            const unsigned previous = target.pixels[target_row + column];
            const unsigned combined = value + previous * (255U - value) / 255U;
            target.pixels[target_row + column] = static_cast<std::uint8_t>(combined);
        }
    }
}

// Blends one face run's monochrome mask (every byte 0 or 255) into the
// target at (left, top) by the larger.
void blend_mono_run(const stxlib::glyphs::RunCoverage& coverage, const std::size_t left, const std::size_t top,
                    TextMaskStorage& target) {
    const stxlib::View<stxlib::u8> pixels = coverage.pixels.view();
    const std::size_t stride = target.metrics.stride_bytes;
    for (std::size_t row = 0; row < coverage.height; ++row) {
        const std::size_t source_row = row * coverage.width;
        const std::size_t target_row = (top + row) * stride + left;
        for (std::size_t column = 0; column < coverage.width; ++column) {
            const std::uint8_t value = pixels[source_row + column];
            const std::uint8_t previous = target.pixels[target_row + column];
            target.pixels[target_row + column] = std::max(value, previous);
        }
    }
}
} // namespace

NativeInk measure_native_ink(NativeFonts& fonts, const MaskOptions& options, const std::span<const NativeGlyph> glyphs,
                             NativeRasterWork& work) {
    const RasterConfiguration configuration = make_raster_configuration(options);
    NativeInk bounds{};
    std::size_t next = 0;
    while (next < glyphs.size()) {
        const std::uint32_t face = glyphs[next].face;
        next = place_face_run(fonts, configuration, glyphs, next, work);
        stxlib::glyphs::RunBox box{};
        const stxlib::View<stxlib::glyphs::RunGlyph> placed(work.glyphs.data(), work.glyphs.size());
        const stxlib::Status measured = fonts.raster_faces[face].measure_run(placed, configuration.style, box);
        if (measured.failed() || box.missing_glyphs != 0U) throw NativeFailure(TextMaskStatus::native_failure);
        if (box.oversized) throw MaskLimitFailure(TextMaskLimit::mask_dimension);
        if (box.right <= box.left || box.bottom <= box.top) continue;
        if (!bounds.present) bounds = {box.left, box.top, box.right, box.bottom, true};
        else {
            bounds.left = std::min<std::int64_t>(bounds.left, box.left);
            bounds.top = std::min<std::int64_t>(bounds.top, box.top);
            bounds.right = std::max<std::int64_t>(bounds.right, box.right);
            bounds.bottom = std::max<std::int64_t>(bounds.bottom, box.bottom);
        }
        if (bounds.right - bounds.left > maximum_mask_dimension || bounds.bottom - bounds.top > maximum_mask_dimension) {
            throw MaskLimitFailure(TextMaskLimit::mask_dimension);
        }
    }
    return bounds;
}

void fill_native_mask(NativeFonts& fonts, const MaskOptions& options, const std::span<const NativeGlyph> glyphs,
                      NativeRasterWork& work, TextMaskStorage& storage) {
    const RasterConfiguration configuration = make_raster_configuration(options);
    std::size_t next = 0;
    while (next < glyphs.size()) {
        const std::uint32_t face = glyphs[next].face;
        next = place_face_run(fonts, configuration, glyphs, next, work);
        const stxlib::View<stxlib::glyphs::RunGlyph> placed(work.glyphs.data(), work.glyphs.size());
        const stxlib::Status drawn = fonts.raster_faces[face].draw_run(placed, configuration.style, work.coverage);
        const stxlib::glyphs::RunCoverage& coverage = work.coverage;
        if (drawn.failed() || coverage.missing_glyphs != 0U || coverage.oversized) {
            throw NativeFailure(TextMaskStatus::native_failure);
        }
        if (coverage.width == 0U || coverage.height == 0U) continue;
        const std::int64_t left = static_cast<std::int64_t>(coverage.left) - storage.metrics.ink_left_px;
        const std::int64_t top = static_cast<std::int64_t>(coverage.top) - storage.metrics.ink_top_px;
        if (left < 0 || top < 0 || left + coverage.width > storage.metrics.width_px ||
            top + coverage.height > storage.metrics.height_px) {
            throw NativeFailure(TextMaskStatus::native_failure);
        }
        const std::size_t target_left = static_cast<std::size_t>(left);
        const std::size_t target_top = static_cast<std::size_t>(top);
        if (configuration.monochrome) blend_mono_run(coverage, target_left, target_top, storage);
        else blend_gray_run(coverage, target_left, target_top, storage);
    }
}
} // namespace gui_forms::detail::mask_native
