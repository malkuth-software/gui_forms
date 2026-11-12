#include "recording_painter.hpp"
#include "../../text/prepared/prepared_storage.hpp"
#include "../../engine/primitives/material/material_raster.hpp"
#include "../../theme/theme/material_layers.hpp"
#include <cmath>
#include <memory>
#include <new>

#include <stdexcept>
#include <utility>

namespace gui_forms::detail {

namespace {

// Whether a new command may take the renderer cache of `previous`: the same
// horizontal text in the same font, or a box shadow of the same size with
// every other parameter equal. A shadow's coverage does not depend on where
// it lies (the renderer's memo checks the device scale and subpixel phase),
// so a shadow that moved, as a panned canvas's sheet does every step, keeps
// its cache. The memo a renderer keeps in the cache validates its own inputs
// as well; this keeps a cache from following a different drawing.
bool same_cached_drawing(const DisplayCommand& previous, const DisplayCommand& recorded) noexcept {
    if (previous.operation != recorded.operation || !previous.retained_cache) return false;
    if (recorded.operation == DisplayOperation::draw_text_utf8) {
        const bool same = previous.text_orientation == TextOrientation::horizontal &&
                          recorded.text_orientation == TextOrientation::horizontal &&
                          previous.font == recorded.font && previous.text == recorded.text;
        return same;
    }
    if (recorded.operation == DisplayOperation::draw_box_shadow) {
        const bool same = previous.rect.width == recorded.rect.width &&
                          previous.rect.height == recorded.rect.height && previous.first == recorded.first &&
                          previous.color == recorded.color && previous.scalar == recorded.scalar &&
                          previous.secondary_scalar == recorded.secondary_scalar &&
                          previous.tertiary_scalar == recorded.tertiary_scalar;
        return same;
    }
    return false;
}

} // namespace

void RecordingPainter::inherit_caches(std::shared_ptr<const DisplayChunk> previous) {
    previous_text_.clear();
    previous_shadows_.clear();
    previous_materials_.clear();
    text_ordinal_ = 0U;
    shadow_ordinal_ = 0U;
    material_ordinal_ = 0U;
    previous_ = std::move(previous);
    if (!previous_) return;
    const std::vector<DisplayCommand>& commands = (*previous_).commands();
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const DisplayCommand& command = commands[index];
        if (command.operation == DisplayOperation::draw_text_utf8 &&
            command.text_orientation == TextOrientation::horizontal) {
            previous_text_.push_back(index);
        } else if (command.operation == DisplayOperation::draw_box_shadow) {
            previous_shadows_.push_back(index);
        } else if (command.operation == DisplayOperation::draw_part_raster && command.surface_material) {
            previous_materials_.push_back(index);
        }
    }
}

std::shared_ptr<RetainedDrawCache> RecordingPainter::inherited_cache(
    const DisplayCommand& recorded, const std::vector<std::size_t>& previous_indices,
    std::size_t& ordinal) const {
    const std::size_t at = ordinal;
    ++ordinal;
    if (!previous_ || at >= previous_indices.size()) return {};
    const DisplayCommand& previous = (*previous_).commands()[previous_indices[at]];
    if (!same_cached_drawing(previous, recorded)) return {};
    return previous.retained_cache;
}

PreparedTextPaintResult RecordingPainter::draw_prepared_text(const PreparedTextLayout& layout,
    const LayoutAuthority expected, const Point baseline, const Color color) {
    PreparedTextPaintResult result{};
    const std::shared_ptr<const PreparedTextStorage>& storage = PreparedTextAccess::layout(layout);
    if (!storage || !std::isfinite(baseline.x) || !std::isfinite(baseline.y)) {
        result.status = PreparedTextStatus::invalid_input;
    } else if (!prepared_authority_current(*storage, expected)) {
        result.status = PreparedTextStatus::stale;
    } else {
        try {
            DisplayCommand command{};
            command.operation = DisplayOperation::draw_prepared_text;
            command.first = baseline;
            command.color = color;
            command.prepared_text = storage;
            command.prepared_authority = expected;
            commands_.push_back(std::move(command));
            result.disposition = PreparedTextPaintDisposition::recorded;
            result.status = PreparedTextStatus::success;
            return result;
        } catch (const std::bad_alloc&) { result.status = PreparedTextStatus::resource_failure; }
    }
    // A caller ignoring refusal cannot publish an incomplete candidate chunk.
    if (!prepared_failure_) prepared_failure_ = result.status;
    return result;
}

void RecordingPainter::save() {
    DisplayCommand command;
    command.operation = DisplayOperation::save;
    saved_origins_.reserve(saved_origins_.size() + 1U);
    commands_.push_back(std::move(command));
    saved_origins_.push_back(origin_);
    ++save_depth_;
}

void RecordingPainter::restore() {
    if (save_depth_ == 0U) {
        throw std::logic_error("GUI.Forms display chunk restored without a matching save");
    }
    DisplayCommand command;
    command.operation = DisplayOperation::restore;
    commands_.push_back(std::move(command));
    --save_depth_;
    origin_ = saved_origins_.back();
    saved_origins_.pop_back();
}

void RecordingPainter::translate(Point offset) {
    DisplayCommand command;
    command.operation = DisplayOperation::translate;
    command.first = offset;
    commands_.push_back(std::move(command));
    origin_ = Point{origin_.x + offset.x, origin_.y + offset.y};
}

void RecordingPainter::clip_rect(Rect rect) {
    if (save_depth_ == 0U) clipped_ = true;
    DisplayCommand command;
    command.operation = DisplayOperation::clip_rect;
    command.rect = rect;
    commands_.push_back(std::move(command));
}

void RecordingPainter::clip_rounded_rect(Rect rect, double radius) {
    if (save_depth_ == 0U) clipped_ = true;
    DisplayCommand command;
    command.operation = DisplayOperation::clip_rounded_rect;
    command.rect = rect;
    command.scalar = radius;
    commands_.push_back(std::move(command));
}

void RecordingPainter::clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) {
    if (save_depth_ == 0U) clipped_ = true;
    DisplayCommand command;
    command.operation = DisplayOperation::clip_rounded_rect_corners;
    command.rect = rect;
    command.first = Point{radii.top_left, radii.top_right};
    command.second = Point{radii.bottom_right, radii.bottom_left};
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_rect(Rect rect, Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::fill_rect;
    command.rect = rect;
    command.color = color;
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_rounded_rect(Rect rect, double radius, Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::fill_rounded_rect;
    command.rect = rect;
    command.color = color;
    command.scalar = radius;
    commands_.push_back(std::move(command));
}

void RecordingPainter::stroke_rect(Rect rect, Color color, double width) {
    DisplayCommand command;
    command.operation = DisplayOperation::stroke_rect;
    command.rect = rect;
    command.color = color;
    command.scalar = width;
    commands_.push_back(std::move(command));
}

void RecordingPainter::stroke_rounded_rect(Rect rect, double radius, Color color,
                                           double width) {
    DisplayCommand command;
    command.operation = DisplayOperation::stroke_rounded_rect;
    command.rect = rect;
    command.color = color;
    command.scalar = radius;
    command.secondary_scalar = width;
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_linear_gradient(
    Rect rect, Point start, Point end,
    std::span<const GradientStop> stops) {
    if (!valid_gradient_stops(stops)) {
        throw std::invalid_argument("display gradient stops are invalid");
    }
    DisplayCommand command;
    command.operation = DisplayOperation::fill_linear_gradient;
    command.rect = rect;
    command.first = start;
    command.second = end;
    command.gradient_stops.assign(stops.begin(), stops.end());
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_linear_gradient_spread(
    Rect rect, Point start, Point end,
    std::span<const GradientStop> stops, GradientSpreadMode spread) {
    if (!valid_gradient_stops(stops) ||
        (spread != GradientSpreadMode::pad &&
         spread != GradientSpreadMode::repeat &&
         spread != GradientSpreadMode::reflect)) {
        throw std::invalid_argument("display gradient spread is invalid");
    }
    DisplayCommand command;
    command.operation = DisplayOperation::fill_linear_gradient_spread;
    command.rect = rect;
    command.first = start;
    command.second = end;
    command.gradient_stops.assign(stops.begin(), stops.end());
    command.gradient_spread = spread;
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_radial_gradient(
    Rect rect, Point center, Size radii,
    std::span<const GradientStop> stops) {
    if (!valid_gradient_stops(stops)) {
        throw std::invalid_argument("display gradient stops are invalid");
    }
    DisplayCommand command;
    command.operation = DisplayOperation::fill_radial_gradient;
    command.rect = rect;
    command.first = center;
    command.second = {radii.width, radii.height};
    command.gradient_stops.assign(stops.begin(), stops.end());
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_box_shadow(Rect rect, double corner_radius,
                                       Point offset, double blur_radius,
                                       double spread, Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_box_shadow;
    command.rect = rect;
    command.first = offset;
    command.color = color;
    command.scalar = corner_radius;
    command.secondary_scalar = blur_radius;
    command.tertiary_scalar = spread;
    command.retained_cache = inherited_cache(command, previous_shadows_, shadow_ordinal_);
    if (!command.retained_cache) command.retained_cache = std::make_shared<RetainedDrawCache>();
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_retained_box_shadow(
    const Rect rect, const double corner_radius, const Point offset, const double blur_radius,
    const double spread, const Color color, const std::shared_ptr<RetainedDrawCache>& cache) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_box_shadow;
    command.rect = rect;
    command.first = offset;
    command.color = color;
    command.scalar = corner_radius;
    command.secondary_scalar = blur_radius;
    command.tertiary_scalar = spread;
    command.retained_cache = cache;
    // The caller's cache wins; the ordinal still counts this shadow, so the
    // ones after it pair with the previous chunk's in order.
    const std::shared_ptr<RetainedDrawCache> inherited =
        inherited_cache(command, previous_shadows_, shadow_ordinal_);
    if (!command.retained_cache) command.retained_cache = inherited;
    if (!command.retained_cache) command.retained_cache = std::make_shared<RetainedDrawCache>();
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_inset_box_shadow(
    Rect rect, double corner_radius, Point offset, double blur_radius,
    double spread, Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_inset_box_shadow;
    command.rect = rect;
    command.first = offset;
    command.color = color;
    command.scalar = corner_radius;
    command.secondary_scalar = blur_radius;
    command.tertiary_scalar = spread;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_line(Point from, Point to, Color color, double width) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_line;
    command.first = from;
    command.second = to;
    command.color = color;
    command.scalar = width;
    commands_.push_back(std::move(command));
}

namespace {

// A batched command with an empty payload of its own: the caller fills the
// spans and style it names.
DisplayCommand marks_command(DisplayOperation operation, std::shared_ptr<DisplayMarks>& marks) {
    marks = std::make_shared<DisplayMarks>();
    DisplayCommand command;
    command.operation = operation;
    return command;
}

} // namespace

void RecordingPainter::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::stroke_polyline, marks);
    (*marks).points.assign(points.begin(), points.end());
    (*marks).stroke = style;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_polygon, marks);
    (*marks).points.assign(points.begin(), points.end());
    (*marks).fill_rule = rule;
    command.color = color;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_band, marks);
    (*marks).points.assign(upper.begin(), upper.end());
    (*marks).lower_points.assign(lower.begin(), lower.end());
    command.color = color;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_rects(std::span<const Rect> rects, Color color) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_rects, marks);
    (*marks).rects.assign(rects.begin(), rects.end());
    command.color = color;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_rects_each(std::span<const Rect> rects, std::span<const Color> colors) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_rects_each, marks);
    (*marks).rects.assign(rects.begin(), rects.end());
    (*marks).colors.assign(colors.begin(), colors.end());
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::stroke_segments, marks);
    (*marks).segments.assign(segments.begin(), segments.end());
    (*marks).stroke = style;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_markers(std::span<const Point> centers, const MarkerStyle& style) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::draw_markers, marks);
    (*marks).points.assign(centers.begin(), centers.end());
    (*marks).marker = style;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_conic_gradient(Rect rect, Point center, double start_degrees,
                                           std::span<const GradientStop> stops) {
    if (!valid_gradient_stops(stops)) {
        throw std::invalid_argument("display gradient stops are invalid");
    }
    DisplayCommand command;
    command.operation = DisplayOperation::fill_conic_gradient;
    command.rect = rect;
    command.first = center;
    command.scalar = start_degrees;
    command.gradient_stops.assign(stops.begin(), stops.end());
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_checker(Rect rect, const CheckerPattern& pattern) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_checker, marks);
    (*marks).checker = pattern;
    command.rect = rect;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_grid_pattern(Rect rect, const GridPattern& pattern) {
    std::shared_ptr<DisplayMarks> marks;
    DisplayCommand command = marks_command(DisplayOperation::fill_grid_pattern, marks);
    (*marks).grid = pattern;
    command.rect = rect;
    command.marks = std::move(marks);
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::fill_rounded_rect_corners;
    command.rect = rect;
    command.first = Point{radii.top_left, radii.top_right};
    command.second = Point{radii.bottom_right, radii.bottom_left};
    command.color = color;
    commands_.push_back(std::move(command));
}

void RecordingPainter::stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width) {
    DisplayCommand command;
    command.operation = DisplayOperation::stroke_rounded_rect_corners;
    command.rect = rect;
    command.first = Point{radii.top_left, radii.top_right};
    command.second = Point{radii.bottom_right, radii.bottom_left};
    command.color = color;
    command.scalar = width;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_text_utf8(Point origin,
                                      std::string_view text,
                                      FontSpec font,
                                      Color color) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_text_utf8;
    command.first = origin;
    command.color = color;
    command.font = font;
    command.text.assign(text);
    command.retained_cache = inherited_cache(command, previous_text_, text_ordinal_);
    if (!command.retained_cache) command.retained_cache = std::make_shared<RetainedDrawCache>();
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_retained_text_utf8(
    const Point origin, const std::string_view text, const FontSpec font,
    const Color color, const std::shared_ptr<RetainedDrawCache>& cache) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_text_utf8;
    command.first = origin;
    command.color = color;
    command.font = font;
    command.text.assign(text);
    command.retained_cache = cache;
    // As draw_retained_box_shadow: the caller's cache wins, and the ordinal
    // counts this run.
    const std::shared_ptr<RetainedDrawCache> inherited =
        inherited_cache(command, previous_text_, text_ordinal_);
    if (!command.retained_cache) command.retained_cache = inherited;
    if (!command.retained_cache) command.retained_cache = std::make_shared<RetainedDrawCache>();
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_turned_text_utf8(const Point origin, const std::string_view text,
                                             const FontSpec font, const Color color,
                                             const TextOrientation orientation) {
    if (orientation == TextOrientation::horizontal) {
        draw_text_utf8(origin, text, font, color);
        return;
    }
    DisplayCommand command;
    command.operation = DisplayOperation::draw_text_utf8;
    command.first = origin;
    command.color = color;
    command.font = font;
    command.text.assign(text);
    command.text_orientation = orientation;
    commands_.push_back(std::move(command));
}

Size RecordingPainter::measure_text_utf8(std::string_view text,
                                         FontSpec font) {
    return resolve_text_layout_utf8(text, font).logical_size;
}

ResolvedTextLayout RecordingPainter::resolve_text_layout_utf8(
    std::string_view text, FontSpec font) {
    return text_metrics_ != nullptr
        ? (*text_metrics_).resolve_text_layout_utf8(text, font)
        : estimate_text_layout_utf8(text, font);
}

TextExtent RecordingPainter::measure_text_extent_utf8(
    std::string_view text, FontSpec font) {
    if (text_metrics_ == nullptr) return text_extent_of(estimate_text_layout_utf8(text, font));
    const TextExtent extent = (*text_metrics_).measure_text_extent_utf8(text, font);
    return extent;
}

TextResolutionStatus RecordingPainter::fill_line_break_opportunities_utf8(
    std::string_view text, std::span<LineBreakOpportunity> opportunities) {
    if (text_metrics_ == nullptr) return estimate_line_break_opportunities_utf8(text, opportunities);
    const TextResolutionStatus status =
        (*text_metrics_).fill_line_break_opportunities_utf8(text, opportunities);
    return status;
}

TextResolutionStatus RecordingPainter::fill_cluster_advances_utf8(
    std::string_view text, FontSpec font, std::span<double> advances) {
    if (text_metrics_ == nullptr) return estimate_cluster_advances_utf8(text, font, advances);
    const TextResolutionStatus status = (*text_metrics_).fill_cluster_advances_utf8(text, font, advances);
    return status;
}

void RecordingPainter::draw_image(ImageId image, Rect destination, double opacity) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_image;
    command.rect = destination;
    command.image = image;
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_live_surface(std::shared_ptr<LiveSurface> surface,
                                         Rect destination, double opacity) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_live_surface;
    command.live_surface = std::move(surface);
    command.rect = destination;
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin,
                                        double opacity) {
    if (!raster) return;
    const DeviceExtent extent = (*raster).extent();
    const double scale = (*raster).scale().value;
    DisplayCommand command;
    command.operation = DisplayOperation::draw_part_raster;
    command.part_raster = raster;
    command.first = origin;
    command.rect = {origin.x, origin.y, static_cast<double>(extent.width) / scale,
                    static_cast<double>(extent.height) / scale};
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_surface_material(Rect bounds, const SurfaceMaterial& authored) {
    if (!raster_scale_ || material_painting(authored) == MaterialPainting::layers) {
        paint_material_layers(*this, bounds, authored);
        return;
    }
    const std::optional<SurfaceMaterial> stripped = material_as_painted(authored);
    const SurfaceMaterial& material = stripped ? *stripped : authored;
    const Size box{bounds.width, bounds.height};
    // The previous chunk's raster at this ordinal, when it was baked from
    // the same material over a box of the same size at the same scale.
    std::shared_ptr<const PartRaster> raster{};
    std::shared_ptr<const SurfaceMaterial> kept{};
    const std::size_t at = material_ordinal_;
    ++material_ordinal_;
    if (previous_ && at < previous_materials_.size()) {
        const DisplayCommand& previous = (*previous_).commands()[previous_materials_[at]];
        const bool same = previous.part_raster && (*previous.part_raster).scale() == *raster_scale_ &&
                          previous.second.x == box.width && previous.second.y == box.height &&
                          *previous.surface_material == material;
        if (same) {
            raster = previous.part_raster;
            kept = previous.surface_material;
        }
    }
    const double outset = material_raster_outset(material);
    if (!raster) {
        raster = bake_material_raster(material, box, *raster_scale_, outset);
        if (!raster) {
            paint_material_layers(*this, bounds, authored);
            return;
        }
        kept = std::make_shared<const SurfaceMaterial>(material);
    }
    draw_part_raster(raster, Point{bounds.x - outset, bounds.y - outset}, 1.0);
    DisplayCommand& recorded = commands_.back();
    recorded.surface_material = std::move(kept);
    recorded.second = Point{box.width, box.height};
    recorded.secondary_scalar = outset;
}

void RecordingPainter::draw_baked_surface_material(Rect bounds,
                                                   const std::shared_ptr<const SurfaceMaterial>& material,
                                                   const std::shared_ptr<const PartRaster>& raster,
                                                   double outset) {
    if (!material) return;
    if (!raster) {
        draw_surface_material(bounds, *material);
        return;
    }
    // Counted as a material raster, so the ones recorded after it pair with
    // the previous chunk's in order.
    ++material_ordinal_;
    draw_part_raster(raster, Point{bounds.x - outset, bounds.y - outset}, 1.0);
    DisplayCommand& recorded = commands_.back();
    recorded.surface_material = material;
    recorded.second = Point{bounds.width, bounds.height};
    recorded.secondary_scalar = outset;
}

void RecordingPainter::draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                               double opacity) {
    if (!raster) return;
    DisplayCommand command;
    command.operation = DisplayOperation::draw_part_raster_scaled;
    command.part_raster = raster;
    command.first = Point{destination.x, destination.y};
    command.rect = destination;
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_image_region(ImageId image, Rect source,
                                          Rect destination, double opacity) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_image_region;
    command.first = {source.x, source.y};
    command.second = {source.width, source.height};
    command.rect = destination;
    command.image = image;
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::draw_image_region_sampled(
    ImageId image, Rect source, Rect destination, ImageSampling sampling,
    double opacity) {
    DisplayCommand command;
    command.operation = DisplayOperation::draw_image_region_sampled;
    command.first = {source.x, source.y};
    command.second = {source.width, source.height};
    command.rect = destination;
    command.image = image;
    command.image_sampling = sampling;
    command.scalar = opacity;
    commands_.push_back(std::move(command));
}

void RecordingPainter::fill_image_pattern(
    ImageId image, Size source_pixel_size, Rect destination,
    Size logical_tile_size, ImagePatternWrap wrap, double opacity) {
    DisplayCommand command;
    command.operation = DisplayOperation::fill_image_pattern;
    command.first = {source_pixel_size.width, source_pixel_size.height};
    command.second = {logical_tile_size.width, logical_tile_size.height};
    command.rect = destination;
    command.image = image;
    command.scalar = opacity;
    command.image_pattern_wrap = wrap;
    commands_.push_back(std::move(command));
}

void RecordingPainter::begin_group(const PaintGroup& group) {
    // A copy group replaces what lies beneath it, transparent included.
    if (group.composite == PaintGroupComposite::copy) opaque_cover_.reset();
    OpenGroup opened{};
    opened.save_depth = save_depth_;
    if (group_depth_ >= maximum_paint_group_depth) {
        // Refused by name: drawn without isolation, limited to its bounds.
        open_groups_.reserve(open_groups_.size() + 1U);
        save();
        clip_rect(group.bounds);
        opened.refused = true;
        opened.save_depth = save_depth_;
        open_groups_.push_back(opened);
        group_refusals_ = group_refusals_ + 1U;
        return;
    }
    DisplayCommand command;
    command.operation = DisplayOperation::begin_group;
    command.rect = group.bounds;
    command.scalar = group.opacity;
    command.secondary_scalar = static_cast<double>(group.composite);
    command.tertiary_scalar = group.isolated ? 1.0 : 0.0;
    open_groups_.reserve(open_groups_.size() + 1U);
    commands_.push_back(std::move(command));
    open_groups_.push_back(opened);
    group_depth_ = group_depth_ + 1U;
}

void RecordingPainter::end_group() {
    if (open_groups_.empty()) {
        throw std::logic_error("GUI.Forms paint group ended without one begun");
    }
    const OpenGroup closing = open_groups_.back();
    if (closing.save_depth != save_depth_) {
        throw std::logic_error("GUI.Forms paint group has unbalanced saves inside it");
    }
    open_groups_.pop_back();
    if (closing.refused) {
        restore();
        return;
    }
    DisplayCommand command;
    command.operation = DisplayOperation::end_group;
    commands_.push_back(std::move(command));
    group_depth_ = group_depth_ - 1U;
}

std::shared_ptr<const DisplayChunk> RecordingPainter::finish(
    std::uint64_t generation, PaintPlane plane, Rect logical_bounds) {
    if (prepared_failure_) throw PreparedTextPaintFailure(*prepared_failure_);
    if (save_depth_ != 0U) {
        throw std::logic_error("GUI.Forms display chunk has an unbalanced painter save");
    }
    if (!open_groups_.empty()) {
        throw std::logic_error("GUI.Forms display chunk leaves a paint group open");
    }
    return std::make_shared<const DisplayChunk>(generation, plane, logical_bounds,
                                                std::move(commands_), opaque_cover_);
}

void RecordingPainter::mark_opaque_cover(const Rect rect) noexcept {
    const bool top_level = save_depth_ == 0U && open_groups_.empty() && !clipped_ && origin_ == Point{};
    if (!top_level || rect.empty() || !rect.finite()) return;
    // Everything drawn since composites over the earlier cover, which stays
    // opaque: keep the larger of the two.
    if (opaque_cover_ && (*opaque_cover_).area() >= rect.area()) return;
    opaque_cover_ = rect;
}

} // namespace gui_forms::detail
