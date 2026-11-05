#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/display.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"

namespace gui_forms::detail {
struct PreparedTextStorage;

enum class DisplayOperation : std::uint8_t {
    save = 0,
    restore = 1,
    translate = 2,
    clip_rect = 3,
    fill_rect = 4,
    stroke_rect = 5,
    draw_line = 6,
    draw_text_utf8 = 7,
    draw_image = 8,
    clip_rounded_rect = 9,
    fill_rounded_rect = 10,
    stroke_rounded_rect = 11,
    fill_linear_gradient = 12,
    fill_radial_gradient = 13,
    draw_box_shadow = 14,
    fill_linear_gradient_spread = 15,
    draw_image_region = 16,
    fill_image_pattern = 17,
    draw_image_region_sampled = 18,
    draw_live_surface = 19,
    draw_inset_box_shadow = 20,
    draw_prepared_text = 21,
    draw_part_raster = 22,
    // A paint group (Painter::begin_group): `rect` its bounds, `scalar` its
    // opacity, `secondary_scalar` its composite mode's value,
    // `tertiary_scalar` 1 when it is isolated at any opacity.
    begin_group = 23,
    end_group = 24,
    // A part raster shown scaled (Painter::draw_part_raster_scaled): `rect`
    // its logical destination, `scalar` its opacity.
    draw_part_raster_scaled = 25,
    // A per-corner rounded clip: `rect`, with the top-left and top-right
    // radii in `first` (x, y) and the bottom-right and bottom-left in
    // `second`.
    clip_rounded_rect_corners = 26,
};

struct DisplayCommand final {
    DisplayOperation operation{};
    Point first{};
    Point second{};
    Rect rect{};
    Color color{};
    FontSpec font{};
    ImageId image{};
    std::shared_ptr<LiveSurface> live_surface;
    double scalar{};
    double secondary_scalar{};
    double tertiary_scalar{};
    std::vector<GradientStop> gradient_stops;
    GradientSpreadMode gradient_spread{GradientSpreadMode::pad};
    ImagePatternWrap image_pattern_wrap{ImagePatternWrap::tile};
    ImageSampling image_sampling{ImageSampling::linear};
    std::string text;
    // The reading direction of draw_text_utf8. A turned run replays through
    // Painter::draw_turned_text_utf8, without the retained cache.
    TextOrientation text_orientation{TextOrientation::horizontal};
    // Renderer-derived data shared by every replay copy of this recording.
    std::shared_ptr<RetainedDrawCache> retained_cache{};
    // The raster of draw_part_raster, shared read-only with its maker. The
    // command keeps it alive for as long as the recording lives. `first` is
    // its logical origin, `rect` its logical bounds at its own scale (for
    // inspection), and `scalar` its opacity.
    std::shared_ptr<const PartRaster> part_raster{};
    std::shared_ptr<const PreparedTextStorage> prepared_text{};
    LayoutAuthority prepared_authority{};
};

} // namespace gui_forms::detail
