#include "gui_forms/types.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"
#include "../../theme/theme/material_layers.hpp"
#include "painter_support.hpp"
#include "text_turn.hpp"

#include <algorithm>
#include <array>
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

bool focus_inside_ellipse(Point center, Size radii, Point focus) noexcept {
    if (!(radii.width > 0.0) || !(radii.height > 0.0)) return false;
    const double across = (focus.x - center.x) / radii.width;
    const double down = (focus.y - center.y) / radii.height;
    const double reach = across * across + down * down;
    return std::isfinite(reach) && reach < 1.0;
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

void Painter::fill_radial_gradient_spread(Rect rect, Point center, Size radii,
                                          std::span<const GradientStop> stops,
                                          GradientSpreadMode spread) {
    // As fill_linear_gradient_spread's: a minimal painter pads. A spread
    // outside the enumeration draws nothing, as the production painters'.
    const bool known_spread = spread == GradientSpreadMode::pad || spread == GradientSpreadMode::repeat ||
                              spread == GradientSpreadMode::reflect;
    if (!known_spread) return;
    fill_radial_gradient(rect, center, radii, stops);
}

void Painter::fill_focal_radial_gradient(Rect rect, Point center, Size radii, Point focus,
                                         std::span<const GradientStop> stops, GradientSpreadMode spread) {
    const bool known_spread = spread == GradientSpreadMode::pad || spread == GradientSpreadMode::repeat ||
                              spread == GradientSpreadMode::reflect;
    if (!known_spread || rect.empty() || !(radii.width > 0.0) || !(radii.height > 0.0) ||
        !std::isfinite(radii.width) || !std::isfinite(radii.height) || !std::isfinite(center.x) ||
        !std::isfinite(center.y) || !valid_gradient_stops(stops) ||
        !focus_inside_ellipse(center, radii, focus)) {
        return;
    }
    // As fill_radial_gradient's rings, each on its own centre: the ring at
    // offset t is centred t of the way from the focus to the centre. The
    // rectangle first takes the last stop, the padded colour outside.
    save();
    clip_rect(rect);
    fill_rect(rect, stops.back().color);
    constexpr unsigned rings = 40U;
    for (unsigned ring = rings; ring > 0U; --ring) {
        const double amount = static_cast<double>(ring) / rings;
        const double width = radii.width * 2.0 * amount;
        const double height = radii.height * 2.0 * amount;
        const Point ring_centre{focus.x + (center.x - focus.x) * amount, focus.y + (center.y - focus.y) * amount};
        fill_rounded_rect({ring_centre.x - width * 0.5, ring_centre.y - height * 0.5, width, height},
                          std::min(width, height) * 0.5, detail::sample_gradient(stops, amount));
    }
    restore();
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

void Painter::draw_glyph_runs(const Point origin, const GlyphRunsView& view, const FontSpec font,
                              const Color color) {
    draw_text_utf8(origin, view.text, font, color);
}

void Painter::draw_rotated_text_utf8(const Point anchor, const std::string_view text, const FontSpec font,
                                     const Color color, const double degrees) {
    if (!std::isfinite(degrees)) return;
    draw_text_utf8(anchor, text, font, color);
}

void Painter::draw_rotated_glyph_runs(const Point anchor, const GlyphRunsView& view, const FontSpec font,
                                      const Color color, const double degrees) {
    draw_rotated_text_utf8(anchor, view.text, font, color, degrees);
}

namespace {

// The turn's unit vectors: along the baseline, and across it towards the
// descent (y grows down the surface, so anticlockwise as seen is -sin).
struct TextTurn final {
    double along_x{};
    double along_y{};
    double across_x{};
    double across_y{};
};

// STXlib's turn (detail::text_turn_entries), the one turned text is drawn
// through, so the bounds and anchor agree with the drawn run.
[[nodiscard]] TextTurn text_turn(const double degrees) noexcept {
    const detail::TextTurnEntries angle = detail::text_turn_entries(degrees);
    const double cosine = angle.cosine;
    const double sine = angle.sine;
    const TextTurn turn{cosine, -sine, sine, cosine};
    return turn;
}

[[nodiscard]] bool usable_box(const RotatedTextBox& box) noexcept {
    const bool finite = std::isfinite(box.width) && std::isfinite(box.ascent) && std::isfinite(box.descent) &&
                        std::isfinite(box.degrees);
    const bool usable = finite && box.width >= 0.0 && box.ascent >= 0.0 && box.descent >= 0.0;
    return usable;
}

} // namespace

Rect rotated_text_bounds(const Point anchor, const RotatedTextBox& box) noexcept {
    const Rect empty{anchor.x, anchor.y, 0.0, 0.0};
    if (!usable_box(box) || !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return empty;
    const TextTurn turn = text_turn(box.degrees);
    const std::array<double, 2> along{0.0, box.width};
    const std::array<double, 2> across{-box.ascent, box.descent};
    double left = anchor.x;
    double top = anchor.y;
    double right = anchor.x;
    double bottom = anchor.y;
    for (const double s : along) {
        for (const double t : across) {
            const double x = anchor.x + s * turn.along_x + t * turn.across_x;
            const double y = anchor.y + s * turn.along_y + t * turn.across_y;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    const Rect bounds{left, top, right - left, bottom - top};
    return bounds;
}

Point rotated_text_anchor(const Point centre, const RotatedTextBox& box) noexcept {
    if (!usable_box(box) || !std::isfinite(centre.x) || !std::isfinite(centre.y)) return centre;
    const TextTurn turn = text_turn(box.degrees);
    // The box's middle in run coordinates: half the advance along, and
    // between the ascent and descent across.
    const double along = box.width * 0.5;
    const double across = (box.descent - box.ascent) * 0.5;
    const Point anchor{centre.x - along * turn.along_x - across * turn.across_x,
                       centre.y - along * turn.along_y - across * turn.across_y};
    return anchor;
}

void Painter::draw_turned_text_utf8(const Point origin, const std::string_view text, const FontSpec font,
                                    const Color color, const TextOrientation) {
    draw_text_utf8(origin, text, font, color);
}

void Painter::draw_text_runs(const FontSpec font, const std::span<const TextRun> runs) {
    for (const TextRun& run : runs) draw_text_utf8(run.origin, run.text, font, run.color);
}

namespace {

// Whether byte `index` of `text` starts a code point (or is its end).
bool code_point_boundary(const std::string_view text, const std::size_t index) noexcept {
    if (index >= text.size()) return true;
    const unsigned char byte = static_cast<unsigned char>(text[index]);
    const bool result = (byte & 0xC0U) != 0x80U;
    return result;
}

constexpr std::string_view horizontal_ellipsis{"\xE2\x80\xA6"};

} // namespace

std::size_t Painter::fit_text_utf8(const std::string_view text, const FontSpec font, const double max_width) {
    if (!std::isfinite(max_width) || max_width < 0.0 || text.empty()) return 0U;
    std::size_t low = 0U;
    std::size_t high = text.size();
    while (low < high) {
        std::size_t middle = low + (high - low + 1U) / 2U;
        while (middle > low && !code_point_boundary(text, middle)) --middle;
        if (middle == low) break;
        const Size measured = measure_text_utf8(text.substr(0U, middle), font);
        if (measured.width <= max_width) {
            low = middle;
        } else {
            high = middle - 1U;
            while (high > low && !code_point_boundary(text, high)) --high;
        }
    }
    return low;
}

double Painter::draw_text_utf8_fitted(const Point origin, const std::string_view text, const FontSpec font,
                                      const Color color, const double max_width, const TextTrim trim) {
    if (text.empty() || !std::isfinite(max_width) || max_width <= 0.0) return 0.0;
    const Size whole = measure_text_utf8(text, font);
    if (whole.width <= max_width) {
        draw_text_utf8(origin, text, font, color);
        return whole.width;
    }
    if (trim == TextTrim::cut) {
        const std::size_t kept = fit_text_utf8(text, font, max_width);
        if (kept == 0U) return 0.0;
        const std::string_view prefix = text.substr(0U, kept);
        draw_text_utf8(origin, prefix, font, color);
        const double width = measure_text_utf8(prefix, font).width;
        return width;
    }
    const double ellipsis_width = measure_text_utf8(horizontal_ellipsis, font).width;
    if (ellipsis_width > max_width) return 0.0;
    std::size_t kept = fit_text_utf8(text, font, max_width - ellipsis_width);
    while (kept > 0U && (text[kept - 1U] == ' ' || text[kept - 1U] == '\t')) --kept;
    double prefix_width = 0.0;
    if (kept > 0U) {
        const std::string_view prefix = text.substr(0U, kept);
        draw_text_utf8(origin, prefix, font, color);
        prefix_width = measure_text_utf8(prefix, font).width;
    }
    draw_text_utf8(Point{origin.x + prefix_width, origin.y}, horizontal_ellipsis, font, color);
    const double drawn = prefix_width + ellipsis_width;
    return drawn;
}

void Painter::draw_live_surface(std::shared_ptr<LiveSurface>, Rect, double) {
    // Platform-neutral and deliberately inert. Terminal CPU renderers consume
    // the retained surface resource directly.
}

ImageDrawOpacity Painter::image_draw_opacity(ImageId) const noexcept {
    return ImageDrawOpacity{};
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

void Painter::begin_transform(const PaintTransformGroup& group) {
    save();
    if (!valid_paint_transform(group.transform) || !paint_transform_is_translation(group.transform)) {
        // A minimal painter cannot turn or scale: the group draws nothing.
        clip_rect(Rect{});
        return;
    }
    translate(Point{group.transform.dx, group.transform.dy});
    clip_rect(group.bounds);
}

void Painter::end_transform() {
    restore();
}

bool valid_paint_transform(const PaintTransform& transform) noexcept {
    const bool finite = std::isfinite(transform.xx) && std::isfinite(transform.yx) && std::isfinite(transform.xy) &&
                        std::isfinite(transform.yy) && std::isfinite(transform.dx) && std::isfinite(transform.dy);
    if (!finite) return false;
    const double determinant = transform.xx * transform.yy - transform.xy * transform.yx;
    const bool usable = std::isfinite(determinant) && std::fabs(determinant) >= minimum_paint_transform_determinant;
    return usable;
}

bool paint_transform_is_translation(const PaintTransform& transform) noexcept {
    const bool translation = transform.xx == 1.0 && transform.yy == 1.0 && transform.xy == 0.0 && transform.yx == 0.0;
    return translation;
}

Point transform_paint_point(const PaintTransform& transform, const Point point) noexcept {
    const double x = transform.xx * point.x + transform.xy * point.y + transform.dx;
    const double y = transform.yx * point.x + transform.yy * point.y + transform.dy;
    return Point{x, y};
}

Rect transformed_paint_bounds(const PaintTransform& transform, const Rect rect) noexcept {
    if (!valid_paint_transform(transform) || rect.empty() || !rect.finite()) return Rect{};
    const std::array<Point, 4> corners{{Point{rect.x, rect.y}, Point{rect.x + rect.width, rect.y},
                                        Point{rect.x, rect.y + rect.height},
                                        Point{rect.x + rect.width, rect.y + rect.height}}};
    const Point first = transform_paint_point(transform, corners[0]);
    double left = first.x;
    double top = first.y;
    double right = first.x;
    double bottom = first.y;
    for (std::size_t index = 1U; index < corners.size(); ++index) {
        const Point mapped = transform_paint_point(transform, corners[index]);
        left = std::min(left, mapped.x);
        top = std::min(top, mapped.y);
        right = std::max(right, mapped.x);
        bottom = std::max(bottom, mapped.y);
    }
    const Rect bounds{left, top, right - left, bottom - top};
    if (!bounds.finite()) return Rect{};
    return bounds;
}

PaintTransform paint_rotation_about(const Point centre, const double degrees) noexcept {
    if (!std::isfinite(degrees) || !std::isfinite(centre.x) || !std::isfinite(centre.y)) return PaintTransform{};
    // The engine's deterministic sine and cosine: the same bits on every
    // host, and quarter turns exact, so a turned rectangle keeps whole-pixel
    // edges.
    const EngineSineCosine turn = engine_sine_cosine_degrees(degrees);
    const double cosine = turn.cosine;
    const double sine = turn.sine;
    PaintTransform rotation{};
    rotation.xx = cosine;
    rotation.xy = sine;
    rotation.yx = -sine;
    rotation.yy = cosine;
    rotation.dx = centre.x - (cosine * centre.x + sine * centre.y);
    rotation.dy = centre.y - (-sine * centre.x + cosine * centre.y);
    return rotation;
}

void Painter::end_group() {
    restore();
}

void Painter::draw_image_region_sampled(
    ImageId image, Rect source, Rect destination, ImageSampling,
    double opacity) {
    draw_image_region(image, source, destination, opacity);
}

namespace {

// fill_image_pattern's clamp fallback: the first tile, clipped to
// `destination`, then its last source column stretched over the room to its
// right, its last row over the room below, and its corner pixel over the
// rest, each sampled nearest so the extended pixels keep the edge's
// colours. Inputs validated by the caller.
void fill_clamped_pattern(Painter& painter, ImageId image, Size source_pixel_size, Rect destination,
                          Size logical_tile_size, double opacity) {
    const double tile_width = std::min(logical_tile_size.width, destination.width);
    const double tile_height = std::min(logical_tile_size.height, destination.height);
    const double source_width = tile_width * source_pixel_size.width / logical_tile_size.width;
    const double source_height = tile_height * source_pixel_size.height / logical_tile_size.height;
    painter.draw_image_region(image, Rect{0.0, 0.0, source_width, source_height},
                              Rect{destination.x, destination.y, tile_width, tile_height}, opacity);
    const double right_width = destination.width - tile_width;
    const double below_height = destination.height - tile_height;
    const double last_column = source_pixel_size.width - 1.0;
    const double last_row = source_pixel_size.height - 1.0;
    if (right_width > 0.0) {
        painter.draw_image_region_sampled(image, Rect{last_column, 0.0, 1.0, source_height},
                                          Rect{destination.x + tile_width, destination.y, right_width, tile_height},
                                          ImageSampling::nearest, opacity);
    }
    if (below_height > 0.0) {
        painter.draw_image_region_sampled(image, Rect{0.0, last_row, source_width, 1.0},
                                          Rect{destination.x, destination.y + tile_height, tile_width, below_height},
                                          ImageSampling::nearest, opacity);
    }
    if (right_width > 0.0 && below_height > 0.0) {
        painter.draw_image_region_sampled(image, Rect{last_column, last_row, 1.0, 1.0},
                                          Rect{destination.x + tile_width, destination.y + tile_height, right_width,
                                               below_height},
                                          ImageSampling::nearest, opacity);
    }
}

} // namespace

void Painter::fill_image_pattern(ImageId image, Size source_pixel_size,
                                 Rect destination, Size logical_tile_size,
                                 ImagePatternWrap wrap, double opacity) {
    const bool known_wrap = wrap == ImagePatternWrap::tile || wrap == ImagePatternWrap::mirror ||
                            wrap == ImagePatternWrap::clamp;
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
    if (wrap == ImagePatternWrap::clamp) {
        fill_clamped_pattern(*this, image, source_pixel_size, destination, logical_tile_size, opacity);
        return;
    }
    // `mirror` draws as `tile` here (painter.hpp): there is no flip.
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
