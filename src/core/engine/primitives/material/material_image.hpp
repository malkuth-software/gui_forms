#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// A material's image fill (owner, 2026-10-22: supported, never used by a
// stock theme). The bake only composites: the image is prepared ahead as a
// variant of the part's device size (owner, 2026-10-22: CONV prepares
// face sizes; image_variant.hpp makes them), and each destination pixel
// takes the variant pixel its centre falls on. This file says which variant
// a fill needs and how its pixels map onto the destination.

namespace gui_forms::engine_primitives {

// A mapped destination pixel that shows no image: one beyond a single
// tile (MaterialImageMode::single).
inline constexpr std::uint32_t outside_image = std::numeric_limits<std::uint32_t>::max();

// An image's pixels as premultiplied BGRA8, rows packed: `width` × `height`
// × 4 live bytes.
struct MaterialImagePixels final {
    std::vector<std::uint8_t> bytes;
    std::uint32_t width{};
    std::uint32_t height{};
};

// Whether `fill`'s image is in `registry` with the pixel size the fill
// declares. No decoding.
[[nodiscard]] bool material_image_available(const ImageRegistry& registry, const MaterialFillLayer& fill) noexcept;

// Reads image `image` into `pixels` (reset-size), decoding a PNG with the
// shared core decoder. False, with `pixels` empty, when the image is not in
// the registry at `width` × `height` pixels or does not decode.
[[nodiscard]] bool read_material_image(const ImageRegistry& registry, ImageId image, std::uint32_t width,
                                       std::uint32_t height, MaterialImagePixels& pixels);

// Where an image lies along one axis, in the destination's device pixels.
// `start` and `length` are the part's box; `source_length` the image's
// pixels along the axis. A nine-patch's `slice_start` and `slice_end` are
// the source pixels of its two fixed segments and `destination_start` and
// `destination_end` their device lengths; a tile's `tile_length` is one
// tile's device length and `tile_origin` where one tile starts (the
// fill's position, E9.6).
struct ImageAxisPlacement final {
    MaterialImageMode mode{MaterialImageMode::stretch};
    double start{};
    double length{};
    double source_length{};
    double slice_start{};
    double slice_end{};
    double destination_start{};
    double destination_end{};
    double tile_length{};
    double tile_origin{};
};

// The placement of `fill`'s image across and down a box (device pixels) at
// `scale`, for an image of `source_width` × `source_height` pixels. A
// nine-patch's fixed segments shrink in proportion when they do not fit.
[[nodiscard]] ImageAxisPlacement image_placement_across(const MaterialFillLayer& fill, double box_start,
                                                        double box_length, double scale,
                                                        double source_width) noexcept;
[[nodiscard]] ImageAxisPlacement image_placement_down(const MaterialFillLayer& fill, double box_start,
                                                      double box_length, double scale,
                                                      double source_height) noexcept;

// The variant `fill` (fill layer `layer`) needs for a box of `box` device
// pixels at `scale`: one tile for a tiled image, else the box's length
// along each axis, rounded, at least 1 (one tile for a single image too);
// a nine-patch's fixed segments at
// their device lengths, rounded and kept within the variant. `fill` is an
// image fill with a positive declared pixel size.
[[nodiscard]] ImageVariantRequest image_variant_request(const MaterialFillLayer& fill, std::size_t layer, Rect box,
                                                        double scale) noexcept;

// Fills `map` (reset-size to `outputs`) with the variant pixel each
// destination pixel along the axis shows, for a variant `variant_length`
// pixels long whose fixed segments are `fixed_start` and `fixed_end`
// pixels (both 0 but for a nine-patch). Each destination pixel's centre is
// mapped proportionally within its segment (within its tile, for a tile)
// and the variant pixel it falls on is taken; centres before or beyond the
// box take the first or last pixel, and those outside a single image's
// tile take outside_image. `variant_length` is at least 1.
void map_image_variant(const ImageAxisPlacement& placement, std::uint32_t variant_length, std::uint32_t fixed_start,
                       std::uint32_t fixed_end, std::uint32_t outputs, std::vector<std::uint32_t>& map);

} // namespace gui_forms::engine_primitives
