#include "replay_display_chunk.hpp"
#include "../../text/prepared/prepared_storage.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace gui_forms::detail {

namespace {

// The per-corner radii a command keeps in `first` and `second`.
PaintCornerRadii recorded_radii(const DisplayCommand& command) noexcept {
    PaintCornerRadii radii{};
    radii.top_left = command.first.x;
    radii.top_right = command.first.y;
    radii.bottom_right = command.second.x;
    radii.bottom_left = command.second.y;
    return radii;
}

// Replays one batched mark or pattern; one without its payload draws
// nothing.
void replay_marks(const DisplayCommand& command, Painter& painter) {
    if (command.operation == DisplayOperation::fill_conic_gradient) {
        painter.fill_conic_gradient(command.rect, command.first, command.scalar, command.gradient_stops);
        return;
    }
    if (command.operation == DisplayOperation::fill_rounded_rect_corners) {
        painter.fill_rounded_rect_corners(command.rect, recorded_radii(command), command.color);
        return;
    }
    if (command.operation == DisplayOperation::stroke_rounded_rect_corners) {
        painter.stroke_rounded_rect_corners(command.rect, recorded_radii(command), command.color, command.scalar);
        return;
    }
    if (!command.marks) return;
    const DisplayMarks& marks = *command.marks;
    switch (command.operation) {
    case DisplayOperation::stroke_polyline: painter.stroke_polyline(marks.points, marks.stroke); return;
    case DisplayOperation::fill_polygon: painter.fill_polygon(marks.points, command.color, marks.fill_rule); return;
    case DisplayOperation::fill_band: painter.fill_band(marks.points, marks.lower_points, command.color); return;
    case DisplayOperation::fill_rects: painter.fill_rects(marks.rects, command.color); return;
    case DisplayOperation::fill_rects_each: painter.fill_rects_each(marks.rects, marks.colors); return;
    case DisplayOperation::stroke_segments: painter.stroke_segments(marks.segments, marks.stroke); return;
    case DisplayOperation::draw_markers: painter.draw_markers(marks.points, marks.marker); return;
    case DisplayOperation::fill_checker: painter.fill_checker(command.rect, marks.checker); return;
    case DisplayOperation::fill_grid_pattern: painter.fill_grid_pattern(command.rect, marks.grid); return;
    default:
        // Not a batched operation: replay_command handles it.
        return;
    }
}

// Replays one recorded command onto `painter`.
void replay_command(const DisplayCommand& command, Painter& painter) {
    switch (command.operation) {
    case DisplayOperation::draw_prepared_text: {
        if (!command.prepared_text || !prepared_authority_current(*command.prepared_text, command.prepared_authority)) {
            throw PreparedTextPaintFailure(PreparedTextStatus::stale);
        }
        PreparedTextLayout layout = PreparedTextAccess::retained_layout(command.prepared_text);
        const PreparedTextPaintResult result = painter.draw_prepared_text(layout, command.prepared_authority,
            command.first, command.color);
        if (result.status != PreparedTextStatus::success || result.disposition == PreparedTextPaintDisposition::refused) {
            throw PreparedTextPaintFailure(result.status);
        }
        return;
    }
    case DisplayOperation::save: painter.save(); return;
    case DisplayOperation::restore: painter.restore(); return;
    case DisplayOperation::translate: painter.translate(command.first); return;
    case DisplayOperation::clip_rect: painter.clip_rect(command.rect); return;
    case DisplayOperation::clip_rounded_rect:
        painter.clip_rounded_rect(command.rect, command.scalar);
        return;
    case DisplayOperation::clip_rounded_rect_corners:
        painter.clip_rounded_rect_corners(command.rect, recorded_radii(command));
        return;
    case DisplayOperation::stroke_polyline:
    case DisplayOperation::fill_polygon:
    case DisplayOperation::fill_band:
    case DisplayOperation::fill_rects:
    case DisplayOperation::fill_rects_each:
    case DisplayOperation::stroke_segments:
    case DisplayOperation::draw_markers:
    case DisplayOperation::fill_conic_gradient:
    case DisplayOperation::fill_checker:
    case DisplayOperation::fill_grid_pattern:
    case DisplayOperation::fill_rounded_rect_corners:
    case DisplayOperation::stroke_rounded_rect_corners:
        replay_marks(command, painter);
        return;
    case DisplayOperation::fill_rect:
        painter.fill_rect(command.rect, command.color);
        return;
    case DisplayOperation::fill_rounded_rect:
        painter.fill_rounded_rect(command.rect, command.scalar,
                                  command.color);
        return;
    case DisplayOperation::stroke_rect:
        painter.stroke_rect(command.rect, command.color, command.scalar);
        return;
    case DisplayOperation::stroke_rounded_rect:
        painter.stroke_rounded_rect(command.rect, command.scalar,
                                    command.color,
                                    command.secondary_scalar);
        return;
    case DisplayOperation::fill_linear_gradient:
        painter.fill_linear_gradient(command.rect, command.first,
                                     command.second,
                                     command.gradient_stops);
        return;
    case DisplayOperation::fill_linear_gradient_spread:
        painter.fill_linear_gradient_spread(
            command.rect, command.first, command.second,
            command.gradient_stops, command.gradient_spread);
        return;
    case DisplayOperation::fill_radial_gradient:
        painter.fill_radial_gradient(
            command.rect, command.first,
            {command.second.x, command.second.y}, command.gradient_stops);
        return;
    case DisplayOperation::draw_box_shadow:
        if (command.retained_cache) {
            painter.draw_retained_box_shadow(
                command.rect, command.scalar, command.first,
                command.secondary_scalar, command.tertiary_scalar,
                command.color, command.retained_cache);
        } else {
            painter.draw_box_shadow(
                command.rect, command.scalar, command.first,
                command.secondary_scalar, command.tertiary_scalar,
                command.color);
        }
        return;
    case DisplayOperation::draw_inset_box_shadow:
        painter.draw_inset_box_shadow(
            command.rect, command.scalar, command.first,
            command.secondary_scalar, command.tertiary_scalar,
            command.color);
        return;
    case DisplayOperation::draw_line:
        painter.draw_line(command.first, command.second, command.color,
                          command.scalar);
        return;
    case DisplayOperation::draw_text_utf8:
        if (command.text_orientation != TextOrientation::horizontal) {
            painter.draw_turned_text_utf8(command.first, command.text, command.font,
                                          command.color, command.text_orientation);
        } else if (command.retained_cache) {
            painter.draw_retained_text_utf8(command.first, command.text, command.font,
                                            command.color, command.retained_cache);
        } else {
            painter.draw_text_utf8(command.first, command.text, command.font,
                                   command.color);
        }
        return;
    case DisplayOperation::draw_image:
        painter.draw_image(command.image, command.rect, command.scalar);
        return;
    case DisplayOperation::draw_live_surface:
        painter.draw_live_surface(
            command.live_surface, command.rect, command.scalar);
        return;
    case DisplayOperation::draw_part_raster:
        if (command.surface_material) {
            // A baked material: its box lies `outset` inside the raster.
            const double outset = command.secondary_scalar;
            const Rect box{command.first.x + outset, command.first.y + outset, command.second.x, command.second.y};
            painter.draw_baked_surface_material(box, command.surface_material, command.part_raster, outset);
            return;
        }
        painter.draw_part_raster(command.part_raster, command.first, command.scalar);
        return;
    case DisplayOperation::draw_part_raster_scaled:
        painter.draw_part_raster_scaled(command.part_raster, command.rect, command.scalar);
        return;
    case DisplayOperation::begin_group: {
        PaintGroup group{};
        group.bounds = command.rect;
        group.opacity = command.scalar;
        // The recording painter writes the enumerator; any other value
        // replays as source-over.
        const double composite = command.secondary_scalar;
        const bool known = composite >= 0.0 && composite <= static_cast<double>(PaintGroupComposite::copy);
        group.composite = known ? static_cast<PaintGroupComposite>(static_cast<std::uint8_t>(composite))
                                : PaintGroupComposite::source_over;
        group.isolated = command.tertiary_scalar != 0.0;
        painter.begin_group(group);
        return;
    }
    case DisplayOperation::end_group:
        painter.end_group();
        return;
    case DisplayOperation::draw_image_region:
        painter.draw_image_region(
            command.image,
            {command.first.x, command.first.y,
             command.second.x, command.second.y},
            command.rect, command.scalar);
        return;
    case DisplayOperation::draw_image_region_sampled:
        painter.draw_image_region_sampled(
            command.image,
            {command.first.x, command.first.y,
             command.second.x, command.second.y},
            command.rect, command.image_sampling, command.scalar);
        return;
    case DisplayOperation::fill_image_pattern:
        painter.fill_image_pattern(
            command.image, {command.first.x, command.first.y},
            command.rect, {command.second.x, command.second.y},
            command.image_pattern_wrap, command.scalar);
        return;
    }
    // An operation outside the enumeration has nothing to replay.
    return;
}

} // namespace

std::uint64_t replay_display_chunk(const DisplayChunk& chunk, Painter& painter) {
    for (const DisplayCommand& command : chunk.commands()) {
        replay_command(command, painter);
    }
    return chunk.commands().size();
}

std::uint64_t replay_display_chunk_occluded(const DisplayChunk& chunk, Painter& painter, const Rect clip) {
    const std::optional<DisplayOccluder>& occluder = chunk.occluder();
    const std::vector<DisplayCommand>& commands = chunk.commands();
    if (!occluder || (*occluder).group_start == 0U || clip.empty()) {
        return replay_display_chunk(chunk, painter);
    }
    const DisplayCommand& surface = commands[(*occluder).command];
    Rect cover = painter.opaque_live_surface_cover(surface.live_surface, surface.rect, surface.scalar);
    if ((*occluder).clipped) cover = Rect::intersection(cover, (*occluder).clip);
    const Rect hidden = Rect::intersection(cover, clip);
    if (hidden.empty()) return replay_display_chunk(chunk, painter);
    // The clip less the hidden rectangle: full-width bands above and below
    // it, and the parts beside it between them.
    const std::array<Rect, 4> bands{{
        {clip.x, clip.y, clip.width, hidden.y - clip.y},
        {clip.x, hidden.bottom(), clip.width, clip.bottom() - hidden.bottom()},
        {clip.x, hidden.y, hidden.x - clip.x, hidden.height},
        {hidden.right(), hidden.y, clip.right() - hidden.right(), hidden.height},
    }};
    const std::size_t beneath = (*occluder).group_start;
    std::uint64_t replayed = 0U;
    for (const Rect band : bands) {
        if (band.empty()) continue;
        painter.save();
        painter.clip_rect(band);
        for (std::size_t index = 0U; index < beneath; ++index) replay_command(commands[index], painter);
        painter.restore();
        replayed += beneath;
    }
    for (std::size_t index = beneath; index < commands.size(); ++index) replay_command(commands[index], painter);
    replayed += commands.size() - beneath;
    return replayed;
}

} // namespace gui_forms::detail
