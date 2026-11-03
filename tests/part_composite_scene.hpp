#pragma once

#include "gui_forms/engine.hpp"
#include "gui_forms/types.hpp"
#include "text_identity_scene.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

// The theme engine's composite identity scene (T2 plan, E2b; GIVEN A2-2):
// part rasters drawn by the engine's own primitives, composited through each
// host's painter with Painter::draw_part_raster. The same painter calls must
// produce the same bytes on every platform. Hashes and dumps use the text
// identity scene's helpers (hashed in RGBA order).
namespace gui_forms::part_composite_identity {

enum class Case : std::uint8_t { plain, faded, translated, fractional_clip, rounded_clip };

inline constexpr std::array<Case, 5> cases{Case::plain, Case::faded, Case::translated, Case::fractional_clip,
                                           Case::rounded_clip};
inline constexpr std::array<const char*, 5> case_names{"plain", "faded", "translated", "fractional_clip",
                                                       "rounded_clip"};
// Every origin below lands on a whole device pixel at each scale, so no host
// rounds a half.
inline constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
inline constexpr Size scene_size{120.0, 48.0};
inline constexpr Size part_size{40.0, 28.0};

// Expected FNV-1a hashes of the raster's rows in RGBA order, [case][scale],
// from SkiaRaster, the one CPU painter. First recorded through the Windows
// DIB painter (Windows x64, clang 22), 2026-10-20; re-recorded through
// SkiaRaster on Windows x64 (clang 22.1.8), 2026-11-02 (SK5), unchanged.
// Every host checks this one table.
// GUI_FORMS_PART_COMPOSITE_PRINT=1 prints the observed table instead.
inline constexpr std::array<std::array<std::uint64_t, 3>, 5> expected{{
    {0x76930cce6775bc50ULL, 0xf84b6b81bb71753bULL, 0x8909e0d61d0afca7ULL},
    {0x2879fea809874149ULL, 0xf2d5c2ad77f8e3c1ULL, 0x52535b4d03fe0b25ULL},
    {0x20f5676ad1fd236eULL, 0xff8276d6c39faf91ULL, 0x372daa5e29ddbfafULL},
    {0xc61f56c660cdabd8ULL, 0x0ac903046e6e2f4bULL, 0x6b5e8248d3012bc8ULL},
    {0xf502e0731d9858dbULL, 0xaf4544f70dbfb94fULL, 0xf8149137f584bfd2ULL},
}};

// One part raster: a built-in primitive drawn at `scale` into a raster the
// size of part_size at that scale, rounded up.
inline std::shared_ptr<const PartRaster> make_part(const char* primitive_name, bool pressed, double scale) {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find(primitive_name);
    if (primitive == nullptr) throw std::runtime_error("the built-in primitive is registered");
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil(part_size.width * scale)),
                              static_cast<std::uint32_t>(std::ceil(part_size.height * scale))};
    const std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{scale});
    if (!raster) throw std::runtime_error("the part raster is made");
    AnatomyStateSet states{};
    if (pressed) states.insert(AnatomyState::pressed);
    const PrimitiveTarget target{part_size, DeviceScale{scale}, states};
    const PrimitiveParameterBlock parameters = default_parameters((*primitive).descriptor());
    PrimitivePreparation preparation{};
    if ((*primitive).prepare(parameters, target, preparation) != PreparationOutcome::prepared) {
        throw std::runtime_error("the part prepares");
    }
    if ((*primitive).rasterize(parameters, target, preparation, (*raster).view()) != RasterOutcome::painted) {
        throw std::runtime_error("the part rasterizes");
    }
    return raster;
}

// Paints the scene after the frame has begun, at the painter's `scale`.
inline void paint_scene(Painter& painter, const Case kind, const double scale) {
    painter.fill_rect({0.0, 0.0, scene_size.width, scene_size.height}, Color::rgba(222, 232, 246, 255));
    painter.fill_rect({0.0, 24.0, scene_size.width, 24.0}, Color::rgba(40, 70, 110, 255));
    // The dithered bulge is opaque dots over transparency; the smooth bulge
    // is translucent premultiplied colour.
    const std::shared_ptr<const PartRaster> dithered = make_part("dithered_bulge", false, scale);
    const std::shared_ptr<const PartRaster> smooth = make_part("bulge", true, scale);
    const double opacity = kind == Case::faded ? 0.5 : 1.0;
    painter.save();
    if (kind == Case::fractional_clip) painter.clip_rect({9.3, 3.4, 70.5, 33.75});
    if (kind == Case::rounded_clip) painter.clip_rounded_rect({10.0, 9.0, 84.0, 26.0}, 10.0);
    if (kind == Case::translated) painter.translate({2.0, 4.0});
    painter.draw_part_raster(dithered, Point{6.0, 6.0}, opacity);
    painter.draw_part_raster(smooth, Point{58.0, 12.0}, opacity);
    painter.restore();
}

// Paint groups (Painter::begin_group; T2 E7, owner decision E7-1): engine
// rasters and opaque fills drawn inside groups, composited through the
// shared blend, so every host writes the same bytes.
enum class GroupCase : std::uint8_t {
    isolated,
    nested,
    empty,
    clipped,
    opacity_zero,
    opacity_one,
    refused,
    translated,
    overlap,
};

inline constexpr std::array<GroupCase, 9> group_cases{GroupCase::isolated,     GroupCase::nested,
                                                      GroupCase::empty,        GroupCase::clipped,
                                                      GroupCase::opacity_zero, GroupCase::opacity_one,
                                                      GroupCase::refused,      GroupCase::translated,
                                                      GroupCase::overlap};
inline constexpr std::array<const char*, 9> group_case_names{"isolated",     "nested",      "empty",
                                                             "clipped",      "opacity_zero", "opacity_one",
                                                             "refused",      "translated",  "overlap"};

// Expected FNV-1a hashes of the raster's rows in RGBA order, [case][scale],
// from SkiaRaster, the one CPU painter. First recorded through the Windows
// DIB painter (Windows x64, clang 22), 2026-10-21; re-recorded through
// SkiaRaster on Windows x64 (clang 22.1.8), 2026-11-02 (SK5), unchanged.
// Every host checks this one table.
// GUI_FORMS_PART_COMPOSITE_PRINT=1 prints the observed table instead.
inline constexpr std::array<std::array<std::uint64_t, 3>, 9> expected_groups{{
    {0xbbdc19788bd13499ULL, 0x6c3059a7826c9bb9ULL, 0x776795552a69caadULL},
    {0xc183a2164b45b65bULL, 0xec6594a4e86abc22ULL, 0x25f850baee2383daULL},
    {0x71f66f11bdc74903ULL, 0xf3ace0f9d5950c23ULL, 0xf99dba7b260b4d83ULL},
    {0x1dccba5d934eafaaULL, 0xb366485acb055227ULL, 0xae0d2722293c7987ULL},
    {0x71f66f11bdc74903ULL, 0xf3ace0f9d5950c23ULL, 0xf99dba7b260b4d83ULL},
    {0x76930cce6775bc50ULL, 0xf84b6b81bb71753bULL, 0x8909e0d61d0afca7ULL},
    {0x507848af44929242ULL, 0x8a75fbb0401ef37bULL, 0x94620dda471017e3ULL},
    {0x6176d49fd632bb53ULL, 0x1c331be338c312e7ULL, 0xb9433b8e76ee884fULL},
    {0x11c0cf9a2220aba3ULL, 0x693153d5a2a435ebULL, 0x585ca9bcec2ded03ULL},
}};

inline constexpr Rect whole_scene{0.0, 0.0, scene_size.width, scene_size.height};
// The overlap case's two fills, red then blue, overlapping over x 30 to 50.
inline constexpr Rect overlap_red{10.0, 8.0, 40.0, 20.0};
inline constexpr Rect overlap_blue{30.0, 8.0, 40.0, 20.0};

inline void paint_background(Painter& painter) {
    painter.fill_rect(whole_scene, Color::rgba(222, 232, 246, 255));
    painter.fill_rect({0.0, 24.0, scene_size.width, 24.0}, Color::rgba(40, 70, 110, 255));
}

inline PaintGroup group_of(Rect bounds, double opacity) {
    PaintGroup group{};
    group.bounds = bounds;
    group.opacity = opacity;
    return group;
}

// Paints the group scene after the frame has begun, at the painter's
// `scale`. Opacity 1 draws the plain case's rasters, so it must equal
// drawing them without a group.
inline void paint_group_scene(Painter& painter, const GroupCase kind, const double scale) {
    paint_background(painter);
    const std::shared_ptr<const PartRaster> dithered = make_part("dithered_bulge", false, scale);
    const std::shared_ptr<const PartRaster> smooth = make_part("bulge", true, scale);
    switch (kind) {
    case GroupCase::isolated:
        painter.begin_group(group_of(whole_scene, 0.5));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.draw_part_raster(smooth, Point{20.0, 10.0}, 1.0);
        painter.end_group();
        return;
    case GroupCase::nested:
        painter.begin_group(group_of({2.0, 2.0, 110.0, 40.0}, 0.75));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.begin_group(group_of({30.0, 4.0, 60.0, 36.0}, 0.5));
        painter.draw_part_raster(smooth, Point{40.0, 8.0}, 1.0);
        painter.end_group();
        painter.end_group();
        return;
    case GroupCase::empty:
        painter.begin_group(group_of(whole_scene, 0.5));
        painter.end_group();
        return;
    case GroupCase::clipped:
        painter.save();
        painter.clip_rect({9.3, 3.4, 70.5, 33.75});
        painter.begin_group(group_of(whole_scene, 0.5));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.draw_part_raster(smooth, Point{58.0, 12.0}, 1.0);
        painter.end_group();
        painter.restore();
        return;
    case GroupCase::opacity_zero:
        painter.begin_group(group_of(whole_scene, 0.0));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.end_group();
        return;
    case GroupCase::opacity_one:
        painter.begin_group(group_of(whole_scene, 1.0));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.draw_part_raster(smooth, Point{58.0, 12.0}, 1.0);
        painter.end_group();
        return;
    case GroupCase::refused:
        // Five deep: the fifth is refused and draws without isolation.
        for (std::size_t level = 0U; level < maximum_paint_group_depth + 1U; ++level) {
            painter.begin_group(group_of(whole_scene, 0.875));
        }
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        for (std::size_t level = 0U; level < maximum_paint_group_depth + 1U; ++level) painter.end_group();
        return;
    case GroupCase::translated:
        painter.save();
        painter.translate({2.0, 4.0});
        painter.begin_group(group_of({4.0, 2.0, 60.0, 30.0}, 0.625));
        painter.draw_part_raster(dithered, Point{6.0, 6.0}, 1.0);
        painter.draw_part_raster(smooth, Point{30.0, 4.0}, 1.0);
        painter.end_group();
        painter.restore();
        return;
    case GroupCase::overlap:
        painter.begin_group(group_of(whole_scene, 0.5));
        painter.fill_rect(overlap_red, Color::rgba(200, 30, 40, 255));
        painter.fill_rect(overlap_blue, Color::rgba(20, 60, 210, 255));
        painter.end_group();
        return;
    }
    // A case outside the enumeration draws only the background.
    return;
}

// The pixel at logical (x, y) of a scene raster at `scale`, as RGBA.
inline std::array<std::uint8_t, 4> scene_pixel(const std::uint8_t* bytes, std::size_t row_bytes, double scale,
                                               double x, double y) {
    const std::size_t column = static_cast<std::size_t>(std::floor(x * scale));
    const std::size_t row = static_cast<std::size_t>(std::floor(y * scale));
    const std::uint8_t* const pixel = bytes + row * row_bytes + column * 4U;
    // The raster is premultiplied BGRA8.
    const std::array<std::uint8_t, 4> rgba{pixel[2], pixel[1], pixel[0], pixel[3]};
    return rgba;
}

// The overlap case composites as one image: where blue covers red inside
// the group, the result is blue over the plane at half opacity, exactly as
// where blue is alone; red shows nowhere under it.
inline bool overlap_is_one_image(const std::uint8_t* bytes, std::size_t row_bytes, double scale) {
    const std::array<std::uint8_t, 4> both = scene_pixel(bytes, row_bytes, scale, 40.5, 18.5);
    const std::array<std::uint8_t, 4> blue = scene_pixel(bytes, row_bytes, scale, 60.5, 18.5);
    const std::array<std::uint8_t, 4> red = scene_pixel(bytes, row_bytes, scale, 20.5, 18.5);
    const bool one_image = both == blue && both != red;
    return one_image;
}

// The copy group (stage T5): over the opaque background, a copy group on
// copy_bounds holding one translucent fill on copy_fill. Within the bounds
// the group's image replaces the background, so that where it drew nothing
// the colour is clear; outside them the background is untouched.
inline constexpr Rect copy_bounds{10.0, 6.0, 60.0, 30.0};
inline constexpr Rect copy_fill{20.0, 10.0, 20.0, 12.0};
inline constexpr Color copy_tint = Color::rgba(200, 100, 40, 128);

inline void paint_copy_scene(Painter& painter) {
    paint_background(painter);
    PaintGroup group{};
    group.bounds = copy_bounds;
    group.composite = PaintGroupComposite::copy;
    painter.begin_group(group);
    painter.fill_rect(copy_fill, copy_tint);
    painter.end_group();
}

// Whether a channel is within one step of what it should be.
inline bool near_channel(std::uint8_t observed, unsigned wanted) {
    const unsigned value = observed;
    const bool within = value + 1U >= wanted && value <= wanted + 1U;
    return within;
}

// What paint_copy_scene must show at `scale`. `keeps_alpha` says whether
// the target holds alpha (Skia's raster) or is opaque (the Windows DIB
// before stage T5's C2), where alpha is 255 and the colour is the
// premultiplied one. The tint premultiplied is (100, 50, 20) at alpha 128.
inline bool copy_scene_holds(const std::uint8_t* bytes, std::size_t row_bytes, double scale,
                             bool keeps_alpha) {
    const unsigned clear_alpha = keeps_alpha ? 0U : 255U;
    const unsigned tint_alpha = keeps_alpha ? 128U : 255U;
    const std::array<std::uint8_t, 4> tinted = scene_pixel(bytes, row_bytes, scale, 30.5, 16.5);
    const bool tint_copied = near_channel(tinted[0], 100U) && near_channel(tinted[1], 50U) &&
                             near_channel(tinted[2], 20U) && near_channel(tinted[3], tint_alpha);
    const std::array<std::uint8_t, 4> cleared = scene_pixel(bytes, row_bytes, scale, 60.5, 30.5);
    const std::array<std::uint8_t, 4> clear{0U, 0U, 0U, static_cast<std::uint8_t>(clear_alpha)};
    const bool rest_cleared = cleared == clear;
    const std::array<std::uint8_t, 4> outside = scene_pixel(bytes, row_bytes, scale, 90.5, 12.5);
    // The background's colour; its alpha is the painter's own outside a
    // group on a target that keeps none.
    const bool outside_kept = outside[0] == 222U && outside[1] == 232U && outside[2] == 246U &&
                              (!keeps_alpha || outside[3] == 255U);
    const bool holds = tint_copied && rest_cleared && outside_kept;
    return holds;
}

} // namespace gui_forms::part_composite_identity
