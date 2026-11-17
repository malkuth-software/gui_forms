#include "sheen_primitive.hpp"

#include "../kernel/pixel_kernel.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

const PartRole sheen_roles[] = {PartRole::sheen};

constexpr std::size_t sheen_colour = 0U;
constexpr std::size_t sheen_width = 1U;
constexpr std::size_t sheen_rest = 2U;

const PrimitiveParameterDeclaration sheen_parameters[] = {
    {"colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 96}},
    {"width", PrimitiveParameterKind::number, 2.0, 96.0, 24.0, {}},
    {"rest", PrimitiveParameterKind::number, 0.0, 1.0, 0.25, {}},
};

const PrimitiveDescriptor sheen_descriptor{
    "sheen", EngineVersion{0, 2}, sheen_roles, {}, sheen_parameters, PrimitiveCost{16U, 16U, 0U}, Insets{}, 0U};

// The band's weight at a pixel centre `x` device pixels into a band `width`
// device pixels wide: (1 - t²)² at t = 2x / width - 1, zero outside.
double band_weight(double x, double width) noexcept {
    const double half = width * 0.5;
    const double t = (x - half) / half;
    if (!(t > -1.0 && t < 1.0)) return 0.0;
    const double inside = 1.0 - t * t;
    const double weight = inside * inside;
    return weight;
}

} // namespace

const PrimitiveDescriptor& SheenPrimitive::descriptor() const noexcept {
    return sheen_descriptor;
}

bool SheenPrimitive::blank(const PrimitiveParameterBlock& parameters) const noexcept {
    const bool clear = parameters.values[sheen_colour].colour.alpha == 0U;
    return clear;
}

stxlib::Optional<PointerOverlay> SheenPrimitive::pointer_overlay(const PrimitiveParameterBlock& parameters,
                                                              Size part) const noexcept {
    PointerOverlay overlay{};
    overlay.size = Size{parameters.values[sheen_width].number, part.height};
    overlay.rest = parameters.values[sheen_rest].number;
    return stxlib::Optional<PointerOverlay>::of(overlay);
}

void SheenPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void SheenPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                          std::span<const double>, const PartRasterView& destination) const noexcept {
    const Color colour = parameters.values[sheen_colour].colour;
    // The band spans the logical width at the target's scale; the raster's
    // last column may be partly outside it.
    const double width = target.logical_size.width * target.scale.value;
    // The first row, then the same bytes down the band.
    for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
        const double centre = static_cast<double>(column) + 0.5;
        const Unit pixel = premultiply(colour, band_weight(centre, width));
        store(destination, static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
    }
    const std::span<std::uint8_t> bytes = destination.pixels;
    const std::size_t row = static_cast<std::size_t>(destination.extent.width) * part_raster_bytes_per_pixel;
    for (std::uint32_t line = 1U; line < destination.extent.height; ++line) {
        const std::size_t start = static_cast<std::size_t>(line) * destination.row_bytes;
        std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(row),
                  bytes.begin() + static_cast<std::ptrdiff_t>(start));
    }
}

} // namespace gui_forms::engine_primitives
