// Applying a paint group's effect: see group_effect.hpp.

#include "group_effect.hpp"

#include "../../engine/primitives/kernel/pixel_kernel.hpp"
#include "../../engine/primitives/material/material_raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace gui_forms::detail {
namespace {

// The widest or tallest image the blur accepts, margins included: the
// blur's two images then stay within 4 × 2^14 × 2^14 elements of 32 bits.
constexpr std::size_t maximum_blur_side = std::size_t{1} << 14U;

// Whether `image` is whole: some pixels, rows at least a row wide, and
// every row within the span.
bool whole_image(const GroupEffectImage& image) noexcept {
    if (image.width == 0U || image.height == 0U) return false;
    const std::size_t row = static_cast<std::size_t>(image.width) * 4U;
    if (image.row_bytes < row) return false;
    if (image.pixels.size() < row) return false;
    // The rows before the last each take row_bytes; the last takes `row`.
    const std::size_t rows_before_last = static_cast<std::size_t>(image.height) - 1U;
    const bool fits = rows_before_last <= (image.pixels.size() - row) / image.row_bytes;
    return fits;
}

// Each pixel through the colour stages (pixel_kernel's colour_staged).
void filter_colours(const MaterialColourFilter& filter, const GroupEffectImage& image) noexcept {
    const MaterialColourMatrix& matrix = *filter;
    const MaterialColourTable* const table = filter.table();
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        std::uint8_t* const row = image.pixels.data() + static_cast<std::size_t>(y) * image.row_bytes;
        for (std::uint32_t x = 0U; x < image.width; ++x) {
            std::uint8_t* const pixel = row + static_cast<std::size_t>(x) * 4U;
            const engine_primitives::Unit unit{static_cast<double>(pixel[2]) / 255.0,
                                               static_cast<double>(pixel[1]) / 255.0,
                                               static_cast<double>(pixel[0]) / 255.0,
                                               static_cast<double>(pixel[3]) / 255.0};
            const engine_primitives::Unit filtered = engine_primitives::colour_staged(matrix, table, unit);
            pixel[0] = engine_primitives::to_byte(filtered.blue);
            pixel[1] = engine_primitives::to_byte(filtered.green);
            pixel[2] = engine_primitives::to_byte(filtered.red);
            pixel[3] = engine_primitives::to_byte(filtered.alpha);
        }
    }
}

// The image blurred in place by three boxes of half width `half`: loaded
// into the work's first image inside a transparent margin of 3h on every
// side, so that the result has the image's own size.
void blur_image(engine_primitives::BoxBlurWork& work, const GroupEffectImage& image, const std::uint32_t half) {
    const std::size_t margin = static_cast<std::size_t>(half) * 3U;
    const std::size_t columns = static_cast<std::size_t>(image.width) + 2U * margin;
    const std::size_t rows = static_cast<std::size_t>(image.height) + 2U * margin;
    engine_primitives::prepare_box_blur(work, columns, rows, engine_primitives::box_blur_lanes);
    const std::size_t stride = columns * engine_primitives::box_blur_lanes;
    const std::size_t row_elements = static_cast<std::size_t>(image.width) * 4U;
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        const std::uint8_t* const source = image.pixels.data() + static_cast<std::size_t>(y) * image.row_bytes;
        std::uint32_t* const loaded = work.first.data() + (static_cast<std::size_t>(y) + margin) * stride + margin * 4U;
        std::copy(source, source + row_elements, loaded);
    }
    engine_primitives::blur_three_boxes(work, columns, rows, engine_primitives::box_blur_lanes, half);
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        std::uint8_t* const target = image.pixels.data() + static_cast<std::size_t>(y) * image.row_bytes;
        const std::uint32_t* const blurred = work.first.data() + static_cast<std::size_t>(y) * stride;
        for (std::size_t element = 0U; element < row_elements; ++element) {
            target[element] = static_cast<std::uint8_t>(std::min<std::uint32_t>(blurred[element], 255U));
        }
    }
}

// The mask for `effect` over the bounds, from the work's last bake when the
// effect, size and scale are the same; null when the bake is refused.
std::shared_ptr<const PartRaster> group_mask(const std::shared_ptr<const PaintGroupEffect>& effect,
                                             const GroupEffectPlacement& placement, GroupEffectWork& work) {
    const bool reusable = work.mask && work.mask_effect == effect && work.mask_size.width == placement.bounds_size.width &&
                          work.mask_size.height == placement.bounds_size.height &&
                          work.mask_scale == placement.scale.value;
    if (reusable) return work.mask;
    const SurfaceMaterial material = paint_group_effect_mask_material(*effect);
    std::shared_ptr<const PartRaster> baked = bake_material_raster(material, placement.bounds_size, placement.scale, 0.0);
    work.mask_effect = effect;
    work.mask_size = placement.bounds_size;
    work.mask_scale = placement.scale.value;
    work.mask = baked;
    return baked;
}

// Each pixel's channels weighted by the mask's alpha at its place: the mask
// raster's (0, 0) at the bounds' top-left, 0 beyond it.
void apply_mask(const PartRaster& mask, const GroupEffectImage& image, const GroupEffectPlacement& placement) noexcept {
    const std::span<const std::uint8_t> mask_pixels = mask.pixels();
    const DeviceExtent extent = mask.extent();
    const std::size_t mask_row_bytes = mask.row_bytes();
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        std::uint8_t* const row = image.pixels.data() + static_cast<std::size_t>(y) * image.row_bytes;
        const std::int64_t mask_y = static_cast<std::int64_t>(y) - placement.bounds_top;
        const bool row_inside = mask_y >= 0 && mask_y < static_cast<std::int64_t>(extent.height);
        for (std::uint32_t x = 0U; x < image.width; ++x) {
            std::uint8_t* const pixel = row + static_cast<std::size_t>(x) * 4U;
            const std::int64_t mask_x = static_cast<std::int64_t>(x) - placement.bounds_left;
            const bool inside = row_inside && mask_x >= 0 && mask_x < static_cast<std::int64_t>(extent.width);
            unsigned weight = 0U;
            if (inside) {
                const std::size_t offset =
                    static_cast<std::size_t>(mask_y) * mask_row_bytes + static_cast<std::size_t>(mask_x) * 4U + 3U;
                weight = mask_pixels[offset];
            }
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                pixel[lane] = static_cast<std::uint8_t>((pixel[lane] * weight + 127U) / 255U);
            }
        }
    }
}

} // namespace

GroupEffectStatus apply_group_effect(const std::shared_ptr<const PaintGroupEffect>& effect, const GroupEffectImage image,
                                     const GroupEffectPlacement placement, GroupEffectWork& work) {
    if (!effect || !valid_paint_group_effect(*effect) || !whole_image(image)) return GroupEffectStatus::refused;
    const PaintGroupEffect& stages = *effect;
    const std::uint32_t half = engine_primitives::box_blur_half(stages.blur, placement.scale.value);
    const std::size_t margin = static_cast<std::size_t>(half) * 3U;
    const bool blur_fits = static_cast<std::size_t>(image.width) + 2U * margin <= maximum_blur_side &&
                           static_cast<std::size_t>(image.height) + 2U * margin <= maximum_blur_side;
    if (half != 0U && !blur_fits) return GroupEffectStatus::refused;
    // The mask first, so that a refused bake leaves the image untouched.
    std::shared_ptr<const PartRaster> mask{};
    if (!stages.masks.empty()) {
        mask = group_mask(effect, placement, work);
        if (!mask) return GroupEffectStatus::refused;
    }
    if (stages.colour_filter) filter_colours(stages.colour_filter, image);
    if (half != 0U) blur_image(work.blur, image, half);
    if (mask) apply_mask(*mask, image, placement);
    return GroupEffectStatus::applied;
}

} // namespace gui_forms::detail
