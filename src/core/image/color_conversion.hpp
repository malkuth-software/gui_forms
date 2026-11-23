#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms::image {

// Codec-neutral colour conversion of decoded straight-alpha RGBA16 samples to
// sRGB (stage I6; owner decision of 2026-10-20). A codec adapter (for PNG,
// png_color.hpp) reads its format's colour metadata into a ColorEncoding;
// nothing here knows a file format.
//
// Degenerate values are dropped, as if undeclared, and reported: a gamma
// outside 16..625000000 (x 100000; the range the previous libpng paths
// accepted), or chromaticities with any y = 0, x + y > 1, a singular primary
// matrix, or a white point outside the primaries' triangle. Gamma without
// chromaticities assumes sRGB primaries and D65; chromaticities without gamma
// assume the sRGB transfer function. No "approximately sRGB" special case.

enum class ColorSource {
    // Nothing declared: sRGB by default.
    srgb_default,
    // The source declares sRGB.
    srgb_declared,
    // An embedded profile the caller's rule ignores: treated as sRGB.
    profile_ignored,
    // Gamma and/or chromaticities declared and usable.
    gamma_chromaticity
};

struct ColorEncoding final {
    ColorSource source = ColorSource::srgb_default;
    // Image gamma times 100000 (an encoding gamma; 45455 is about 1/2.2);
    // none without a usable gamma.
    std::optional<std::uint32_t> gamma{};
    // Chromaticities times 100000: white x, y; red x, y; green x, y; blue x, y;
    // none without usable chromaticities.
    std::optional<std::array<std::uint32_t, 8>> chromaticities{};
    bool ignored_gamma = false;
    bool ignored_chromaticities = false;

    [[nodiscard]] bool converts() const noexcept {
        const bool result = source == ColorSource::gamma_chromaticity;
        return result;
    }
};

// For an encoding built from declared gamma and/or chromaticities: drops the
// degenerate values (setting ignored_*), then sets source to
// gamma_chromaticity when anything usable remains, else srgb_default.
void settle_gamma_chromaticities(ColorEncoding& encoding) noexcept;

// Converts straight-alpha RGBA16 samples in place to sRGB for an encoding
// that converts(); alpha is unchanged. Pipeline, in doubles with contraction
// off and the engine's deterministic power (no host libm):
//   1. decode: s = sample / 65535; linear = s ^ (100000 / gamma), or the sRGB
//      decoding curve without a usable gamma;
//   2. gamut: rgb = M * linear, M = inverse(sRGB primaries) * Bradford(white
//      -> D65) * (image primaries); without usable chromaticities the matrix
//      step is skipped;
//   3. clip each channel to [0, 1];
//   4. encode: the 16-bit code whose sRGB-decoded midpoint thresholds bracket
//      the linear value, that is round(65535 * sRGB_encode(linear)), found by
//      binary search over 65535 precomputed thresholds.
// Rounding rule: nearest 16-bit code, a value exactly on a threshold taking
// the upper code. Error bound: within one 16-bit code of the exact result
// (STXlib's pow, within 0.51 ulp, moves a value across a threshold only
// within about 1e-15 of it); the subsequent 8-bit conversion rounds again.
// work is reusable scratch (lookup tables) owned by the caller.
struct ColorWorkspace final {
    // Decoded linear value per possible sample, 256 entries for 8-bit and
    // lower depths (every sample is a multiple of 257), 65536 for 16-bit.
    std::vector<double> linear{};
    // Output code per sample when there is no matrix step.
    std::vector<std::uint16_t> direct{};
};
// depth is the source's bits per sample: 16, or 8 and below (whose samples
// are multiples of 257 after expansion).
[[nodiscard]] bool convert_samples_to_srgb(const ColorEncoding& encoding,
    unsigned depth, std::span<std::uint16_t> samples, ColorWorkspace& work);

} // namespace gui_forms::image
