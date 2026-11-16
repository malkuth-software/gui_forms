#pragma once

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/surface_material/paint_group_effect/paint_group_effect.hpp"

#include "../../engine/primitives/kernel/box_blur.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms::detail {

// Applying a paint group's effect (PaintGroupEffect) to its isolated image,
// for the painters that isolate groups: the stages in their order, in
// place, through the material kernels (colour_staged, the three-box blur
// and a mask baked by bake_material_raster).

// The group's image: premultiplied BGRA8 (memory order blue, green, red,
// alpha), `width` × `height` pixels, rows `row_bytes` apart.
struct GroupEffectImage final {
    std::span<std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_bytes{};
};

// Where the group's bounds lie in the image, in device pixels: the bounds'
// top-left at (bounds_left, bounds_top), which may lie outside the image,
// and their logical size, at `scale` device pixels per logical unit.
struct GroupEffectPlacement final {
    std::int64_t bounds_left{};
    std::int64_t bounds_top{};
    Size bounds_size{};
    DeviceScale scale{1.0};
};

// The storage an effect reuses from one group to the next, owned by the
// painter: the blur's images and the last mask, rebaked only when the
// effect, size or scale differ.
struct GroupEffectWork final {
    engine_primitives::BoxBlurWork blur{};
    std::shared_ptr<const PaintGroupEffect> mask_effect{};
    Size mask_size{};
    double mask_scale{};
    std::shared_ptr<const PartRaster> mask{};
};

enum class GroupEffectStatus : std::uint8_t {
    applied,
    // Nothing changed: the effect is invalid, the image's shape is not
    // whole, or the mask's bake was refused (past the bake budget or the
    // raster bound). The caller composites the image as it is and counts a
    // refusal.
    refused,
};

// Applies `effect` to `image` in place: the colour stages, then the blur,
// then the mask (PaintGroupEffect). Validates the effect and the image's
// shape before writing any pixel. The blur treats pixels beyond the image
// as transparent. Throws std::bad_alloc when the blur's or the mask's
// storage cannot be held; the image is then unchanged if the throw came
// before the blur, else blurred without its mask.
[[nodiscard]] GroupEffectStatus apply_group_effect(const std::shared_ptr<const PaintGroupEffect>& effect,
                                                   GroupEffectImage image, GroupEffectPlacement placement,
                                                   GroupEffectWork& work);

} // namespace gui_forms::detail
