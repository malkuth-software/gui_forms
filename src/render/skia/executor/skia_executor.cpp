#include "skia_executor.hpp"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkColorMatrixFilter.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"

#include "../../../core/image/png_pixels.hpp"
#include "../../../core/types/paint_marks/grid_lines.hpp"
#include "../../../core/image/png_writer.hpp"
#include "../../../core/text/composite/text_composite.hpp"
#include "../../text/glyph_run/glyph_run_raster.hpp"
#include "../../text/harfbuzz/harfbuzz_font_engine.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <array>
#include <optional>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_drawing::render {
namespace {

[[nodiscard]] double pixel_font_size(const FontSnapshot& font) noexcept {
    constexpr double dpi = 96.0;
    switch (font.unit) {
    case GraphicsUnit::display: return font.size * dpi / 75.0;
    case GraphicsUnit::point: return font.size * dpi / 72.0;
    case GraphicsUnit::inch: return font.size * dpi;
    case GraphicsUnit::document: return font.size * dpi / 300.0;
    case GraphicsUnit::millimeter: return font.size * dpi / 25.4;
    case GraphicsUnit::world:
    case GraphicsUnit::pixel: return font.size;
    }
    return font.size;
}

[[nodiscard]] std::string ascii_lower(std::string_view value) {
    std::string result(value);
    for (char& entry : result) {
        if (entry >= 'A' && entry <= 'Z') {
            entry = static_cast<char>(entry + ('a' - 'A'));
        }
    }
    return result;
}

[[nodiscard]] SkColor to_sk_color(Color color) noexcept {
    return color.is_empty() ? SK_ColorTRANSPARENT :
        SkColorSetARGB(color.alpha(), color.red(), color.green(), color.blue());
}

[[nodiscard]] std::uint8_t premultiply(std::uint8_t channel,
                                       std::uint8_t alpha) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(channel) * alpha + 127U) / 255U);
}

[[nodiscard]] std::uint8_t unpremultiply(std::uint8_t channel,
                                         std::uint8_t alpha) noexcept {
    if (alpha == 0U) return 0U;
    return static_cast<std::uint8_t>(std::min(
        255U, (static_cast<unsigned>(channel) * 255U + alpha / 2U) / alpha));
}

void assign_raster_error(RasterError* output, RasterError value) noexcept {
    if (output != nullptr) *output = value;
}

template <std::size_t Count>
void mark_hatch_pixel(std::array<SkColor, Count>& pixels,
                      int extent, int x, int y, SkColor color) noexcept {
    pixels[static_cast<std::size_t>(y * extent + x)] = color;
}

[[nodiscard]] std::vector<std::byte> remapped_pixels(
    const ImageSnapshot& image, const ImageAttributesSnapshot& attributes) {
    std::vector<std::byte> result(image.pixels().begin(), image.pixels().end());
    if (attributes.remap_table.empty()) return result;
    for (std::size_t offset = 0; offset + 3U < result.size(); offset += 4U) {
        // The image is BGRA8.
        const std::uint8_t blue = std::to_integer<std::uint8_t>(result[offset]);
        const std::uint8_t green = std::to_integer<std::uint8_t>(result[offset + 1U]);
        const std::uint8_t red = std::to_integer<std::uint8_t>(result[offset + 2U]);
        const std::uint8_t alpha = std::to_integer<std::uint8_t>(result[offset + 3U]);
        const Color source = Color::from_argb(
            alpha, unpremultiply(red, alpha), unpremultiply(green, alpha),
            unpremultiply(blue, alpha));
        std::vector<ImageAttributesSnapshot::ColorRemap>::const_iterator found =
            attributes.remap_table.begin();
        while (found != attributes.remap_table.end() &&
               (*found).old_color.argb() != source.argb()) {
            ++found;
        }
        if (found == attributes.remap_table.end()) continue;
        const Color replacement = (*found).new_color;
        const std::uint8_t out_alpha = replacement.alpha();
        const std::uint8_t out_red = premultiply(replacement.red(), out_alpha);
        const std::uint8_t out_green = premultiply(replacement.green(), out_alpha);
        const std::uint8_t out_blue = premultiply(replacement.blue(), out_alpha);
        result[offset] = static_cast<std::byte>(out_blue);
        result[offset + 1U] = static_cast<std::byte>(out_green);
        result[offset + 2U] = static_cast<std::byte>(out_red);
        result[offset + 3U] = static_cast<std::byte>(out_alpha);
    }
    return result;
}

[[nodiscard]] SkRect to_sk_rect(RectF rectangle) noexcept {
    return SkRect::MakeXYWH(static_cast<SkScalar>(rectangle.x),
                            static_cast<SkScalar>(rectangle.y),
                            static_cast<SkScalar>(rectangle.width),
                            static_cast<SkScalar>(rectangle.height));
}

// Bitmaps and images are premultiplied BGRA8 in sRGB.
[[nodiscard]] SkImageInfo image_info(std::uint32_t width, std::uint32_t height) {
    return SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height), kBGRA_8888_SkColorType,
        kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
}

[[nodiscard]] SkMatrix to_sk_matrix(const Matrix& matrix) {
    SkMatrix result;
    result.setAll(static_cast<SkScalar>(matrix.m11()),
                  static_cast<SkScalar>(matrix.m21()),
                  static_cast<SkScalar>(matrix.dx()),
                  static_cast<SkScalar>(matrix.m12()),
                  static_cast<SkScalar>(matrix.m22()),
                  static_cast<SkScalar>(matrix.dy()),
                  0.0F, 0.0F, 1.0F);
    return result;
}

// The path, or nullopt when an element has no PathVerb; the command then fails.
[[nodiscard]] std::optional<SkPath> to_sk_path(const PathSnapshot& source) {
    SkPathBuilder builder(source.fill_mode == FillMode::alternate ?
        SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
    bool has_current_point = false;
    PointF current_point{};
    for (const PathElement& element : source.elements) {
        switch (element.verb) {
        case PathVerb::start_figure:
            has_current_point = false;
            break;
        case PathVerb::line:
            if (!has_current_point || current_point != element.first) {
                builder.moveTo(static_cast<SkScalar>(element.first.x),
                               static_cast<SkScalar>(element.first.y));
            }
            builder.lineTo(static_cast<SkScalar>(element.second.x),
                           static_cast<SkScalar>(element.second.y));
            current_point = element.second;
            has_current_point = true;
            break;
        case PathVerb::quadratic:
            if (!has_current_point || current_point != element.first) {
                builder.moveTo(static_cast<SkScalar>(element.first.x),
                               static_cast<SkScalar>(element.first.y));
            }
            builder.quadTo(static_cast<SkScalar>(element.second.x),
                           static_cast<SkScalar>(element.second.y),
                           static_cast<SkScalar>(element.third.x),
                           static_cast<SkScalar>(element.third.y));
            current_point = element.third;
            has_current_point = true;
            break;
        case PathVerb::bezier:
            if (!has_current_point || current_point != element.first) {
                builder.moveTo(static_cast<SkScalar>(element.first.x),
                               static_cast<SkScalar>(element.first.y));
            }
            builder.cubicTo(static_cast<SkScalar>(element.second.x),
                            static_cast<SkScalar>(element.second.y),
                            static_cast<SkScalar>(element.third.x),
                            static_cast<SkScalar>(element.third.y),
                            static_cast<SkScalar>(element.fourth.x),
                            static_cast<SkScalar>(element.fourth.y));
            current_point = element.fourth;
            has_current_point = true;
            break;
        case PathVerb::rectangle:
            builder.addRect(to_sk_rect(element.rect));
            has_current_point = false;
            break;
        case PathVerb::ellipse:
            builder.addOval(to_sk_rect(element.rect));
            has_current_point = false;
            break;
        case PathVerb::arc:
            // GDI+ GraphicsPath.AddArc extends the current figure. Skia's
            // addArc starts a fresh contour, which left rounded rectangles as
            // four disconnected corner outlines with an empty centre. arcTo
            // preserves the implicit connecting edges used by WinForms custom
            // controls while still starting a contour for the first arc.
            builder.arcTo(to_sk_rect(element.rect),
                          static_cast<SkScalar>(element.start_angle),
                          static_cast<SkScalar>(element.sweep_angle),
                          !has_current_point);
            {
                constexpr double degrees_to_radians =
                    0.01745329251994329576923690768489;
                const double angle =
                    (element.start_angle + element.sweep_angle) *
                    degrees_to_radians;
                current_point = {
                    element.rect.x + element.rect.width * 0.5 +
                        element.rect.width * 0.5 * std::cos(angle),
                    element.rect.y + element.rect.height * 0.5 +
                        element.rect.height * 0.5 * std::sin(angle),
                };
            }
            has_current_point = true;
            break;
        case PathVerb::close_figure:
            builder.close();
            has_current_point = false;
            break;
        default:
            return std::nullopt;
        }
    }
    return builder.detach();
}

void apply_image_attributes(SkPaint& paint,
                            const ImageAttributesSnapshot& attributes) {
    if (!attributes.color_matrix.has_value()) return;
    const std::array<double, 25>& colors = *attributes.color_matrix;
    float matrix[20]{};
    for (std::size_t output = 0; output < 4U; ++output) {
        for (std::size_t input = 0; input < 4U; ++input) {
            matrix[output * 5U + input] = static_cast<float>(
                colors[input * 5U + output]);
        }
        matrix[output * 5U + 4U] = static_cast<float>(
            colors[20U + output] * 255.0);
    }
    paint.setColorFilter(SkColorFilters::Matrix(matrix));
}

[[nodiscard]] SkSamplingOptions sampling_for(const GraphicsState& state) {
    return state.interpolation == InterpolationMode::nearest ?
        SkSamplingOptions(SkFilterMode::kNearest) :
        SkSamplingOptions(SkFilterMode::kLinear);
}

void configure_paint(SkPaint& paint, const GraphicsState& state) {
    paint.setAntiAlias(state.smoothing != SmoothingMode::none &&
                       state.smoothing != SmoothingMode::high_speed);
    paint.setBlendMode(state.compositing == CompositingMode::source_copy ?
        SkBlendMode::kSrc : SkBlendMode::kSrcOver);
}

// The on/off lengths of a preset dash style for a pen `width` wide; none
// for a solid line or a custom pattern (the pen's own).
std::vector<SkScalar> preset_dash_pattern(DashStyle style, SkScalar width) {
    switch (style) {
    case DashStyle::dash: return {3.0F * width, width};
    case DashStyle::dot: return {width, width};
    case DashStyle::dash_dot: return {3.0F * width, width, width, width};
    case DashStyle::dash_dot_dot: return {3.0F * width, width, width, width, width, width};
    case DashStyle::solid: return {};
    case DashStyle::custom: return {};
    }
    // A style outside the enumeration draws solid.
    return {};
}

[[nodiscard]] SkPaint::Join to_sk_join(LineJoin join) noexcept {
    switch (join) {
    case LineJoin::miter: return SkPaint::kMiter_Join;
    case LineJoin::bevel: return SkPaint::kBevel_Join;
    case LineJoin::round: return SkPaint::kRound_Join;
    }
    return SkPaint::kMiter_Join;
}

[[nodiscard]] SkPaint::Cap to_sk_cap(LineCap cap) noexcept {
    switch (cap) {
    case LineCap::flat: return SkPaint::kButt_Cap;
    case LineCap::square: return SkPaint::kSquare_Cap;
    case LineCap::round: return SkPaint::kRound_Cap;
    }
    return SkPaint::kButt_Cap;
}

// Configures paint to stroke with pen. False when stx_skia refuses the pen's
// dash pattern (an entry that overflows float, for example); the command must
// then fail rather than draw a solid line.
[[nodiscard]] bool configure_pen(SkPaint& paint, const PenSnapshot& pen,
                                 const GraphicsState& state) {
    configure_paint(paint, state);
    paint.setColor(to_sk_color(pen.color));
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(static_cast<SkScalar>(pen.width));
    paint.setStrokeJoin(to_sk_join(pen.join));
    paint.setStrokeMiter(static_cast<SkScalar>(pen.miter_limit));
    paint.setStrokeCap(to_sk_cap(pen.cap));
    std::vector<SkScalar> pattern;
    if (pen.dash_style == DashStyle::custom) {
        pattern.reserve(pen.dash_pattern.size());
        for (const double entry : pen.dash_pattern) {
            pattern.push_back(static_cast<SkScalar>(entry));
        }
    } else {
        pattern = preset_dash_pattern(pen.dash_style, static_cast<SkScalar>(pen.width));
    }
    if (pattern.size() >= 2U && pattern.size() % 2U == 0U) {
        SkDashPathEffectResult made = SkDashPathEffect::Make(
            SkSpan<const SkScalar>(pattern.data(), pattern.size()), 0.0F);
        if (made.rejection != SkDashRejection::kNone) return false;
        paint.setPathEffect(std::move(made.effect));
    }
    return true;
}

[[nodiscard]] SkTileMode tile_mode(WrapMode mode) noexcept {
    switch (mode) {
    case WrapMode::tile: return SkTileMode::kRepeat;
    case WrapMode::tile_flip_x:
    case WrapMode::tile_flip_y:
    case WrapMode::tile_flip_xy: return SkTileMode::kMirror;
    case WrapMode::clamp: return SkTileMode::kClamp;
    }
    return SkTileMode::kClamp;
}

[[nodiscard]] SkTileMode texture_tile_mode_x(WrapMode mode) noexcept {
    return mode == WrapMode::tile_flip_x || mode == WrapMode::tile_flip_xy
        ? SkTileMode::kMirror
        : mode == WrapMode::clamp ? SkTileMode::kClamp : SkTileMode::kRepeat;
}

[[nodiscard]] SkTileMode texture_tile_mode_y(WrapMode mode) noexcept {
    return mode == WrapMode::tile_flip_y || mode == WrapMode::tile_flip_xy
        ? SkTileMode::kMirror
        : mode == WrapMode::clamp ? SkTileMode::kClamp : SkTileMode::kRepeat;
}

// Configures paint to fill with brush. False when stx_skia refuses the brush's
// gradient (its geometry overflows float, for example); paint is then unusable
// and the command must fail rather than fill with the paint's plain color.
[[nodiscard]] bool configure_brush(SkPaint& paint, const BrushSnapshot& brush,
                                   const GraphicsState& state) {
    configure_paint(paint, state);
    paint.setStyle(SkPaint::kFill_Style);
    if (brush.kind == BrushKind::solid) {
        paint.setColor(to_sk_color(brush.primary));
        return true;
    }
    if (brush.kind == BrushKind::hatch) {
        constexpr int extent = 8;
        std::array<SkColor, extent * extent> pixels;
        pixels.fill(to_sk_color(brush.secondary));
        const SkColor hatch_color = to_sk_color(brush.primary);
        for (int coordinate = 0; coordinate < extent; ++coordinate) {
            switch (brush.hatch_style) {
            case HatchStyle::horizontal:
                mark_hatch_pixel(pixels, extent, coordinate, 3, hatch_color);
                break;
            case HatchStyle::vertical:
                mark_hatch_pixel(pixels, extent, 3, coordinate, hatch_color);
                break;
            case HatchStyle::forward_diagonal:
                mark_hatch_pixel(pixels, extent, coordinate,
                                 extent - 1 - coordinate, hatch_color);
                break;
            case HatchStyle::backward_diagonal:
                mark_hatch_pixel(pixels, extent, coordinate, coordinate,
                                 hatch_color);
                break;
            case HatchStyle::cross:
                mark_hatch_pixel(pixels, extent, coordinate, 3, hatch_color);
                mark_hatch_pixel(pixels, extent, 3, coordinate, hatch_color);
                break;
            case HatchStyle::diagonal_cross:
                mark_hatch_pixel(pixels, extent, coordinate, coordinate,
                                 hatch_color);
                mark_hatch_pixel(pixels, extent, coordinate,
                                 extent - 1 - coordinate, hatch_color);
                break;
            default:
                // Not a HatchStyle: the brush is refused and the command fails.
                return false;
            }
        }
        const SkImageInfo info = SkImageInfo::MakeN32Premul(extent, extent);
        sk_sp<SkData> data = SkData::MakeWithCopy(pixels.data(), sizeof(pixels));
        sk_sp<SkImage> image = SkImages::RasterFromData(
            info, std::move(data), extent * sizeof(SkColor));
        if (image) {
            paint.setShader((*image).makeShader(
                SkTileMode::kRepeat, SkTileMode::kRepeat,
                SkSamplingOptions(SkFilterMode::kNearest)));
        }
        return true;
    }
    if (brush.kind == BrushKind::checker) {
        // Two by two cells, scaled to the cell size in the recorder's
        // coordinates, with a `primary` cell's corner at the origin.
        if (!(brush.cell_size > 0.0)) return false;
        const SkPMColor first = SkPreMultiplyColor(to_sk_color(brush.primary));
        const SkPMColor second = SkPreMultiplyColor(to_sk_color(brush.secondary));
        const std::array<SkPMColor, 4> cells{first, second, second, first};
        const SkImageInfo info = SkImageInfo::MakeN32Premul(2, 2);
        sk_sp<SkData> data = SkData::MakeWithCopy(cells.data(), sizeof(cells));
        sk_sp<SkImage> image = SkImages::RasterFromData(info, std::move(data), 2U * sizeof(SkPMColor));
        if (!image) return false;
        const SkScalar cell = static_cast<SkScalar>(brush.cell_size);
        const SkMatrix local = SkMatrix::Scale(cell, cell);
        paint.setShader((*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                            SkSamplingOptions(SkFilterMode::kNearest), local));
        return true;
    }
    if (brush.kind == BrushKind::texture) {
        if (!brush.image.has_pixels()) {
            paint.setColor(SK_ColorTRANSPARENT);
            return true;
        }
        const SkImageInfo info = image_info(brush.image.width, brush.image.height);
        sk_sp<SkData> data = SkData::MakeWithCopy(
            brush.image.pixels().data(), brush.image.pixels().size());
        sk_sp<SkImage> image = SkImages::RasterFromData(
            info, std::move(data), brush.image.row_bytes());
        if (!image) {
            paint.setColor(SK_ColorTRANSPARENT);
            return true;
        }
        const SkMatrix local = to_sk_matrix(brush.transform);
        paint.setShader((*image).makeShader(
            texture_tile_mode_x(brush.wrap_mode),
            texture_tile_mode_y(brush.wrap_mode),
            sampling_for(state), local));
        return true;
    }

    std::vector<SkColor4f> colors;
    std::vector<float> positions;
    colors.reserve(brush.colors.size());
    positions.reserve(brush.positions.size());
    for (const Color color : brush.colors) {
        colors.push_back(SkColor4f::FromColor(to_sk_color(color)));
    }
    for (const double position : brush.positions) {
        positions.push_back(static_cast<SkScalar>(position));
    }
    if (colors.size() < 2U || colors.size() != positions.size()) {
        paint.setColor(to_sk_color(brush.primary));
        return true;
    }
    const SkGradient::Colors gradient_colors(
        SkSpan<const SkColor4f>(colors.data(), colors.size()),
        SkSpan<const float>(positions.data(), positions.size()),
        tile_mode(brush.wrap_mode), SkColorSpace::MakeSRGB());
    const SkGradient gradient(gradient_colors, SkGradient::Interpolation{});
    if (brush.kind == BrushKind::conic_gradient) {
        // One full turn clockwise (y down) from the start angle.
        const double start = std::fmod(brush.angle, 360.0);
        SkGradientShaderResult made = SkShaders::SweepGradient(
            {static_cast<SkScalar>(brush.center.x), static_cast<SkScalar>(brush.center.y)},
            static_cast<SkScalar>(start), static_cast<SkScalar>(start + 360.0), gradient);
        if (made.rejection != SkGradientRejection::kNone) return false;
        paint.setShader(std::move(made.shader));
        return true;
    }
    if (brush.kind == BrushKind::linear_gradient) {
        constexpr double degrees_to_radians = 0.017453292519943295769;
        const double radians = brush.angle * degrees_to_radians;
        const double half_length = std::abs(std::cos(radians)) * brush.bounds.width / 2.0 +
            std::abs(std::sin(radians)) * brush.bounds.height / 2.0;
        const PointF center{brush.bounds.x + brush.bounds.width / 2.0,
                            brush.bounds.y + brush.bounds.height / 2.0};
        const SkPoint points[] = {
            {static_cast<SkScalar>(center.x - std::cos(radians) * half_length),
             static_cast<SkScalar>(center.y - std::sin(radians) * half_length)},
            {static_cast<SkScalar>(center.x + std::cos(radians) * half_length),
             static_cast<SkScalar>(center.y + std::sin(radians) * half_length)},
        };
        SkGradientShaderResult made = SkShaders::LinearGradient(points, gradient);
        if (made.rejection != SkGradientRejection::kNone) return false;
        paint.setShader(std::move(made.shader));
        return true;
    }
    double radius{};
    for (const PointF point : brush.points) {
        radius = std::max(radius, std::hypot(point.x - brush.center.x,
                                             point.y - brush.center.y));
    }
    if (radius <= 0.0) {
        paint.setColor(to_sk_color(brush.primary));
        return true;
    }
    SkGradientShaderResult made = SkShaders::RadialGradient(
        {static_cast<SkScalar>(brush.center.x),
         static_cast<SkScalar>(brush.center.y)},
        static_cast<SkScalar>(radius), gradient);
    if (made.rejection != SkGradientRejection::kNone) return false;
    paint.setShader(std::move(made.shader));
    return true;
}

[[nodiscard]] SkPoint to_sk_point(PointF point) noexcept {
    return SkPoint{static_cast<SkScalar>(point.x), static_cast<SkScalar>(point.y)};
}

// A path through every point in order (draw_lines) or one contour per pair
// (draw_line_segments).
[[nodiscard]] SkPath batch_line_path(const std::vector<PointF>& points, bool pairs) {
    SkPathBuilder builder;
    builder.incReserve(static_cast<int>(points.size()));
    const std::size_t step = pairs ? 2U : points.size();
    for (std::size_t start = 0U; start + 1U < points.size(); start += step) {
        builder.moveTo(to_sk_point(points[start]));
        const std::size_t end = std::min(start + step, points.size());
        for (std::size_t index = start + 1U; index < end; ++index) builder.lineTo(to_sk_point(points[index]));
    }
    return builder.detach();
}

[[nodiscard]] bool marker_is_stroked(MarkerShape shape) noexcept {
    return shape == MarkerShape::cross || shape == MarkerShape::plus || shape == MarkerShape::asterisk;
}

// The marker centred on the origin, built once per draw_markers command
// from the GUI.Forms marker geometry (whose shapes share this order).
[[nodiscard]] SkPath marker_path(const MarkerStyle& style) {
    SkPathBuilder builder;
    const double half = style.size * 0.5;
    if (style.shape == MarkerShape::circle) {
        builder.addOval(SkRect::MakeLTRB(static_cast<SkScalar>(-half), static_cast<SkScalar>(-half),
                                         static_cast<SkScalar>(half), static_cast<SkScalar>(half)));
        return builder.detach();
    }
    const gui_forms::MarkerShape shape = static_cast<gui_forms::MarkerShape>(style.shape);
    std::array<gui_forms::Point, gui_forms::maximum_marker_vertices> outline{};
    const std::size_t vertex_count = gui_forms::marker_outline(shape, gui_forms::Point{}, style.size, outline);
    if (vertex_count != 0U) {
        builder.moveTo(static_cast<SkScalar>(outline[0].x), static_cast<SkScalar>(outline[0].y));
        for (std::size_t index = 1U; index < vertex_count; ++index) {
            builder.lineTo(static_cast<SkScalar>(outline[index].x), static_cast<SkScalar>(outline[index].y));
        }
        builder.close();
        return builder.detach();
    }
    std::array<gui_forms::PaintSegment, gui_forms::maximum_marker_lines> lines{};
    const std::size_t line_count = gui_forms::marker_lines(shape, gui_forms::Point{}, style.size, lines);
    for (std::size_t index = 0U; index < line_count; ++index) {
        builder.moveTo(static_cast<SkScalar>(lines[index].from.x), static_cast<SkScalar>(lines[index].from.y));
        builder.lineTo(static_cast<SkScalar>(lines[index].to.x), static_cast<SkScalar>(lines[index].to.y));
    }
    return builder.detach();
}

// Every marker's fill, then every marker's edge, so a later marker's fill
// never covers an earlier one's edge differently from the Painter fallback.
void draw_marker_batch(SkCanvas& canvas, const DrawingCommand& command) {
    const MarkerStyle& style = command.marker;
    const SkPath shape = marker_path(style);
    const bool stroked = marker_is_stroked(style.shape);
    SkPaint fill;
    configure_paint(fill, command.state);
    fill.setStyle(SkPaint::kFill_Style);
    fill.setColor(to_sk_color(style.fill));
    SkPaint edge;
    configure_paint(edge, command.state);
    edge.setStyle(SkPaint::kStroke_Style);
    edge.setColor(to_sk_color(style.edge));
    edge.setStrokeWidth(static_cast<SkScalar>(style.edge_width));
    const bool draws_fill = !stroked && SkColorGetA(fill.getColor()) != 0U;
    const bool draws_edge = style.edge_width > 0.0 && SkColorGetA(edge.getColor()) != 0U;
    if (draws_fill) {
        for (const PointF center : command.points) {
            canvas.drawPath(shape.makeOffset(static_cast<SkScalar>(center.x), static_cast<SkScalar>(center.y)),
                            fill);
        }
    }
    if (draws_edge) {
        for (const PointF center : command.points) {
            canvas.drawPath(shape.makeOffset(static_cast<SkScalar>(center.x), static_cast<SkScalar>(center.y)),
                            edge);
        }
    }
}

// The lines of one axis whose majority (`majors`) matches, as rectangles
// `line_width` wide across `area`.
void draw_grid_lines(SkCanvas& canvas, const SkPaint& paint, const RectF& area, const GridPatternStyle& grid,
                     bool vertical, bool majors) {
    const double half = grid.line_width * 0.5;
    const double low = vertical ? area.x : area.y;
    const double high = vertical ? area.right() : area.bottom();
    const double origin = vertical ? grid.origin.x : grid.origin.y;
    const gui_forms::detail::GridLineRange range =
        gui_forms::detail::grid_line_range(low, high, origin, grid.spacing, half);
    for (std::int64_t index = range.first; index < range.first + range.count; ++index) {
        if (gui_forms::detail::grid_line_is_major(index, grid.major_every) != majors) continue;
        const double centre = origin + static_cast<double>(index) * grid.spacing;
        const RectF line = vertical ? RectF{centre - half, area.y, grid.line_width, area.height}
                                    : RectF{area.x, centre - half, area.width, grid.line_width};
        canvas.drawRect(to_sk_rect(line), paint);
    }
}

// Minor lines under major ones, all clipped to the command's rectangle.
void draw_grid(SkCanvas& canvas, const DrawingCommand& command) {
    const GridPatternStyle& grid = command.grid;
    SkPaint minor;
    configure_paint(minor, command.state);
    minor.setColor(to_sk_color(grid.minor));
    SkPaint major;
    configure_paint(major, command.state);
    major.setColor(to_sk_color(grid.major));
    canvas.save();
    canvas.clipRect(to_sk_rect(command.rect), SkClipOp::kIntersect, false);
    draw_grid_lines(canvas, minor, command.rect, grid, true, false);
    draw_grid_lines(canvas, minor, command.rect, grid, false, false);
    draw_grid_lines(canvas, major, command.rect, grid, true, true);
    draw_grid_lines(canvas, major, command.rect, grid, false, true);
    canvas.restore();
}

// The part of the track a proportion covers from the direction's edge.
[[nodiscard]] RectF proportion_part(RectF track, double proportion, FillDirection direction) noexcept {
    const double across = track.width * proportion;
    const double up = track.height * proportion;
    switch (direction) {
    case FillDirection::left_to_right: return RectF{track.x, track.y, across, track.height};
    case FillDirection::right_to_left: return RectF{track.right() - across, track.y, across, track.height};
    case FillDirection::bottom_to_top: return RectF{track.x, track.bottom() - up, track.width, up};
    case FillDirection::top_to_bottom: return RectF{track.x, track.y, track.width, up};
    }
    return RectF{};
}

[[nodiscard]] SkRRect track_shape(RectF track, const CornerRadii& radii) {
    const SkScalar limit = static_cast<SkScalar>(std::min(track.width, track.height) * 0.5);
    const SkScalar top_left = std::min(static_cast<SkScalar>(radii.top_left), limit);
    const SkScalar top_right = std::min(static_cast<SkScalar>(radii.top_right), limit);
    const SkScalar bottom_right = std::min(static_cast<SkScalar>(radii.bottom_right), limit);
    const SkScalar bottom_left = std::min(static_cast<SkScalar>(radii.bottom_left), limit);
    // Skia's corner order: upper left, upper right, lower right, lower left.
    const std::array<SkVector, 4> corners{SkVector{top_left, top_left}, SkVector{top_right, top_right},
                                          SkVector{bottom_right, bottom_right},
                                          SkVector{bottom_left, bottom_left}};
    SkRRect shape;
    shape.setRectRadii(to_sk_rect(track), corners.data());
    return shape;
}

class BitmapUnlock final {
public:
    BitmapUnlock(Bitmap& bitmap, std::uint64_t token)
        : bitmap_(&bitmap), token_(token) {}
    ~BitmapUnlock() {
        if (bitmap_ != nullptr) {
            try { (*bitmap_).unlock(token_); } catch (...) {}
        }
    }
    void release() {
        (*bitmap_).unlock(token_);
        bitmap_ = nullptr;
    }

private:
    Bitmap* bitmap_;
    std::uint64_t token_;
};


// Whether a coordinate is a whole number that float holds exactly: Skia
// receives every rectangle and matrix entry as float (to_sk_rect,
// to_sk_matrix).
[[nodiscard]] bool exact_whole(double value) noexcept {
    constexpr double float_exact_limit = 16777216.0;
    const bool whole = std::isfinite(value) && std::abs(value) <= float_exact_limit && std::floor(value) == value;
    return whole;
}

// An image drawn at its own size, opaque, without attributes, under an
// integer translation and no clip or a clip on whole pixels: the draw is a
// copy of its source rectangle's rows. Source-over and source-copy agree
// for an opaque image, and at its own size on whole pixels both of Skia's
// samplings return each texel unchanged. Positions are device pixels:
// the source rectangle's first pixel lands on (left, top).
struct ImageRowCopy final {
    std::int64_t source_x{};
    std::int64_t source_y{};
    std::int64_t width{};
    std::int64_t height{};
    std::int64_t left{};
    std::int64_t top{};
    // The clip, [clip_left, clip_right) x [clip_top, clip_bottom), when
    // the state has one.
    bool clipped{};
    std::int64_t clip_left{};
    std::int64_t clip_top{};
    std::int64_t clip_right{};
    std::int64_t clip_bottom{};
};

// The row copy a draw_image command amounts to, or none when it is not one
// (see ImageRowCopy). Reads the source rectangle's alpha bytes once.
[[nodiscard]] std::optional<ImageRowCopy> image_row_copy(const DrawingCommand& command) {
    const ImageAttributesSnapshot& attributes = command.image_attributes;
    if (attributes.color_matrix.has_value() || !attributes.remap_table.empty()) return std::nullopt;
    const Matrix& transform = command.state.transform;
    if (transform.m11() != 1.0 || transform.m12() != 0.0 || transform.m21() != 0.0 || transform.m22() != 1.0) {
        return std::nullopt;
    }
    // first is the source rectangle's origin and second its size.
    const std::array<double, 8> placement{command.first.x, command.first.y, command.second.x, command.second.y,
                                          command.rect.x, command.rect.y, transform.dx(), transform.dy()};
    for (const double value : placement) {
        if (!exact_whole(value)) return std::nullopt;
    }
    if (command.rect.width != command.second.x || command.rect.height != command.second.y) return std::nullopt;
    const ImageSnapshot& image = command.image;
    ImageRowCopy copy{};
    copy.source_x = static_cast<std::int64_t>(command.first.x);
    copy.source_y = static_cast<std::int64_t>(command.first.y);
    copy.width = static_cast<std::int64_t>(command.second.x);
    copy.height = static_cast<std::int64_t>(command.second.y);
    if (copy.source_x < 0 || copy.source_y < 0 || copy.width <= 0 || copy.height <= 0 ||
        copy.source_x + copy.width > static_cast<std::int64_t>(image.width) ||
        copy.source_y + copy.height > static_cast<std::int64_t>(image.height)) {
        return std::nullopt;
    }
    const std::size_t row_bytes = image.row_bytes();
    const std::span<const std::byte> pixels = image.pixels();
    const std::size_t last_row = static_cast<std::size_t>(copy.source_y + copy.height - 1);
    const std::size_t row_end = static_cast<std::size_t>(copy.source_x + copy.width) * 4U;
    // Every copied row lies inside the pixels: the last starts at last_row
    // * row_bytes and needs row_end bytes (row_bytes >= row_end > 0).
    if (row_bytes < row_end || pixels.size() < row_end) return std::nullopt;
    if (last_row > (pixels.size() - row_end) / row_bytes) return std::nullopt;
    if (command.state.clip) {
        const RectF& clip = *command.state.clip;
        if (!exact_whole(clip.x) || !exact_whole(clip.y) || !exact_whole(clip.width) || !exact_whole(clip.height) ||
            clip.width < 0.0 || clip.height < 0.0) {
            return std::nullopt;
        }
        copy.clipped = true;
        copy.clip_left = static_cast<std::int64_t>(clip.x);
        copy.clip_top = static_cast<std::int64_t>(clip.y);
        copy.clip_right = static_cast<std::int64_t>(clip.x + clip.width);
        copy.clip_bottom = static_cast<std::int64_t>(clip.y + clip.height);
    }
    copy.left = static_cast<std::int64_t>(command.rect.x) + static_cast<std::int64_t>(transform.dx());
    copy.top = static_cast<std::int64_t>(command.rect.y) + static_cast<std::int64_t>(transform.dy());
    // Opaque: every alpha byte (the fourth of each BGRA pixel) is 255.
    std::uint8_t every = 255U;
    for (std::int64_t row = 0; row < copy.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(copy.source_y + row) * row_bytes +
                                  static_cast<std::size_t>(copy.source_x) * 4U;
        for (std::size_t offset = 3U; offset < static_cast<std::size_t>(copy.width) * 4U; offset += 4U) {
            every = static_cast<std::uint8_t>(every & std::to_integer<std::uint8_t>(pixels[start + offset]));
        }
    }
    if (every != 255U) return std::nullopt;
    return copy;
}

// Copies the image's rows as `copy` places them into the locked target,
// within the target and the clip. The image is a snapshot: its pixels are
// shared with no target that is written (an owned target's write lock
// copies storage a snapshot shares, and a borrowed target's snapshot is a
// copy), so source and destination never overlap.
void copy_image_rows(const ImageRowCopy& copy, const ImageSnapshot& image, const BitmapLockView& lock) {
    std::int64_t left = std::max<std::int64_t>(copy.left, 0);
    std::int64_t top = std::max<std::int64_t>(copy.top, 0);
    std::int64_t right = std::min<std::int64_t>(copy.left + copy.width, static_cast<std::int64_t>(lock.width));
    std::int64_t bottom = std::min<std::int64_t>(copy.top + copy.height, static_cast<std::int64_t>(lock.height));
    if (copy.clipped) {
        left = std::max(left, copy.clip_left);
        top = std::max(top, copy.clip_top);
        right = std::min(right, copy.clip_right);
        bottom = std::min(bottom, copy.clip_bottom);
    }
    if (right <= left || bottom <= top) return;
    const std::size_t row_bytes = image.row_bytes();
    const std::size_t copied = static_cast<std::size_t>(right - left) * 4U;
    const std::byte* const source = image.pixels().data();
    for (std::int64_t y = top; y < bottom; ++y) {
        const std::size_t from = static_cast<std::size_t>(copy.source_y + y - copy.top) * row_bytes +
                                 static_cast<std::size_t>(copy.source_x + left - copy.left) * 4U;
        const std::size_t to = static_cast<std::size_t>(y) * lock.row_bytes + static_cast<std::size_t>(left) * 4U;
        std::memcpy(lock.writable_data + to, source + from, copied);
    }
}

// The error the draw would end `command` with, found without drawing: its
// paint, path and image checked as the draw makes them. An owned target's
// list is checked first, so that a list that would fail leaves it as it
// was.
[[nodiscard]] RasterError refusal_of(const DrawingCommand& command) {
    SkPaint paint;
    switch (command.kind) {
    case CommandKind::save:
    case CommandKind::restore:
    case CommandKind::translate:
    case CommandKind::set_transform:
    case CommandKind::set_clip:
    case CommandKind::reset_clip:
    case CommandKind::set_quality:
    case CommandKind::clear:
    case CommandKind::draw_string:
    case CommandKind::draw_markers:
    case CommandKind::fill_grid_pattern:
        return RasterError::none;
    case CommandKind::fill_rectangle:
    case CommandKind::fill_ellipse:
    case CommandKind::fill_polygon:
    case CommandKind::fill_rectangles:
    case CommandKind::fill_proportion: {
        const bool configured = configure_brush(paint, command.brush, command.state);
        return configured ? RasterError::none : RasterError::invalid_argument;
    }
    case CommandKind::draw_rectangle:
    case CommandKind::draw_line:
    case CommandKind::draw_ellipse:
    case CommandKind::draw_lines:
    case CommandKind::draw_line_segments: {
        const bool configured = configure_pen(paint, command.pen, command.state);
        return configured ? RasterError::none : RasterError::invalid_argument;
    }
    case CommandKind::draw_path: {
        const std::optional<SkPath> path = to_sk_path(command.path);
        const bool drawable = path.has_value() && configure_pen(paint, command.pen, command.state);
        return drawable ? RasterError::none : RasterError::invalid_argument;
    }
    case CommandKind::fill_path: {
        const std::optional<SkPath> path = to_sk_path(command.path);
        const bool drawable = path.has_value() && configure_brush(paint, command.brush, command.state);
        return drawable ? RasterError::none : RasterError::invalid_argument;
    }
    case CommandKind::draw_image: {
        if (!command.image.has_pixels()) return RasterError::missing_image_pixels;
        const SkImageInfo source_info = image_info(command.image.width, command.image.height);
        const std::size_t row_bytes = command.image.row_bytes();
        const bool fits = source_info.validRowBytes(row_bytes) &&
                          source_info.computeByteSize(row_bytes) <= command.image.pixels().size();
        return fits ? RasterError::none : RasterError::missing_image_pixels;
    }
    default:
        break;
    }
    // Not a CommandKind: the list cannot be replayed.
    return RasterError::invalid_argument;
}

} // namespace

namespace text = gui_forms::render::text;

namespace {

// Vertical metrics per em, unrounded, from the face's tables as FreeType
// reads them (hhea, falling back to OS/2).
struct FaceMetrics final {
    double ascent{};
    double descent{};
    double line_gap{};
};

// One encoded face of a family: the shared bytes, the collection index and
// its metrics.
struct FaceRecord final {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    std::uint32_t index{};
    FaceMetrics metrics{};
};

// A family's faces, indexed by the bold (1) and italic (2) bits of a font
// style, with a private HarfBuzz engine that shapes them and a rasterizer
// over the same bytes (ADR-004). Declared in that order so the rasterizer is
// destroyed before the engine it borrows.
struct TextFamily final {
    std::array<std::optional<FaceRecord>, 4> faces;
    std::unique_ptr<text::HarfBuzzFontEngine> engine;
    std::unique_ptr<text::GlyphRunRasterizer> rasterizer;
};

constexpr std::size_t bold_slot_bit = 1U;
constexpr std::size_t italic_slot_bit = 2U;

// The faces tried for each requested slot, nearest first: the same slant
// with the other weight before the other slant. A face of another weight or
// slant is emboldened or sheared toward the request.
constexpr std::array<std::array<std::size_t, 4>, 4> nearest_slots{{
    {0U, 1U, 2U, 3U},
    {1U, 0U, 3U, 2U},
    {2U, 0U, 3U, 1U},
    {3U, 1U, 2U, 0U},
}};

constexpr std::uint16_t regular_weight = 400U;
constexpr std::uint16_t bold_weight = 700U;
constexpr std::size_t maximum_font_bytes = 64U * 1024U * 1024U;

// A font request resolved to one registered face and the styles synthesized
// for it.
struct ResolvedFont final {
    TextFamily* family{};
    gui_forms::FontSpec spec{};
    text::GlyphRunSynthesis synthesis{};
    FaceMetrics metrics{};
};

class FreeTypeLibrary final {
public:
    FreeTypeLibrary() {
        if (FT_Init_FreeType(&library_) != 0) throw std::runtime_error("FreeType initialization failed");
    }
    ~FreeTypeLibrary() { FT_Done_FreeType(library_); }
    FreeTypeLibrary(const FreeTypeLibrary&) = delete;
    FreeTypeLibrary& operator=(const FreeTypeLibrary&) = delete;
    [[nodiscard]] FT_Library get() const noexcept { return library_; }

private:
    FT_Library library_{};
};

class FreeTypeFace final {
public:
    FreeTypeFace(FT_Library library, std::span<const std::byte> bytes, FT_Long index) {
        status_ = FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(bytes.data()),
                                     static_cast<FT_Long>(bytes.size()), index, &face_);
    }
    ~FreeTypeFace() {
        if (status_ == 0) FT_Done_Face(face_);
    }
    FreeTypeFace(const FreeTypeFace&) = delete;
    FreeTypeFace& operator=(const FreeTypeFace&) = delete;
    // Nullptr when the bytes are not a font or the index is out of range.
    [[nodiscard]] FT_Face get() const noexcept { return status_ == 0 ? face_ : nullptr; }

private:
    FT_Face face_{};
    FT_Error status_{};
};

[[nodiscard]] int face_weight(FT_Face face) {
    const TT_OS2* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && (*os2).usWeightClass != 0U) return static_cast<int>((*os2).usWeightClass);
    const int weight = ((*face).style_flags & FT_STYLE_FLAG_BOLD) != 0 ? bold_weight : regular_weight;
    return weight;
}

[[nodiscard]] FaceMetrics face_metrics(FT_Face face) noexcept {
    FaceMetrics result{};
    const double units = (*face).units_per_EM == 0U ? 1000.0 : static_cast<double>((*face).units_per_EM);
    result.ascent = static_cast<double>((*face).ascender) / units;
    result.descent = -static_cast<double>((*face).descender) / units;
    const double height = static_cast<double>((*face).height) / units;
    result.line_gap = std::max(0.0, height - result.ascent - result.descent);
    return result;
}

// Device clip for text: the command clip snapped to whole pixels as Skia's
// non-antialiased clipRect does for every other command, or the target.
[[nodiscard]] gui_forms::Rect text_clip_rect(const GraphicsState& state, const std::uint32_t width,
                                             const std::uint32_t height) noexcept {
    gui_forms::Rect result{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
    if (!state.clip) return result;
    const RectF clip = *state.clip;
    // Within the target: a clip may reach far beyond it (draw_string_over_fill's
    // bands reach 2^30 units when no clip is in force), past the text
    // compositor's coordinate range, and nothing outside the target is drawn.
    const double left = std::clamp(std::floor(clip.x + 0.5), 0.0, result.width);
    const double top = std::clamp(std::floor(clip.y + 0.5), 0.0, result.height);
    const double right = std::clamp(std::floor(clip.x + clip.width + 0.5), 0.0, result.width);
    const double bottom = std::clamp(std::floor(clip.y + clip.height + 0.5), 0.0, result.height);
    result = {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
    return result;
}

} // namespace

class SkiaExecutor::Impl final {
public:
    FreeTypeLibrary library;
    // Keyed by ASCII-lowercased family name.
    std::unordered_map<std::string, std::unique_ptr<TextFamily>> families;
    // Requested family to the registered family drawn for it, both
    // ASCII-lowercased (stage F substitution).
    std::unordered_map<std::string, std::string> aliases;

    // Chooses the collection face best matching family and weight, scanning
    // at most 64 faces. Nullopt when the bytes hold no usable face.
    [[nodiscard]] std::optional<FaceRecord> select_face(std::string_view family,
                                                        std::shared_ptr<const std::vector<std::byte>> bytes,
                                                        const int requested_weight) const {
        const std::string requested_family = ascii_lower(family);
        std::optional<FaceRecord> best;
        int best_score = std::numeric_limits<int>::min();
        const FreeTypeFace collection(library.get(), *bytes, -1);
        if (collection.get() == nullptr) return best;
        const FT_Long count = std::min<FT_Long>((*collection.get()).num_faces, 64);
        for (FT_Long index = 0; index < count; ++index) {
            const FreeTypeFace candidate(library.get(), *bytes, index);
            const FT_Face face = candidate.get();
            if (face == nullptr || !FT_IS_SCALABLE(face)) continue;
            const bool exact_family = (*face).family_name != nullptr &&
                ascii_lower((*face).family_name) == requested_family;
            const int weight_distance = std::abs(face_weight(face) - requested_weight);
            const int score = (exact_family ? 100000 : 0) - weight_distance;
            if (score > best_score) {
                best_score = score;
                best = FaceRecord{bytes, static_cast<std::uint32_t>(index), face_metrics(face)};
            }
            if (exact_family && weight_distance == 0) break;
        }
        return best;
    }

    // Stores a face in one of a family's four slots and rebuilds the
    // family's engine from its records, so a replaced face takes effect.
    void install(const std::string& key, const std::size_t slot, FaceRecord record) {
        std::unique_ptr<TextFamily>& entry = families[key];
        if (!entry) entry = std::make_unique<TextFamily>();
        TextFamily& family = *entry;
        family.faces[slot] = std::move(record);
        family.rasterizer.reset();
        family.engine = std::make_unique<text::HarfBuzzFontEngine>();
        for (std::size_t index = 0; index < family.faces.size(); ++index) {
            const std::optional<FaceRecord>& face = family.faces[index];
            if (!face) continue;
            const std::uint16_t weight = (index & bold_slot_bit) != 0U ? bold_weight : regular_weight;
            const bool italic = (index & italic_slot_bit) != 0U;
            if (!(*family.engine).register_shared_typeface(gui_forms::FontRole::control, weight, italic,
                                                          (*face).bytes, (*face).index)) {
                throw std::runtime_error("font registration failed");
            }
        }
        family.rasterizer = std::make_unique<text::GlyphRunRasterizer>(*family.engine);
    }

    // The family registered under a name, following its alias if it has one.
    [[nodiscard]] TextFamily* registered(std::string_view family) {
        using FamilyMap =
            std::unordered_map<std::string, std::unique_ptr<TextFamily>>;
        using AliasMap = std::unordered_map<std::string, std::string>;
        std::string key = ascii_lower(family);
        const AliasMap::const_iterator alias = aliases.find(key);
        if (alias != aliases.end()) key = (*alias).second;
        const FamilyMap::iterator found = families.find(key);
        TextFamily* const result = found == families.end() ? nullptr : (*found).second.get();
        return result;
    }

    // The registered family, else Portsmouth Rapids. The requested slot's
    // face draws as it is; otherwise the nearest registered face is
    // emboldened or sheared toward the request.
    [[nodiscard]] std::optional<ResolvedFont> resolve(const FontSnapshot& spec) {
        const std::size_t requested = static_cast<std::size_t>(spec.style & 3U);
        TextFamily* family = registered(spec.family);
        if (family == nullptr) family = registered("Portsmouth Rapids");
        std::optional<ResolvedFont> result;
        if (family == nullptr) return result;
        std::size_t slot = requested;
        for (const std::size_t candidate : nearest_slots[requested]) {
            if (!(*family).faces[candidate]) continue;
            slot = candidate;
            break;
        }
        if (!(*family).faces[slot]) return result;
        const double size = pixel_font_size(spec);
        if (!std::isfinite(size) || size <= 0.0) throw std::invalid_argument("font size is invalid");
        const bool face_bold = (slot & bold_slot_bit) != 0U;
        const bool face_italic = (slot & italic_slot_bit) != 0U;
        ResolvedFont resolved{};
        resolved.family = family;
        resolved.spec = gui_forms::FontSpec{gui_forms::FontRole::control, size,
                                            face_bold ? bold_weight : regular_weight, face_italic};
        resolved.synthesis.embolden = (requested & bold_slot_bit) != 0U && !face_bold;
        resolved.synthesis.oblique = (requested & italic_slot_bit) != 0U && !face_italic;
        resolved.metrics = (*(*family).faces[slot]).metrics;
        result = resolved;
        return result;
    }

    void draw_text(const DrawingCommand& command, const gui_forms::detail::TextCompositeTarget& target);
};

// Shapes and draws one draw_string command through the shared text engine
// and compositor (ADR-004).
void SkiaExecutor::Impl::draw_text(const DrawingCommand& command,
                                   const gui_forms::detail::TextCompositeTarget& target) {
    const bool source = command.state.compositing == CompositingMode::source_copy;
    const Color colour = command.color;
    const std::uint8_t alpha = colour.is_empty() ? 0U : colour.alpha();
    if (alpha == 0U && !source) return;
    const std::optional<ResolvedFont> resolved = resolve(command.font);
    if (!resolved) return;
    const ResolvedFont& font = *resolved;
    TextFamily& family = *font.family;
    const double size = font.spec.size;
    const double overhang = (command.format.flags & UINT32_C(0x0004)) == 0U ? size / 6.0 : 0.0;
    const double first_baseline = command.first.y + font.metrics.ascent * size + overhang;
    const double line_height = std::max(0.0, (font.metrics.ascent + font.metrics.descent + font.metrics.line_gap) * size +
                                                 overhang);
    const Matrix& matrix = command.state.transform;
    const bool uniform = matrix.m12() == 0.0 && matrix.m21() == 0.0 && matrix.m11() == matrix.m22() &&
        std::isfinite(matrix.m11()) && matrix.m11() > 0.0;
    const gui_forms::detail::TextCompositeClip clip{text_clip_rect(command.state, target.width, target.height)};
    gui_forms::Color text_colour = gui_forms::Color::rgba(0, 0, 0, 0);
    if (!colour.is_empty()) text_colour = gui_forms::Color::rgba(colour.red(), colour.green(), colour.blue(), alpha);
    const gui_forms::detail::TextCompositeMode mode =
        source ? gui_forms::detail::TextCompositeMode::source : gui_forms::detail::TextCompositeMode::source_over;
    std::string_view remaining = command.text;
    std::size_t line_index{};
    for (;;) {
        const std::size_t newline = remaining.find('\n');
        std::string_view line = remaining.substr(0U, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (!line.empty()) {
            const text::ShapedText shaped = (*family.engine).shape(line, font.spec);
            double x = command.first.x + overhang;
            if (command.format.alignment == StringAlignment::center) x -= shaped.width / 2.0;
            if (command.format.alignment == StringAlignment::far) x -= shaped.width;
            const double y = first_baseline + line_height * static_cast<double>(line_index);
            const double device_x = matrix.m11() * x + matrix.m21() * y + matrix.dx();
            const double device_y = matrix.m12() * x + matrix.m22() * y + matrix.dy();
            text::GlyphRunOrigin origin{};
            text::GlyphRunCoverage coverage{};
            if (uniform) {
                origin = text::glyph_run_origin(device_x, device_y);
                coverage = (*family.rasterizer).rasterize(shaped, font.spec, matrix.m11(), origin.phase_x, font.synthesis);
            } else {
                const text::GlyphRunTransform linear{matrix.m11(), matrix.m21(), matrix.m12(), matrix.m22()};
                coverage = (*family.rasterizer).rasterize_transformed(shaped, font.spec, linear, device_x, device_y,
                                                                     font.synthesis, origin);
            }
            const gui_forms::detail::TextCoverage placed{coverage.pixels, coverage.width, coverage.height,
                                                         origin.pixel_x + coverage.left, origin.pixel_y + coverage.top};
            if (gui_forms::detail::composite_text_coverage(placed, target, clip, text_colour, mode) !=
                gui_forms::detail::TextCompositeStatus::success) {
                throw std::invalid_argument("text placement is outside the supported range");
            }
        }
        if (newline == std::string_view::npos) break;
        remaining.remove_prefix(newline + 1U);
        ++line_index;
    }
}


SkiaExecutor::SkiaExecutor()
    : impl_(std::make_unique<Impl>()), owner_thread_(std::this_thread::get_id()) {}
SkiaExecutor::~SkiaExecutor() = default;

bool SkiaExecutor::register_typeface(std::string_view family,
                                     std::span<const std::byte> encoded,
                                     std::uint32_t style) {
    if (!owner_thread() || family.empty() || family.size() > 4096U || encoded.empty() ||
        encoded.size() > maximum_font_bytes) {
        return false;
    }
    try {
        const std::shared_ptr<const std::vector<std::byte>> bytes =
            std::make_shared<const std::vector<std::byte>>(encoded.begin(), encoded.end());
        const std::size_t slot = static_cast<std::size_t>(style & 3U);
        const bool bold = (slot & bold_slot_bit) != 0U;
        // Collections (TTC/OTC) hold several faces; choose the requested
        // family and weight instead of silently using face zero.
        std::optional<FaceRecord> record = (*impl_).select_face(family, bytes, bold ? bold_weight : regular_weight);
        if (!record) return false;
        const std::string key = ascii_lower(family);
        // A face registered under its own name replaces an alias of it.
        (*impl_).aliases.erase(key);
        (*impl_).install(key, slot, std::move(*record));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool SkiaExecutor::alias_family(std::string_view requested, std::string_view family) {
    if (!owner_thread() || requested.empty() || requested.size() > 4096U || family.empty() ||
        family.size() > 4096U) {
        return false;
    }
    try {
        const std::string target = ascii_lower(family);
        if ((*impl_).families.find(target) == (*impl_).families.end()) return false;
        const std::string key = ascii_lower(requested);
        if (key == target) return true;
        // The alias replaces faces registered under the requested name.
        (*impl_).families.erase(key);
        (*impl_).aliases.insert_or_assign(key, target);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

SizeF SkiaExecutor::measure_string(std::string_view utf8,
                                  const FontSnapshot& font_spec,
                                  const StringFormatSnapshot& format,
                                  double layout_width) {
    if (!owner_thread() || !std::isfinite(layout_width) || layout_width < 0.0) {
        throw std::invalid_argument("text measurement arguments are invalid");
    }
    if (utf8.empty()) return {};
    const std::optional<ResolvedFont> resolved = (*impl_).resolve(font_spec);
    if (!resolved) return {};
    const ResolvedFont& font = *resolved;
    text::HarfBuzzFontEngine& engine = *(*font.family).engine;
    const double size = font.spec.size;
    const bool fit_black_box = (format.flags & UINT32_C(0x0004)) != 0U;
    const double overhang = fit_black_box ? 0.0 : size / 6.0;
    const double line_height = std::max(0.0,
        (font.metrics.ascent + font.metrics.descent + font.metrics.line_gap) * size + overhang);
    const bool wrap = layout_width > 0.0 && (format.flags & UINT32_C(0x1000)) == 0U;
    const double content_width = wrap ? std::max(0.0, layout_width - overhang * 2.0) : 0.0;
    double maximum_width{};
    std::size_t line_count{};
    std::size_t offset{};
    // Per paragraph, reused: each byte's cluster advance, whether a cluster
    // starts there, and the line-break opportunity after it.
    std::vector<double> advance_at;
    std::vector<std::uint8_t> cluster_start;
    std::vector<gui_forms::LineBreakOpportunity> opportunities;
    do {
        const std::size_t newline = utf8.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? utf8.size() : newline;
        std::string_view line = utf8.substr(offset, end - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (!wrap || line.empty()) {
            const double width = line.empty() ? 0.0 : engine.shape(line, font.spec).width;
            maximum_width = std::max(maximum_width, width);
            ++line_count;
        } else {
            // N60: the paragraph is shaped once. Its clusters (from one
            // glyph cluster's first byte to the next's) are what a line
            // fits, so no line ends inside a cluster, and a line that does
            // not fit ends at the last UAX #14 opportunity among its fitted
            // clusters, its trailing spaces and tabs not measured. Each line
            // so broken is shaped once more for its width: the shaping is
            // linear in the paragraph, not quadratic.
            const text::ShapedText shaped = engine.shape(line, font.spec);
            const std::size_t length = line.size();
            advance_at.assign(length, 0.0);
            cluster_start.assign(length, 0U);
            cluster_start[0] = 1U;
            for (const text::ShapedFontRun& run : shaped.runs) {
                for (const text::ShapedGlyph& glyph : run.glyphs) {
                    const std::size_t cluster = glyph.cluster.value();
                    if (cluster >= length) continue;
                    advance_at[cluster] += static_cast<double>(glyph.advance_x);
                    cluster_start[cluster] = 1U;
                }
            }
            opportunities.resize(length);
            const bool breaks_known =
                text::HarfBuzzFontEngine::fill_line_break_opportunities_utf8(line, opportunities) !=
                gui_forms::TextResolutionStatus::invalid_request;
            std::size_t line_start = 0U;
            while (line_start < length) {
                double width{};
                std::size_t cursor = line_start;
                std::size_t break_end = line_start;
                while (cursor < length) {
                    std::size_t next = cursor + 1U;
                    while (next < length && cluster_start[next] == 0U) ++next;
                    // A glyph's advance is held at its cluster's first byte.
                    const double cluster_width = advance_at[cursor];
                    // The first cluster is always taken, even when too wide.
                    if (cursor > line_start && width + cluster_width > content_width) break;
                    width += cluster_width;
                    cursor = next;
                    const gui_forms::LineBreakOpportunity after = opportunities[next - 1U];
                    if (breaks_known && (after == gui_forms::LineBreakOpportunity::allowed ||
                                         after == gui_forms::LineBreakOpportunity::mandatory)) {
                        break_end = next;
                    }
                }
                std::size_t consumed = cursor;
                if (cursor < length && break_end > line_start) {
                    std::size_t trimmed = break_end;
                    while (trimmed > line_start && (line[trimmed - 1U] == ' ' || line[trimmed - 1U] == '\t')) {
                        --trimmed;
                    }
                    if (trimmed > line_start) {
                        consumed = break_end;
                        width = engine.shape(line.substr(line_start, trimmed - line_start), font.spec).width;
                    }
                }
                maximum_width = std::max(maximum_width, std::min(content_width, width));
                ++line_count;
                line_start = consumed;
                while (line_start < length && (line[line_start] == ' ' || line[line_start] == '\t')) {
                    ++line_start;
                }
            }
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    } while (offset <= utf8.size());
    return {wrap ? std::min(layout_width, maximum_width + overhang * 2.0) :
                   maximum_width + overhang * 2.0,
            line_height * static_cast<double>(line_count)};
}

RasterResult SkiaExecutor::execute(const GraphicsRecorder& recorder,
                                   Bitmap& target,
                                   std::size_t first_command) {
    if (!owner_thread()) return {RasterError::wrong_thread};
    try {
        const std::span<const DrawingCommand> commands = recorder.commands();
        if (first_command > commands.size()) return {RasterError::invalid_argument};
        if (first_command == commands.size()) return {};
        // Every target is drawn in place. An owned target's list is checked
        // first, command by command, for what the draw would refuse, so a
        // list that fails leaves it untouched, its generation included;
        // only an exception during the draw itself leaves the commands
        // before it drawn. A target borrowing native memory (a DIB under
        // Graphics.FromHdc) is not checked first: GDI writes the program's
        // own buffer without transactions too, and a failed command leaves
        // the ones before it drawn.
        if (!target.borrows_memory()) {
            for (std::size_t index = first_command; index < commands.size(); ++index) {
                const RasterError refused = refusal_of(commands[index]);
                if (refused != RasterError::none) return {refused, index - first_command};
            }
        }
        const BitmapLockView target_lock = target.lock(BitmapLockMode::write);
        BitmapUnlock unlock_target(target, target_lock.token);
        const SkImageInfo info = image_info(target_lock.width, target_lock.height);
        sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
            info, target_lock.writable_data, target_lock.row_bytes);
        if (!surface) return {RasterError::target_unavailable};
        SkCanvas* canvas = (*surface).getCanvas();
        // Text composites straight into the pixels Skia draws into; raster
        // canvases draw immediately, so command order is preserved.
        if (target_lock.row_bytes % sizeof(std::uint32_t) != 0U) return {RasterError::target_unavailable};
        const std::size_t text_stride = target_lock.row_bytes / sizeof(std::uint32_t);
        gui_forms::detail::TextCompositeTarget text_target{};
        text_target.pixels = std::span<std::uint32_t>(reinterpret_cast<std::uint32_t*>(target_lock.writable_data),
                                                      text_stride * target_lock.height);
        text_target.width = target_lock.width;
        text_target.height = target_lock.height;
        text_target.stride = text_stride;
        // The state the canvas holds, the command before's while it still
        // stands: a command whose clip and transform equal it draws under
        // the canvas as it is. fill_proportion's own clip ends it.
        const GraphicsState* applied = nullptr;
        std::size_t executed{};
        for (const DrawingCommand& command : commands.subspan(first_command)) {
            // A command draws inside a save of its own: restoreAll undoes
            // only what lies above the base level, so the state's clip and a
            // command's own (fill_proportion's track) are made inside it.
            // The state is set again only where it differs from the one the
            // canvas holds: a new clip from the base, a new transform alone
            // over the same clip (the clip was made under the identity).
            const bool same_clip = applied != nullptr && (*applied).clip == command.state.clip;
            const bool same_transform = applied != nullptr && (*applied).transform == command.state.transform;
            if (!same_clip) {
                (*canvas).restoreAll();
                (*canvas).save();
                (*canvas).resetMatrix();
                if (command.state.clip) {
                    (*canvas).clipRect(to_sk_rect(*command.state.clip),
                                     SkClipOp::kIntersect, false);
                }
                (*canvas).setMatrix(to_sk_matrix(command.state.transform));
            } else if (!same_transform) {
                (*canvas).setMatrix(to_sk_matrix(command.state.transform));
            }
            applied = &command.state;

            SkPaint paint;
            configure_paint(paint, command.state);
            switch (command.kind) {
            case CommandKind::save:
            case CommandKind::restore:
            case CommandKind::translate:
            case CommandKind::set_transform:
            case CommandKind::set_clip:
            case CommandKind::reset_clip:
            case CommandKind::set_quality:
                // State commands: their effect is in command.state, applied above.
                break;
            case CommandKind::clear:
                (*canvas).clear(to_sk_color(command.color));
                break;
            case CommandKind::fill_rectangle:
                if (!configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawRect(to_sk_rect(command.rect), paint);
                break;
            case CommandKind::draw_rectangle:
                if (!configure_pen(paint, command.pen, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawRect(to_sk_rect(command.rect), paint);
                break;
            case CommandKind::draw_line:
                if (!configure_pen(paint, command.pen, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawLine(static_cast<SkScalar>(command.first.x),
                                 static_cast<SkScalar>(command.first.y),
                                 static_cast<SkScalar>(command.second.x),
                                 static_cast<SkScalar>(command.second.y), paint);
                break;
            case CommandKind::draw_string:
                (*impl_).draw_text(command, text_target);
                break;
            case CommandKind::draw_ellipse:
                if (!configure_pen(paint, command.pen, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawOval(to_sk_rect(command.rect), paint);
                break;
            case CommandKind::fill_ellipse:
                if (!configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawOval(to_sk_rect(command.rect), paint);
                break;
            case CommandKind::fill_polygon: {
                SkPathBuilder builder(command.path.fill_mode == FillMode::alternate ?
                    SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
                builder.moveTo(static_cast<SkScalar>(command.points.front().x),
                               static_cast<SkScalar>(command.points.front().y));
                for (std::size_t index = 1; index < command.points.size(); ++index) {
                    builder.lineTo(static_cast<SkScalar>(command.points[index].x),
                                   static_cast<SkScalar>(command.points[index].y));
                }
                builder.close();
                if (!configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawPath(builder.detach(), paint);
                break;
            }
            case CommandKind::draw_path: {
                const std::optional<SkPath> path = to_sk_path(command.path);
                if (!path || !configure_pen(paint, command.pen, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawPath(*path, paint);
                break;
            }
            case CommandKind::fill_path: {
                const std::optional<SkPath> path = to_sk_path(command.path);
                if (!path || !configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawPath(*path, paint);
                break;
            }
            case CommandKind::draw_image: {
                if (!command.image.has_pixels()) {
                    return {RasterError::missing_image_pixels, executed};
                }
                const std::optional<ImageRowCopy> row_copy = image_row_copy(command);
                if (row_copy) {
                    copy_image_rows(*row_copy, command.image, target_lock);
                    break;
                }
                const SkImageInfo source_info = image_info(command.image.width, command.image.height);
                const std::vector<std::byte> pixels = remapped_pixels(
                    command.image, command.image_attributes);
                sk_sp<SkData> data = SkData::MakeWithCopy(
                    pixels.data(), pixels.size());
                sk_sp<SkImage> image = SkImages::RasterFromData(
                    source_info, std::move(data), command.image.row_bytes());
                if (!image) return {RasterError::missing_image_pixels, executed};
                apply_image_attributes(paint, command.image_attributes);
                const RectF source{command.first.x, command.first.y,
                                   command.second.x, command.second.y};
                (*canvas).drawImageRect(image.get(), to_sk_rect(source),
                                      to_sk_rect(command.rect),
                                      sampling_for(command.state), &paint,
                                      SkCanvas::kStrict_SrcRectConstraint);
                break;
            }
            case CommandKind::draw_lines:
            case CommandKind::draw_line_segments:
                if (!configure_pen(paint, command.pen, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                (*canvas).drawPath(batch_line_path(command.points,
                                                   command.kind == CommandKind::draw_line_segments),
                                   paint);
                break;
            case CommandKind::fill_rectangles:
                if (!configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                for (const RectF& rect : command.rects) (*canvas).drawRect(to_sk_rect(rect), paint);
                break;
            case CommandKind::draw_markers:
                draw_marker_batch(*canvas, command);
                break;
            case CommandKind::fill_grid_pattern:
                draw_grid(*canvas, command);
                break;
            case CommandKind::fill_proportion: {
                if (!configure_brush(paint, command.brush, command.state)) {
                    return {RasterError::invalid_argument, executed};
                }
                const RectF part = proportion_part(command.rect, command.scalar, command.direction);
                if (part.empty() || command.rect.empty()) break;
                // The track's clip stays on the canvas: the next command
                // sets its state again from the base.
                applied = nullptr;
                (*canvas).clipRRect(track_shape(command.rect, command.radii), SkClipOp::kIntersect, true);
                (*canvas).drawRect(to_sk_rect(part), paint);
                break;
            }
            default:
                // Not a CommandKind: the list cannot be replayed.
                return {RasterError::invalid_argument, executed};
            }
            ++executed;
        }
        unlock_target.release();
        return {RasterError::none, executed};
    } catch (const std::invalid_argument&) {
        return {RasterError::invalid_argument};
    } catch (const std::logic_error&) {
        return {RasterError::invalid_argument};
    } catch (...) {
        return {RasterError::internal_error};
    }
}

DecodeResult SkiaExecutor::decode_png(std::span<const std::byte> encoded,
                                      const PngCodecLimits& limits) {
    if (!owner_thread()) return DecodeResult::failure(RasterError::wrong_thread);
    if (encoded.empty()) return DecodeResult::failure(RasterError::decode_failed);
    if (encoded.size() > limits.maximum_encoded_bytes) {
        return DecodeResult::failure(RasterError::encoded_limit_exceeded);
    }
    try {
        // The shared owned decode (src/core/image/png_pixels), as on every
        // host: premultiplied BGRA straight into the bitmap. iCCP is refused
        // for this purpose (owner decision D1, option 2).
        gui_forms::image::PngPixelRequest request{};
        request.limits = gui_forms::image::png_limits_for_pixels(limits.maximum_pixels, encoded.size());
        request.limits.dimension = std::min<std::uint32_t>(
            std::max(limits.maximum_width, limits.maximum_height), 0x7fffffffU);
        request.target = {gui_forms::image::PixelOrder::bgra,
                          gui_forms::image::PixelAlpha::premultiplied};
        request.purpose = gui_forms::image::PngPixelPurpose::drawing_decode;
        gui_forms::image::PngPixelDecoder decoder{};
        const gui_forms::image::PngPixelResult inspected = decoder.inspect(encoded, request);
        if (inspected.error != gui_forms::image::PngPixelError::none) {
            const bool too_large = inspected.decode_error == gui_forms::image::PngDecodeError::resource_limit;
            return DecodeResult::failure(too_large ? RasterError::dimension_limit_exceeded
                                                   : RasterError::decode_failed);
        }
        if (inspected.width > limits.maximum_width || inspected.height > limits.maximum_height) {
            return DecodeResult::failure(RasterError::dimension_limit_exceeded);
        }
        std::unique_ptr<gui_drawing::Bitmap> bitmap = std::make_unique<Bitmap>(
            inspected.width, inspected.height, PixelFormat::bgra32_premultiplied);
        const BitmapLockView lock = (*bitmap).lock(BitmapLockMode::write);
        BitmapUnlock unlock(*bitmap, lock.token);
        const std::size_t byte_count = lock.row_bytes * static_cast<std::size_t>(lock.height);
        const std::span<std::byte> pixels(static_cast<std::byte*>(lock.writable_data), byte_count);
        const gui_forms::image::PngPixelResult decoded =
            decoder.decode(encoded, request, pixels, lock.row_bytes);
        if (decoded.error != gui_forms::image::PngPixelError::none) {
            return DecodeResult::failure(RasterError::decode_failed);
        }
        unlock.release();
        return DecodeResult::success(std::move(bitmap));
    } catch (const std::length_error&) {
        return DecodeResult::failure(RasterError::dimension_limit_exceeded);
    } catch (...) {
        return DecodeResult::failure(RasterError::internal_error);
    }
}

std::vector<std::byte> SkiaExecutor::encode_png(
    const Bitmap& bitmap, RasterError* error, const PngCodecLimits& limits) {
    if (!owner_thread()) {
        assign_raster_error(error, RasterError::wrong_thread);
        return {};
    }
    try {
        const ImageSnapshot snapshot = bitmap.snapshot();
        if (snapshot.width > limits.maximum_width ||
            snapshot.height > limits.maximum_height ||
            static_cast<std::uint64_t>(snapshot.width) * snapshot.height >
                limits.maximum_pixels) {
            assign_raster_error(error, RasterError::dimension_limit_exceeded);
            return {};
        }
        // The shared owned encoder, as on every host; level 6 matches the
        // former SkPngEncoder setting and the encoder's default.
        gui_forms::image::PngEncodeInput input{};
        input.width = snapshot.width;
        input.height = snapshot.height;
        input.row_bytes = snapshot.row_bytes();
        input.pixels = snapshot.pixels();
        input.order = gui_forms::image::PngChannelOrder::bgra;
        input.alpha = gui_forms::image::PngSourceAlpha::premultiplied;
        input.compression_level = gui_forms::image::default_png_compression_level;
        std::vector<std::byte> result;
        if (!gui_forms::image::encode_png(input, result) || result.empty()) {
            assign_raster_error(error, RasterError::encode_failed);
            return {};
        }
        if (result.size() > limits.maximum_encoded_bytes) {
            assign_raster_error(error, RasterError::encoded_limit_exceeded);
            return {};
        }
        assign_raster_error(error, RasterError::none);
        return result;
    } catch (const std::logic_error&) {
        assign_raster_error(error, RasterError::invalid_argument);
        return {};
    } catch (...) {
        assign_raster_error(error, RasterError::internal_error);
        return {};
    }
}

bool SkiaExecutor::owner_thread() const noexcept {
    return std::this_thread::get_id() == owner_thread_;
}

} // namespace gui_drawing::render
