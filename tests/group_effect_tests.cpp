// A paint group's effect stage (capability review decision 1, owner
// 2026-11-16): PaintGroupEffect's validation, reach and mask material, and
// apply_group_effect's stages on an image, each byte worked by hand.

#include "../src/core/display/group_effect/group_effect.hpp"

#include "gui_forms/surface_material.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_group_effect_tests: " << message << '\n';
    std::exit(1);
}

// An image of `width` × `height` pixels, BGRA in memory, every pixel the
// premultiplied (red, green, blue, alpha).
struct TestImage final {
    std::vector<std::uint8_t> bytes{};
    std::uint32_t width{};
    std::uint32_t height{};

    detail::GroupEffectImage view() {
        const detail::GroupEffectImage image{.pixels = std::span<std::uint8_t>(bytes.data(), bytes.size()),
                                             .width = width,
                                             .height = height,
                                             .row_bytes = static_cast<std::size_t>(width) * 4U};
        return image;
    }
    std::array<std::uint8_t, 4> pixel(std::uint32_t x, std::uint32_t y) const {
        const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
        const std::array<std::uint8_t, 4> read{bytes[offset + 2U], bytes[offset + 1U], bytes[offset], bytes[offset + 3U]};
        return read;
    }
    void set(std::uint32_t x, std::uint32_t y, std::array<std::uint8_t, 4> rgba) {
        const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
        bytes[offset] = rgba[2];
        bytes[offset + 1U] = rgba[1];
        bytes[offset + 2U] = rgba[0];
        bytes[offset + 3U] = rgba[3];
    }
};

TestImage image_of(std::uint32_t width, std::uint32_t height, std::array<std::uint8_t, 4> rgba) {
    TestImage made{};
    made.width = width;
    made.height = height;
    made.bytes.reserve(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t index = 0U; index < static_cast<std::size_t>(width) * height; ++index) {
        made.bytes.push_back(rgba[2]);
        made.bytes.push_back(rgba[1]);
        made.bytes.push_back(rgba[0]);
        made.bytes.push_back(rgba[3]);
    }
    return made;
}

detail::GroupEffectPlacement placed(std::int64_t left, std::int64_t top, Size size) {
    const detail::GroupEffectPlacement placement{
        .bounds_left = left, .bounds_top = top, .bounds_size = size, .scale = DeviceScale{1.0}};
    return placement;
}

void validation_and_reach() {
    PaintGroupEffect effect{};
    require(valid_paint_group_effect(effect), "an empty effect is valid");
    require(paint_group_effect_reach(effect) == 0.0, "without a blur it reaches nothing");
    effect.blur = 4.0;
    require(valid_paint_group_effect(effect) && paint_group_effect_reach(effect) == 9.0,
            "a blur of 4 reaches 1.5 × 4 + 3 logical units");
    effect.blur = maximum_paint_group_effect_blur + 1.0;
    require(!valid_paint_group_effect(effect), "a blur past the cap is refused");
    effect.blur = std::numeric_limits<double>::quiet_NaN();
    require(!valid_paint_group_effect(effect), "a blur that is not a number is refused");
    effect.blur = 0.0;
    effect.masks.push_back(MaterialMaskLayer{.source = MaterialFillLayer::solid(Color::rgba(0, 0, 0, 128))});
    require(valid_paint_group_effect(effect), "a solid mask is valid");
    const SurfaceMaterial material = paint_group_effect_mask_material(effect);
    require(material.fills.size() == 1U && material.masks == effect.masks, "the mask material is white under the masks");
    PaintGroupEffect imaged{};
    MaterialMaskLayer image_layer{};
    image_layer.source.kind = MaterialFillKind::image;
    imaged.masks.push_back(image_layer);
    require(!valid_paint_group_effect(imaged), "an image mask is refused");
    PaintGroupEffect crowded{};
    crowded.masks.assign(SurfaceMaterial::maximum_mask_layers + 1U, effect.masks[0]);
    require(!valid_paint_group_effect(crowded), "more mask layers than a material holds are refused");
}

void colour_stage() {
    PaintGroupEffect fill{};
    fill.colour_filter = MaterialColourMatrix::fill(Color::rgba(10, 20, 30));
    const std::shared_ptr<const PaintGroupEffect> shared = std::make_shared<const PaintGroupEffect>(fill);
    TestImage image = image_of(2U, 1U, {100U, 50U, 25U, 255U});
    image.set(1U, 0U, {64U, 32U, 16U, 128U});
    detail::GroupEffectWork work{};
    require(detail::apply_group_effect(shared, image.view(), placed(0, 0, Size{2.0, 1.0}), work) ==
                detail::GroupEffectStatus::applied,
            "a colour effect applies");
    require(image.pixel(0U, 0U) == (std::array<std::uint8_t, 4>{10U, 20U, 30U, 255U}), "fill replaces the colour");
    // Straight (10, 20, 30) at alpha 128/255, premultiplied and rounded:
    // 10 × 128 / 255 = 5.02, 20 × 128 / 255 = 10.04, 30 × 128 / 255 = 15.06.
    require(image.pixel(1U, 0U) == (std::array<std::uint8_t, 4>{5U, 10U, 15U, 128U}), "and keeps the alpha");
}

void blur_stage() {
    // One opaque white pixel in the middle of 7 × 7; a blur of 2 at scale 1
    // is three boxes of half width 1. Along an axis the passes give, with
    // each average (sum + 1) / 3: 85 85 85, then 28 57 85 57 28, then
    // 9 28 57 66 57 28 9; down the middle column 66 gives 22, then 15 22,
    // then 17 at the centre and 2 at the edge.
    PaintGroupEffect soft{};
    soft.blur = 2.0;
    const std::shared_ptr<const PaintGroupEffect> shared = std::make_shared<const PaintGroupEffect>(soft);
    TestImage image = image_of(7U, 7U, {0U, 0U, 0U, 0U});
    image.set(3U, 3U, {255U, 255U, 255U, 255U});
    detail::GroupEffectWork work{};
    require(detail::apply_group_effect(shared, image.view(), placed(0, 0, Size{7.0, 7.0}), work) ==
                detail::GroupEffectStatus::applied,
            "a blur applies");
    require(image.pixel(3U, 3U) == (std::array<std::uint8_t, 4>{17U, 17U, 17U, 17U}), "the centre keeps 17");
    require(image.pixel(3U, 0U) == (std::array<std::uint8_t, 4>{2U, 2U, 2U, 2U}), "the spread reaches the edge");
    require(image.pixel(0U, 0U) == (std::array<std::uint8_t, 4>{0U, 0U, 0U, 0U}), "the corner stays clear");
    require(image.pixel(3U, 2U) == image.pixel(2U, 3U) && image.pixel(4U, 3U) == image.pixel(3U, 4U),
            "the blur is symmetric");
}

void mask_stage() {
    // A half-alpha solid mask over the bounds (columns 1 and 2 of 4): each
    // channel × 128 / 255, rounded as (v × 128 + 127) / 255; 0 beyond.
    PaintGroupEffect masked{};
    masked.masks.push_back(MaterialMaskLayer{.source = MaterialFillLayer::solid(Color::rgba(0, 0, 0, 128))});
    const std::shared_ptr<const PaintGroupEffect> shared = std::make_shared<const PaintGroupEffect>(masked);
    TestImage image = image_of(4U, 2U, {200U, 100U, 50U, 255U});
    detail::GroupEffectWork work{};
    require(detail::apply_group_effect(shared, image.view(), placed(1, 0, Size{2.0, 2.0}), work) ==
                detail::GroupEffectStatus::applied,
            "a mask applies");
    require(image.pixel(1U, 0U) == (std::array<std::uint8_t, 4>{100U, 50U, 25U, 128U}) &&
                image.pixel(2U, 1U) == (std::array<std::uint8_t, 4>{100U, 50U, 25U, 128U}),
            "within the bounds the mask's alpha weights each channel");
    require(image.pixel(0U, 0U) == (std::array<std::uint8_t, 4>{0U, 0U, 0U, 0U}) &&
                image.pixel(3U, 1U) == (std::array<std::uint8_t, 4>{0U, 0U, 0U, 0U}),
            "beyond the bounds the mask is 0");
    const std::shared_ptr<const PartRaster> first = work.mask;
    TestImage again = image_of(4U, 2U, {200U, 100U, 50U, 255U});
    static_cast<void>(detail::apply_group_effect(shared, again.view(), placed(1, 0, Size{2.0, 2.0}), work));
    require(work.mask == first && again.bytes == image.bytes, "the same effect and size reuse the baked mask");
}

void refusals() {
    detail::GroupEffectWork work{};
    TestImage image = image_of(2U, 2U, {10U, 20U, 30U, 255U});
    const std::vector<std::uint8_t> before = image.bytes;
    require(detail::apply_group_effect(nullptr, image.view(), placed(0, 0, Size{2.0, 2.0}), work) ==
                detail::GroupEffectStatus::refused,
            "no effect is refused");
    PaintGroupEffect invalid{};
    invalid.blur = -1.0;
    invalid.colour_filter = MaterialColourMatrix::fill(Color::rgba(1, 2, 3));
    require(detail::apply_group_effect(std::make_shared<const PaintGroupEffect>(invalid), image.view(),
                                       placed(0, 0, Size{2.0, 2.0}), work) == detail::GroupEffectStatus::refused,
            "an invalid effect is refused");
    PaintGroupEffect valid{};
    valid.colour_filter = MaterialColourMatrix::fill(Color::rgba(1, 2, 3));
    detail::GroupEffectImage short_rows = image.view();
    short_rows.row_bytes = 4U;
    require(detail::apply_group_effect(std::make_shared<const PaintGroupEffect>(valid), short_rows,
                                       placed(0, 0, Size{2.0, 2.0}), work) == detail::GroupEffectStatus::refused,
            "rows narrower than the image are refused");
    detail::GroupEffectImage truncated = image.view();
    truncated.pixels = truncated.pixels.first(12U);
    require(detail::apply_group_effect(std::make_shared<const PaintGroupEffect>(valid), truncated,
                                       placed(0, 0, Size{2.0, 2.0}), work) == detail::GroupEffectStatus::refused,
            "an image past its span is refused");
    require(image.bytes == before, "a refusal changes no pixel");
}

} // namespace

int main() {
    validation_and_reach();
    colour_stage();
    blur_stage();
    mask_stage();
    refusals();
    std::cout << "group effects: validation, reach, colour, blur, mask and refusals passed\n";
    return EXIT_SUCCESS;
}
