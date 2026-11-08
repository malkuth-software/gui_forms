#include "replay_display_chunk.hpp"
#include "../../text/prepared/prepared_storage.hpp"

namespace gui_forms::detail {

namespace {

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
    case DisplayOperation::clip_rounded_rect_corners: {
        PaintCornerRadii radii{};
        radii.top_left = command.first.x;
        radii.top_right = command.first.y;
        radii.bottom_right = command.second.x;
        radii.bottom_left = command.second.y;
        painter.clip_rounded_rect_corners(command.rect, radii);
        return;
    }
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

} // namespace gui_forms::detail
