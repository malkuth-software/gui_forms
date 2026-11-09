#include "gui_forms/visual_inspection.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace gui_forms {
namespace {

void append_json_string(std::ostringstream& output, std::string_view value) {
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) output << '?';
            else output << static_cast<char>(character);
            break;
        }
    }
    output << '"';
}

void append_point(std::ostringstream& output, Point value) {
    output << '[' << value.x << ',' << value.y << ']';
}

void append_size(std::ostringstream& output, Size value) {
    output << '[' << value.width << ',' << value.height << ']';
}

void append_rect(std::ostringstream& output, Rect value) {
    output << '[' << value.x << ',' << value.y << ',' << value.width << ','
           << value.height << ']';
}

void append_insets(std::ostringstream& output, Insets value) {
    output << '[' << value.left << ',' << value.top << ',' << value.right << ','
           << value.bottom << ']';
}

void append_color(std::ostringstream& output, Color value) {
    output << '[' << static_cast<unsigned>(value.red) << ','
           << static_cast<unsigned>(value.green) << ','
           << static_cast<unsigned>(value.blue) << ','
           << static_cast<unsigned>(value.alpha) << ']';
}

void append_font(std::ostringstream& output, FontSpec value) {
    output << "{\"role\":";
    append_json_string(output, font_role_name(value.role));
    output << ",\"size\":" << value.size << ",\"weight\":" << value.weight
           << ",\"italic\":" << (value.italic ? "true" : "false")
           << ",\"letter_spacing\":" << value.letter_spacing << '}';
}

void append_resolved_text(
    std::ostringstream& output,
    const std::optional<ResolvedTextLayout>& resolved) {
    if (!resolved) {
        output << "null";
        return;
    }
    output << "{\"status\":";
    append_json_string(output, text_resolution_status_name((*resolved).status));
    output << ",\"primary_family\":";
    append_json_string(output, (*resolved).primary_family);
    output << ",\"logical_size\":";
    append_size(output, (*resolved).logical_size);
    output << ",\"ascent\":" << (*resolved).ascent
           << ",\"descent\":" << (*resolved).descent
           << ",\"line_gap\":" << (*resolved).line_gap
           << ",\"missing_clusters\":" << (*resolved).missing_clusters
           << ",\"runs\":[";
    for (std::size_t index = 0U; index < (*resolved).runs.size(); ++index) {
        if (index != 0U) output << ',';
        const ResolvedFontRun& run = (*resolved).runs[index];
        output << "{\"utf8_start\":" << run.utf8_start
               << ",\"utf8_length\":" << run.utf8_length
               << ",\"family\":";
        append_json_string(output, run.family);
        output << ",\"weight\":" << run.registered_weight
               << ",\"italic\":" << (run.registered_italic ? "true" : "false")
               << ",\"fallback\":" << (run.fallback ? "true" : "false")
               << '}';
    }
    output << "]}";
}

void append_gradient_stops(std::ostringstream& output,
                           const std::vector<GradientStop>& stops) {
    output << '[';
    for (std::size_t index = 0U; index < stops.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"offset\":" << stops[index].offset << ",\"color\":";
        append_color(output, stops[index].color);
        output << '}';
    }
    output << ']';
}

const char* fill_kind_name(MaterialFillKind kind) noexcept {
    switch (kind) {
    case MaterialFillKind::solid: return "solid";
    case MaterialFillKind::linear_gradient: return "linear_gradient";
    case MaterialFillKind::radial_gradient: return "radial_gradient";
    case MaterialFillKind::image: return "image";
    case MaterialFillKind::conic_gradient: return "conic_gradient";
    case MaterialFillKind::procedural: return "procedural";
    }
    return "unknown";
}

const char* procedural_pattern_name(ProceduralPattern pattern) noexcept {
    switch (pattern) {
    case ProceduralPattern::felt: return "felt";
    case ProceduralPattern::wood_grain: return "wood_grain";
    case ProceduralPattern::paper_fibre: return "paper_fibre";
    case ProceduralPattern::pinstripe: return "pinstripe";
    case ProceduralPattern::brushed_metal: return "brushed_metal";
    case ProceduralPattern::fresco: return "fresco";
    case ProceduralPattern::checker: return "checker";
    }
    return "unknown";
}

const char* coordinate_space_name(MaterialCoordinateSpace value) noexcept {
    return value == MaterialCoordinateSpace::logical ? "logical" : "normalized";
}

const char* spread_name(GradientSpreadMode value) noexcept {
    switch (value) {
    case GradientSpreadMode::pad: return "pad";
    case GradientSpreadMode::repeat: return "repeat";
    case GradientSpreadMode::reflect: return "reflect";
    }
    return "unknown";
}

const char* image_mode_name(MaterialImageMode value) noexcept {
    switch (value) {
    case MaterialImageMode::stretch: return "stretch";
    case MaterialImageMode::tile: return "tile";
    case MaterialImageMode::nine_patch: return "nine_patch";
    case MaterialImageMode::single: return "single";
    }
    return "unknown";
}

const char* image_pattern_wrap_name(ImagePatternWrap value) noexcept {
    switch (value) {
    case ImagePatternWrap::tile: return "tile";
    }
    return "unknown";
}

const char* image_sampling_name(ImageSampling value) noexcept {
    switch (value) {
    case ImageSampling::nearest: return "nearest";
    case ImageSampling::linear: return "linear";
    }
    return "unknown";
}

const char* text_orientation_name(TextOrientation value) noexcept {
    switch (value) {
    case TextOrientation::horizontal: return "horizontal";
    case TextOrientation::bottom_to_top: return "bottom_to_top";
    case TextOrientation::top_to_bottom: return "top_to_bottom";
    }
    return "unknown";
}

const char* material_edge_name(MaterialEdge edge) noexcept {
    switch (edge) {
    case MaterialEdge::top: return "top";
    case MaterialEdge::right: return "right";
    case MaterialEdge::bottom: return "bottom";
    case MaterialEdge::left: return "left";
    }
    return "unknown";
}

const char* border_style_name(MaterialBorderStyle value) noexcept {
    switch (value) {
    case MaterialBorderStyle::solid: return "solid";
    case MaterialBorderStyle::dashed: return "dashed";
    case MaterialBorderStyle::dotted: return "dotted";
    }
    return "unknown";
}

void append_border(std::ostringstream& output,
                   const std::optional<MaterialBorder>& border) {
    if (!border) {
        output << "null";
        return;
    }
    output << "{\"color\":";
    append_color(output, (*border).color);
    output << ",\"width\":" << (*border).width << ",\"style\":";
    append_json_string(output, border_style_name((*border).style));
    output << '}';
}

void append_corner_radii(std::ostringstream& output,
                         const std::optional<MaterialCornerRadii>& radii) {
    if (!radii) {
        output << "null";
        return;
    }
    output << "{\"top_left\":" << (*radii).top_left
           << ",\"top_right\":" << (*radii).top_right
           << ",\"bottom_right\":" << (*radii).bottom_right
           << ",\"bottom_left\":" << (*radii).bottom_left << '}';
}

const char* blend_mode_name(MaterialBlendMode value) noexcept {
    switch (value) {
    case MaterialBlendMode::normal: return "normal";
    case MaterialBlendMode::multiply: return "multiply";
    case MaterialBlendMode::screen: return "screen";
    }
    return "unknown";
}

// The colour filter's 20 entries, row-major (MaterialColourMatrix), or null.
void append_colour_filter(std::ostringstream& output,
                          const MaterialColourFilter& filter) {
    if (!filter) {
        output << "null";
        return;
    }
    const MaterialColourMatrix& matrix = *filter;
    output << '[';
    for (std::size_t index = 0U; index < matrix.values.size(); ++index) {
        if (index != 0U) output << ',';
        output << matrix.values[index];
    }
    output << ']';
}

void append_drop_shadow(std::ostringstream& output,
                        const std::optional<MaterialDropShadow>& shadow) {
    if (!shadow) {
        output << "null";
        return;
    }
    output << "{\"offset\":";
    append_point(output, (*shadow).offset);
    output << ",\"blur_radius\":" << (*shadow).blur_radius << ",\"color\":";
    append_color(output, (*shadow).color);
    output << '}';
}

// One fill layer, of a material's fills or its mask.
void append_fill(std::ostringstream& output, const MaterialFillLayer& fill) {
    output << "{\"kind\":";
    append_json_string(output, fill_kind_name(fill.kind));
    output << ",\"coordinate_space\":";
    append_json_string(output, coordinate_space_name(fill.coordinate_space));
    output << ",\"angle_degrees\":" << fill.angle_degrees
           << ",\"period_degrees\":" << fill.period_degrees
           << ",\"color\":";
    append_color(output, fill.color);
    output << ",\"start\":";
    append_point(output, fill.start);
    output << ",\"end\":";
    append_point(output, fill.end);
    output << ",\"center\":";
    append_point(output, fill.center);
    output << ",\"radii\":";
    append_size(output, fill.radii);
    output << ",\"stops\":";
    append_gradient_stops(output, fill.stops);
    output << ",\"spread\":";
    append_json_string(output, spread_name(fill.spread));
    output << ",\"image_id\":" << fill.image.value
           << ",\"image_pixel_size\":";
    append_size(output, fill.image_pixel_size);
    output << ",\"image_slice\":";
    append_insets(output, fill.image_slice);
    output << ",\"image_scale\":" << fill.image_scale
           << ",\"opacity\":" << fill.opacity << ",\"image_mode\":";
    append_json_string(output, image_mode_name(fill.image_mode));
    output << ",\"blend\":";
    append_json_string(output, blend_mode_name(fill.blend));
    // A tiled image's position (E9.6), only for an image fill, so that
    // other fills' snapshots are unchanged.
    if (fill.kind == MaterialFillKind::image) {
        output << ",\"image_anchor\":";
        append_point(output, fill.image_anchor);
        output << ",\"image_offset\":";
        append_point(output, fill.image_offset);
    }
    // A procedural fill's parameters (E10), only for one, so that other
    // fills' snapshots are unchanged.
    if (fill.kind == MaterialFillKind::procedural) {
        const ProceduralFill& pattern = fill.procedural;
        output << ",\"procedural\":{\"pattern\":";
        append_json_string(output, procedural_pattern_name(pattern.pattern));
        output << ",\"seed\":" << pattern.seed << ",\"tile\":" << pattern.tile
               << ",\"strength\":" << pattern.strength << ",\"ground\":";
        append_color(output, pattern.ground);
        output << ",\"figure\":";
        append_color(output, pattern.figure);
        output << ",\"accent\":";
        append_color(output, pattern.accent);
        output << ",\"bloom\":";
        append_color(output, pattern.bloom);
        output << '}';
    }
    output << '}';
}

void append_material(std::ostringstream& output,
                     const SurfaceMaterial& material) {
    output << "{\"corner_radius\":" << material.corner_radius
           << ",\"corner_radii\":";
    append_corner_radii(output, material.corner_radii);
    output << ",\"fills\":[";
    for (std::size_t index = 0U; index < material.fills.size(); ++index) {
        if (index != 0U) output << ',';
        const MaterialFillLayer& fill = material.fills[index];
        append_fill(output, fill);
    }
    output << "],\"shadows\":[";
    for (std::size_t index = 0U; index < material.shadows.size(); ++index) {
        if (index != 0U) output << ',';
        const MaterialShadow& shadow = material.shadows[index];
        output << "{\"offset\":";
        append_point(output, shadow.offset);
        output << ",\"blur_radius\":" << shadow.blur_radius
               << ",\"spread\":" << shadow.spread << ",\"color\":";
        append_color(output, shadow.color);
        output << ",\"inset\":" << (shadow.inset ? "true" : "false") << '}';
    }
    output << "],\"glows\":[";
    for (std::size_t index = 0U; index < material.glows.size(); ++index) {
        if (index != 0U) output << ',';
        const MaterialGlow& glow = material.glows[index];
        output << "{\"color\":";
        append_color(output, glow.color);
        output << ",\"blur_radius\":" << glow.blur_radius
               << ",\"spread\":" << glow.spread << ",\"placement\":";
        append_json_string(output, glow.placement == MaterialGlowPlacement::over ? "over" : "under");
        output << '}';
    }
    output << "],\"border\":";
    append_border(output, material.border);
    output << ",\"border_edges\":{\"top\":";
    append_border(output, material.border_edges.top);
    output << ",\"right\":";
    append_border(output, material.border_edges.right);
    output << ",\"bottom\":";
    append_border(output, material.border_edges.bottom);
    output << ",\"left\":";
    append_border(output, material.border_edges.left);
    output << "},\"keylines\":[";
    for (std::size_t index = 0U; index < material.keylines.size(); ++index) {
        if (index != 0U) output << ',';
        const MaterialKeyline& keyline = material.keylines[index];
        output << "{\"edge\":";
        append_json_string(output, material_edge_name(keyline.edge));
        output << ",\"color\":";
        append_color(output, keyline.color);
        output << ",\"width\":" << keyline.width
               << ",\"inset\":" << keyline.inset << '}';
    }
    output << "],\"colour_filter\":";
    append_colour_filter(output, material.colour_filter);
    output << ",\"drop_shadow\":";
    append_drop_shadow(output, material.drop_shadow);
    output << ",\"filter_blur\":" << material.filter_blur;
    // A finish and a mask (E9.6), only when set, so that other materials'
    // snapshots are unchanged.
    if (material.finish != 0.0) output << ",\"finish\":" << material.finish;
    if (material.mask) {
        output << ",\"mask\":";
        append_fill(output, *material.mask);
    }
    // A reflection (T5 C7), likewise only when set.
    if (material.reflection) {
        const MaterialReflection& reflection = *material.reflection;
        output << ",\"reflection\":{\"edge\":";
        append_json_string(output, material_edge_name(reflection.edge));
        output << ",\"gap\":" << reflection.gap << ",\"reach\":" << reflection.reach
               << ",\"start_opacity\":" << reflection.start_opacity
               << ",\"end_opacity\":" << reflection.end_opacity << ",\"source\":";
        const bool backplane = reflection.source == MaterialReflectionSource::backplane;
        append_json_string(output, backplane ? "backplane" : "face");
        output << '}';
    }
    output << '}';
}

void append_operation(std::ostringstream& output,
                      const VisualPaintOperationSnapshot& operation) {
    output << "{\"index\":" << operation.command_index << ",\"operation\":";
    append_json_string(output, visual_paint_operation_name(operation.operation));
    output << ",\"first\":";
    append_point(output, operation.first);
    output << ",\"second\":";
    append_point(output, operation.second);
    output << ",\"rect\":";
    append_rect(output, operation.rect);
    output << ",\"color\":";
    append_color(output, operation.color);
    output << ",\"font\":";
    append_font(output, operation.font);
    output << ",\"resolved_text\":";
    append_resolved_text(output, operation.resolved_text);
    output << ",\"image_id\":" << operation.image.value
           << ",\"scalar\":" << operation.scalar
           << ",\"secondary_scalar\":" << operation.secondary_scalar
           << ",\"tertiary_scalar\":" << operation.tertiary_scalar
           << ",\"gradient_stops\":";
    append_gradient_stops(output, operation.gradient_stops);
    output << ",\"gradient_spread\":";
    append_json_string(output, spread_name(operation.gradient_spread));
    output << ",\"image_pattern_wrap\":";
    append_json_string(output,
                       image_pattern_wrap_name(operation.image_pattern_wrap));
    output << ",\"image_sampling\":";
    append_json_string(output, image_sampling_name(operation.image_sampling));
    output << ",\"text_byte_count\":" << operation.text_byte_count
           << ",\"text\":";
    append_json_string(output, operation.text);
    output << ",\"text_orientation\":";
    append_json_string(output, text_orientation_name(operation.text_orientation));
    if (operation.element_count != 0U) output << ",\"element_count\":" << operation.element_count;
    output << '}';
}

void append_control(std::ostringstream& output,
                    const VisualControlInspection& control) {
    output << "{\"runtime_id\":" << control.runtime_id.value
           << ",\"stable_id\":";
    append_json_string(output, control.stable_id);
    output << ",\"parent_runtime_id\":";
    if (control.parent_runtime_id) output << (*control.parent_runtime_id).value;
    else output << "null";
    output << ",\"parent_stable_id\":";
    append_json_string(output, control.parent_stable_id);
    output << ",\"depth\":" << control.depth
           << ",\"overlay_tree\":" << (control.overlay_tree ? "true" : "false")
           << ",\"layout\":{\"requested_bounds\":";
    append_rect(output, control.layout.requested_bounds);
    output << ",\"arranged_bounds\":";
    append_rect(output, control.layout.arranged_bounds);
    output << ",\"absolute_bounds\":";
    append_rect(output, control.layout.absolute_bounds);
    output << ",\"client_rectangle\":";
    append_rect(output, control.layout.client_rectangle);
    output << ",\"display_rectangle\":";
    append_rect(output, control.layout.display_rectangle);
    output << ",\"child_viewport\":";
    append_rect(output, control.layout.child_viewport);
    output << ",\"visual_bounds\":";
    append_rect(output, control.layout.visual_bounds);
    output << ",\"effective_clip\":";
    append_rect(output, control.layout.effective_clip);
    output << ",\"visual_outsets\":";
    append_insets(output, control.layout.visual_outsets);
    output << ",\"margin\":";
    append_insets(output, control.layout.margin);
    output << ",\"padding\":";
    append_insets(output, control.layout.padding);
    output << ",\"minimum_size\":";
    append_size(output, control.layout.minimum_size);
    output << ",\"maximum_size\":";
    append_size(output, control.layout.maximum_size);
    output << ",\"dock\":";
    append_json_string(output, dock_style_name(control.layout.dock));
    output << ",\"anchor_mask\":"
           << static_cast<unsigned>(control.layout.anchor)
           << ",\"auto_size\":" << (control.layout.auto_size ? "true" : "false")
           << ",\"clipped_by_ancestor\":"
           << (control.layout.clipped_by_ancestor ? "true" : "false")
           << ",\"fully_clipped\":"
           << (control.layout.fully_clipped ? "true" : "false")
           << ",\"ancestor_clip_count\":" << control.layout.ancestor_clip_count
           << ",\"transaction\":{\"suspend_depth\":"
           << control.layout.transaction.suspend_depth
           << ",\"deferred\":"
           << (control.layout.transaction.deferred ? "true" : "false")
           << ",\"requested_revision\":"
           << control.layout.transaction.requested_revision
           << ",\"committed_revision\":"
           << control.layout.transaction.committed_revision << "}},\"state\":{";
    output << "\"visible\":" << (control.state.visible ? "true" : "false")
           << ",\"layout_collapsed\":"
           << (control.state.layout_collapsed ? "true" : "false")
           << ",\"effectively_visible\":"
           << (control.state.effectively_visible ? "true" : "false")
           << ",\"enabled\":" << (control.state.enabled ? "true" : "false")
           << ",\"effectively_enabled\":"
           << (control.state.effectively_enabled ? "true" : "false")
           << ",\"focusable\":" << (control.state.focusable ? "true" : "false")
           << ",\"focused\":" << (control.state.focused ? "true" : "false")
           << ",\"focus_cue_visible\":"
           << (control.state.focus_cue_visible ? "true" : "false")
           << ",\"hovered\":" << (control.state.hovered ? "true" : "false")
           << ",\"pressed\":" << (control.state.pressed ? "true" : "false")
           << ",\"pointer_captured\":"
           << (control.state.pointer_captured ? "true" : "false")
           << ",\"hit_test_transparent\":"
           << (control.state.hit_test_transparent ? "true" : "false")
           << ",\"window_active\":"
           << (control.state.window_active ? "true" : "false")
           << ",\"initializing\":"
           << (control.state.initializing ? "true" : "false")
           << ",\"visual_status\":";
    append_json_string(output, visual_status_name(control.state.visual_status));
    using DirtyValue = std::underlying_type_t<Dirty>;
    output << "},\"dirty_mask\":" << static_cast<DirtyValue>(control.dirty)
           << ",\"subtree_dirty_mask\":"
           << static_cast<DirtyValue>(control.subtree_dirty)
           << ",\"paint_plane\":";
    append_json_string(output, paint_plane_name(control.paint_plane));
    output << ",\"theme_id\":";
    append_json_string(output, control.theme_id);
    output << ",\"theme_override\":"
           << (control.theme_override ? "true" : "false")
           << ",\"authored_material\":";
    if (control.authored_material) append_material(output, *control.authored_material);
    else output << "null";
    output << ",\"display_chunk\":";
    if (control.display_chunk) {
        output << "{\"generation\":" << (*control.display_chunk).generation
               << ",\"plane\":";
        append_json_string(output, paint_plane_name((*control.display_chunk).plane));
        output << ",\"logical_bounds\":";
        append_rect(output, (*control.display_chunk).logical_bounds);
        output << ",\"command_count\":"
               << (*control.display_chunk).command_count << '}';
    } else {
        output << "null";
    }
    output << ",\"display_chunk_current\":"
           << (control.display_chunk_current ? "true" : "false")
           << ",\"total_display_operations\":"
           << control.total_display_operations
           << ",\"display_operations_truncated\":"
           << (control.display_operations_truncated ? "true" : "false")
           << ",\"display_operations\":[";
    for (std::size_t index = 0U; index < control.display_operations.size(); ++index) {
        if (index != 0U) output << ',';
        append_operation(output, control.display_operations[index]);
    }
    output << "]}";
}

} // namespace

const char* visual_paint_operation_name(VisualPaintOperation operation) noexcept {
    switch (operation) {
    case VisualPaintOperation::save: return "save";
    case VisualPaintOperation::restore: return "restore";
    case VisualPaintOperation::translate: return "translate";
    case VisualPaintOperation::clip_rect: return "clip_rect";
    case VisualPaintOperation::clip_rounded_rect: return "clip_rounded_rect";
    case VisualPaintOperation::fill_rect: return "fill_rect";
    case VisualPaintOperation::fill_rounded_rect: return "fill_rounded_rect";
    case VisualPaintOperation::stroke_rect: return "stroke_rect";
    case VisualPaintOperation::stroke_rounded_rect: return "stroke_rounded_rect";
    case VisualPaintOperation::fill_linear_gradient: return "fill_linear_gradient";
    case VisualPaintOperation::fill_linear_gradient_spread:
        return "fill_linear_gradient_spread";
    case VisualPaintOperation::fill_radial_gradient: return "fill_radial_gradient";
    case VisualPaintOperation::draw_box_shadow: return "draw_box_shadow";
    case VisualPaintOperation::draw_inset_box_shadow:
        return "draw_inset_box_shadow";
    case VisualPaintOperation::draw_line: return "draw_line";
    case VisualPaintOperation::draw_text: return "draw_text";
    case VisualPaintOperation::draw_image: return "draw_image";
    case VisualPaintOperation::draw_image_region: return "draw_image_region";
    case VisualPaintOperation::draw_image_region_sampled:
        return "draw_image_region_sampled";
    case VisualPaintOperation::fill_image_pattern: return "fill_image_pattern";
    case VisualPaintOperation::draw_live_surface: return "draw_live_surface";
    case VisualPaintOperation::draw_part_raster: return "draw_part_raster";
    case VisualPaintOperation::begin_group: return "begin_group";
    case VisualPaintOperation::end_group: return "end_group";
    case VisualPaintOperation::draw_prepared_text: return "draw_prepared_text";
    case VisualPaintOperation::stroke_polyline: return "stroke_polyline";
    case VisualPaintOperation::fill_polygon: return "fill_polygon";
    case VisualPaintOperation::fill_band: return "fill_band";
    case VisualPaintOperation::fill_rects: return "fill_rects";
    case VisualPaintOperation::stroke_segments: return "stroke_segments";
    case VisualPaintOperation::draw_markers: return "draw_markers";
    case VisualPaintOperation::fill_conic_gradient: return "fill_conic_gradient";
    case VisualPaintOperation::fill_checker: return "fill_checker";
    case VisualPaintOperation::fill_grid_pattern: return "fill_grid_pattern";
    case VisualPaintOperation::fill_rounded_rect_corners: return "fill_rounded_rect_corners";
    case VisualPaintOperation::stroke_rounded_rect_corners: return "stroke_rounded_rect_corners";
    }
    return "unknown";
}

const char* paint_plane_name(PaintPlane plane) noexcept {
    switch (plane) {
    case PaintPlane::backplane: return "backplane";
    case PaintPlane::control: return "control";
    case PaintPlane::overlay: return "overlay";
    }
    return "unknown";
}

const char* font_role_name(FontRole role) noexcept {
    switch (role) {
    case FontRole::control: return "control";
    case FontRole::content: return "content";
    case FontRole::monospace: return "monospace";
    }
    return "unknown";
}

const char* dock_style_name(DockStyle dock) noexcept {
    switch (dock) {
    case DockStyle::none: return "none";
    case DockStyle::top: return "top";
    case DockStyle::bottom: return "bottom";
    case DockStyle::left: return "left";
    case DockStyle::right: return "right";
    case DockStyle::fill: return "fill";
    }
    return "unknown";
}

const char* visual_status_name(ControlVisualStatus status) noexcept {
    switch (status) {
    case ControlVisualStatus::normal: return "normal";
    case ControlVisualStatus::pending: return "pending";
    case ControlVisualStatus::invalid: return "invalid";
    }
    return "unknown";
}

const VisualControlInspection* VisualInspectionSnapshot::find(
    std::string_view stable_id) const noexcept {
    for (const VisualControlInspection& control : controls) {
        if (control.stable_id == stable_id) return &control;
    }
    return nullptr;
}

std::string VisualInspectionSnapshot::to_json() const {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"content_revision\":" << content_revision
           << ",\"display_generation\":" << display_generation
           << ",\"client_size\":";
    append_size(output, client_size);
    output << ",\"device_scale\":" << device_scale
           << ",\"text_scale\":" << text_scale
           << ",\"high_contrast\":" << (high_contrast ? "true" : "false")
           << ",\"reduced_motion\":" << (reduced_motion ? "true" : "false")
           << ",\"sound_enabled\":" << (sound_enabled ? "true" : "false")
           << ",\"theme_id\":";
    append_json_string(output, theme_id);
    output << ",\"window_active\":" << (window_active ? "true" : "false")
           << ",\"window_occluded\":" << (window_occluded ? "true" : "false")
           << ",\"paint_lease\":{\"state\":"
           << static_cast<unsigned>(paint_lease.state)
           << ",\"content_revision\":" << paint_lease.content_revision
           << ",\"rendered_revision\":" << paint_lease.rendered_revision
           << ",\"presented_revision\":" << paint_lease.presented_revision
           << ",\"surface_epoch\":" << paint_lease.surface_epoch
           << ",\"dirty_after_render\":"
           << (paint_lease.dirty_after_render ? "true" : "false") << "}"
           << ",\"pending_damage\":{";
    for (std::size_t plane = 0U; plane < paint_plane_count; ++plane) {
        if (plane != 0U) output << ',';
        append_json_string(output,
            paint_plane_name(static_cast<PaintPlane>(plane)));
        output << ":[";
        for (std::size_t index = 0U; index < pending_damage[plane].size(); ++index) {
            if (index != 0U) output << ',';
            append_rect(output, pending_damage[plane][index]);
        }
        output << ']';
    }
    output << "},\"total_controls\":" << total_controls
           << ",\"controls_truncated\":"
           << (controls_truncated ? "true" : "false")
           << ",\"controls\":[";
    for (std::size_t index = 0U; index < controls.size(); ++index) {
        if (index != 0U) output << ',';
        append_control(output, controls[index]);
    }
    output << "]}";
    return output.str();
}

} // namespace gui_forms
