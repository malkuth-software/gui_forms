#include "gui_forms/types.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"
#include "../../theme/theme/material_layers.hpp"
#include "painter_support.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
std::unique_ptr<PaintFramebuffer> Painter::create_framebuffer(Size, double) {
    return {};
}
PreparedTextPaintResult Painter::draw_prepared_text(const PreparedTextLayout&, const LayoutAuthority, const Point, const Color) {
    return {};
}
namespace {

double clamped_radius(Rect rect, double radius) noexcept {
    return std::clamp(radius, 0.0,
                      std::max(0.0, std::min(rect.width, rect.height) * 0.5));
}

} // namespace

bool valid_gradient_stops(std::span<const GradientStop> stops) noexcept {
    if (stops.size() < 2U || stops.size() > maximum_gradient_stops ||
        !std::isfinite(stops.front().offset) ||
        !std::isfinite(stops.back().offset) ||
        stops.front().offset != 0.0 || stops.back().offset != 1.0) {
        return false;
    }
    double previous = -1.0;
    for (const GradientStop& stop : stops) {
        if (!std::isfinite(stop.offset) || stop.offset < 0.0 ||
            stop.offset > 1.0 || stop.offset <= previous) {
            return false;
        }
        previous = stop.offset;
    }
    return true;
}

void Painter::clip_rounded_rect(Rect rect, double) {
    clip_rect(rect);
}

void Painter::clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) {
    const double top = std::max(radii.top_left, radii.top_right);
    const double bottom = std::max(radii.bottom_right, radii.bottom_left);
    const double largest = std::max(top, bottom);
    clip_rounded_rect(rect, largest);
}

void Painter::fill_rounded_rect(Rect rect, double radius, Color color) {
    if (rect.empty()) return;
    radius = clamped_radius(rect, radius);
    if (radius <= 0.0) {
        fill_rect(rect, color);
        return;
    }
    fill_rect({rect.x, rect.y + radius, rect.width,
               std::max(0.0, rect.height - radius * 2.0)}, color);
    const unsigned slices = std::max(1U, static_cast<unsigned>(std::ceil(radius)));
    const double step = radius / static_cast<double>(slices);
    for (unsigned slice = 0; slice < slices; ++slice) {
        const double sample_y = (static_cast<double>(slice) + 0.5) * step;
        const double dy = radius - sample_y;
        const double chord = std::sqrt(std::max(0.0, radius * radius - dy * dy));
        const double left = rect.x + radius - chord;
        const double width = std::max(0.0, rect.width - 2.0 * (radius - chord));
        fill_rect({left, rect.y + sample_y - step * 0.5, width, step}, color);
        fill_rect({left, rect.y + rect.height - sample_y - step * 0.5,
                   width, step}, color);
    }
}

void Painter::stroke_rounded_rect(Rect rect, double, Color color, double width) {
    stroke_rect(rect, color, width);
}

void Painter::fill_linear_gradient(Rect rect, Point start, Point end,
                                   std::span<const GradientStop> stops) {
    if (rect.empty() || !valid_gradient_stops(stops)) return;
    constexpr unsigned slices = 48U;
    const bool horizontal = std::abs(end.x - start.x) >= std::abs(end.y - start.y);
    for (unsigned slice = 0; slice < slices; ++slice) {
        const double first = static_cast<double>(slice) / slices;
        const double next = static_cast<double>(slice + 1U) / slices;
        const Color color = detail::sample_gradient(stops, (first + next) * 0.5);
        if (horizontal) {
            fill_rect({rect.x + rect.width * first, rect.y,
                       rect.width * (next - first), rect.height}, color);
        } else {
            fill_rect({rect.x, rect.y + rect.height * first, rect.width,
                       rect.height * (next - first)}, color);
        }
    }
}

void Painter::fill_linear_gradient_spread(
    Rect rect, Point start, Point end, std::span<const GradientStop> stops,
    GradientSpreadMode spread) {
    // Minimal painters remain source-compatible and coherent. Production and
    // recording painters override this operation so repeat/reflect survive the
    // retained display list exactly.
    static_cast<void>(spread);
    fill_linear_gradient(rect, start, end, stops);
}

void Painter::fill_radial_gradient(Rect rect, Point center, Size radii,
                                   std::span<const GradientStop> stops) {
    if (rect.empty() || radii.width <= 0.0 || radii.height <= 0.0 ||
        !valid_gradient_stops(stops)) {
        return;
    }
    // The approximation may be centred outside the target rectangle. Keep
    // the fallback inside the same authored bounds as a native shader.
    save();
    clip_rect(rect);
    constexpr unsigned rings = 40U;
    for (unsigned ring = rings; ring > 0U; --ring) {
        const double amount = static_cast<double>(ring) / rings;
        const double width = std::min(rect.width, radii.width * 2.0 * amount);
        const double height = std::min(rect.height, radii.height * 2.0 * amount);
        fill_rounded_rect(
            {center.x - width * 0.5, center.y - height * 0.5, width, height},
            std::min(width, height) * 0.5,
            detail::sample_gradient(stops, amount));
    }
    restore();
}

void Painter::draw_box_shadow(Rect rect, double corner_radius, Point offset,
                              double blur_radius, double spread, Color color) {
    if (rect.empty() || color.alpha == 0U || !std::isfinite(blur_radius) ||
        !std::isfinite(spread) || blur_radius < 0.0) {
        return;
    }
    constexpr unsigned rings = 12U;
    const double extent = std::max(0.0, spread) + blur_radius * 1.5;
    for (unsigned ring = rings; ring > 0U; --ring) {
        const double amount = static_cast<double>(ring) / rings;
        const double expansion = spread + extent * amount;
        Color ring_color = color;
        ring_color.alpha = static_cast<std::uint8_t>(std::lround(
            static_cast<double>(color.alpha) * (1.0 - amount) /
            static_cast<double>(rings)));
        stroke_rounded_rect(
            {rect.x + offset.x - expansion, rect.y + offset.y - expansion,
             rect.width + expansion * 2.0, rect.height + expansion * 2.0},
            std::max(0.0, corner_radius + expansion), ring_color,
            std::max(0.5, extent / rings));
    }
}

void Painter::draw_inset_box_shadow(Rect rect, double corner_radius,
                                    Point offset, double blur_radius,
                                    double spread, Color color) {
    if (rect.empty() || color.alpha == 0U || !std::isfinite(blur_radius) ||
        !std::isfinite(spread) || blur_radius < 0.0) {
        return;
    }
    save();
    clip_rounded_rect(rect, corner_radius);
    constexpr unsigned rings = 16U;
    const double reach = std::max(1.0, blur_radius * 3.0 + 1.0);
    const Rect hole{rect.x + offset.x + spread,
                    rect.y + offset.y + spread,
                    std::max(0.0, rect.width - spread * 2.0),
                    std::max(0.0, rect.height - spread * 2.0)};
    for (unsigned ring = 0U; ring < rings; ++ring) {
        const double amount = static_cast<double>(ring) /
                              static_cast<double>(rings - 1U);
        Color ring_color = color;
        ring_color.alpha = static_cast<std::uint8_t>(std::lround(
            static_cast<double>(color.alpha) * (1.0 - amount) /
            static_cast<double>(rings)));
        const double inset = amount * reach;
        const Rect stroke{hole.x + inset, hole.y + inset,
                          std::max(0.0, hole.width - inset * 2.0),
                          std::max(0.0, hole.height - inset * 2.0)};
        stroke_rounded_rect(
            stroke, std::max(0.0, corner_radius - spread - inset),
            ring_color, std::max(1.0, reach * 2.0 / rings));
    }
    restore();
}

void Painter::draw_image_region(ImageId image, Rect source, Rect destination,
                                double opacity) {
    if (image.value == 0U || source.empty() || destination.empty() ||
        !source.finite() || !destination.finite() ||
        !std::isfinite(opacity) || opacity <= 0.0) {
        return;
    }
    draw_image(image, destination, std::clamp(opacity, 0.0, 1.0));
}

void Painter::draw_retained_box_shadow(
    const Rect rect, const double corner_radius, const Point offset, const double blur_radius,
    const double spread, const Color color, const std::shared_ptr<RetainedDrawCache>&) {
    draw_box_shadow(rect, corner_radius, offset, blur_radius, spread, color);
}

void Painter::draw_retained_text_utf8(
    const Point origin, const std::string_view text, const FontSpec font,
    const Color color, const std::shared_ptr<RetainedDrawCache>&) {
    draw_text_utf8(origin, text, font, color);
}

void Painter::draw_turned_text_utf8(const Point origin, const std::string_view text, const FontSpec font,
                                    const Color color, const TextOrientation) {
    draw_text_utf8(origin, text, font, color);
}

void Painter::draw_live_surface(std::shared_ptr<LiveSurface>, Rect, double) {
    // Platform-neutral and deliberately inert. Terminal CPU renderers consume
    // the retained surface resource directly.
}

Rect Painter::opaque_live_surface_cover(const std::shared_ptr<LiveSurface>&, Rect, double) {
    // A painter that draws nothing, or cannot promise opaque coverage, hides
    // nothing.
    const Rect nothing{};
    return nothing;
}

void Painter::draw_part_raster(const std::shared_ptr<const PartRaster>&, Point, double) {
    // Deliberately inert: only painters that own device pixels composite
    // part rasters, through the shared blend.
}

void Painter::draw_part_raster_scaled(const std::shared_ptr<const PartRaster>&, Rect, double) {
    // Deliberately inert, as draw_part_raster.
}

void Painter::draw_surface_material(Rect bounds, const SurfaceMaterial& material) {
    detail::paint_material_layers(*this, bounds, material);
}

void Painter::draw_baked_surface_material(Rect bounds, const std::shared_ptr<const SurfaceMaterial>& material,
                                          const std::shared_ptr<const PartRaster>&, double) {
    if (material) draw_surface_material(bounds, *material);
}

void Painter::begin_group(const PaintGroup& group) {
    // A minimal painter cannot isolate: the group's commands draw directly,
    // limited to its bounds, and a group at opacity 0 (or NaN) to nothing.
    save();
    clip_rect(group.bounds);
    if (!(group.opacity > 0.0)) clip_rect(Rect{});
}

void Painter::end_group() {
    restore();
}

void Painter::draw_image_region_sampled(
    ImageId image, Rect source, Rect destination, ImageSampling,
    double opacity) {
    draw_image_region(image, source, destination, opacity);
}

void Painter::fill_image_pattern(ImageId image, Size source_pixel_size,
                                 Rect destination, Size logical_tile_size,
                                 ImagePatternWrap wrap, double opacity) {
    const bool known_wrap = wrap == ImagePatternWrap::tile;
    if (!known_wrap || image.value == 0U || destination.empty() ||
        !destination.finite() || !std::isfinite(source_pixel_size.width) ||
        !std::isfinite(source_pixel_size.height) ||
        !std::isfinite(logical_tile_size.width) ||
        !std::isfinite(logical_tile_size.height) ||
        source_pixel_size.width <= 0.0 || source_pixel_size.height <= 0.0 ||
        logical_tile_size.width <= 0.0 || logical_tile_size.height <= 0.0 ||
        !std::isfinite(opacity) || opacity <= 0.0) {
        return;
    }
    constexpr std::size_t maximum_fallback_tiles = 4096U;
    const std::size_t columns = static_cast<std::size_t>(
        std::ceil(destination.width / logical_tile_size.width));
    const std::size_t rows = static_cast<std::size_t>(
        std::ceil(destination.height / logical_tile_size.height));
    if (columns == 0U || rows == 0U ||
        columns > maximum_fallback_tiles / rows) {
        draw_image_region(image,
            {0.0, 0.0, source_pixel_size.width, source_pixel_size.height},
            destination, opacity);
        return;
    }
    for (std::size_t row = 0; row < rows; ++row) {
        const double y = destination.y +
            static_cast<double>(row) * logical_tile_size.height;
        const double height = std::min(logical_tile_size.height,
                                       destination.bottom() - y);
        const double source_height = std::min(
            source_pixel_size.height,
            height * source_pixel_size.height / logical_tile_size.height);
        for (std::size_t column = 0; column < columns; ++column) {
            const double x = destination.x +
                static_cast<double>(column) * logical_tile_size.width;
            const double width = std::min(logical_tile_size.width,
                                          destination.right() - x);
            const double source_width = std::min(
                source_pixel_size.width,
                width * source_pixel_size.width / logical_tile_size.width);
            draw_image_region(image,
                {0.0, 0.0, source_width, source_height},
                {x, y, width, height}, opacity);
        }
    }
}

} // namespace gui_forms
