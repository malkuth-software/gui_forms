#include "count_display_blend.hpp"
#include "../../display/replay/replay_display_chunk.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace gui_forms::detail {

namespace {

// Device coordinates are kept well inside std::int32_t, so the products and
// differences below cannot overflow.
constexpr double device_limit = 1073741824.0;

std::int32_t device_edge(const double value) noexcept {
    const double bounded = std::clamp(value, -device_limit, device_limit);
    const std::int32_t edge = static_cast<std::int32_t>(bounded);
    return edge;
}

bool usable(const Rect area, const double scale) noexcept {
    const bool result = area.finite() && !area.empty() && std::isfinite(scale) && scale > 0.0;
    return result;
}

DevicePixelRect device_intersection(const DevicePixelRect first, const DevicePixelRect second) noexcept {
    DevicePixelRect result{};
    result.left = std::max(first.left, second.left);
    result.top = std::max(first.top, second.top);
    result.right = std::max(result.left, std::min(first.right, second.right));
    result.bottom = std::max(result.top, std::min(first.bottom, second.bottom));
    return result;
}

bool device_empty(const DevicePixelRect area) noexcept {
    const bool empty = area.right <= area.left || area.bottom <= area.top;
    return empty;
}

Rect outset(const Rect area, const double by) noexcept {
    const Rect grown{area.x - by, area.y - by, area.width + by + by, area.height + by + by};
    return grown;
}

// The bounds of `points`, or an empty rectangle for none.
Rect point_bounds(const std::vector<Point>& points) noexcept {
    if (points.empty()) return Rect{};
    double left = points.front().x;
    double right = left;
    double top = points.front().y;
    double bottom = top;
    for (const Point point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    const Rect bounds{left, top, right - left, bottom - top};
    return bounds;
}

Rect united(const Rect first, const Rect second) noexcept {
    if (first.width < 0.0 || first.height < 0.0) return second;
    const double left = std::min(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double right = std::max(first.right(), second.right());
    const double bottom = std::max(first.bottom(), second.bottom());
    const Rect result{left, top, right - left, bottom - top};
    return result;
}

bool stops_opaque(const std::vector<GradientStop>& stops) noexcept {
    if (stops.empty()) return false;
    for (const GradientStop& stop : stops) {
        if (stop.color.alpha != 255U) return false;
    }
    return true;
}

// One level of the walk's state: the translation and clip in force (window
// logical coordinates), whether every clip in force is a hard rectangle,
// and the paint group the level opened, if it did.
struct WalkLevel final {
    Point offset{};
    Rect clip{};
    bool rectangular{true};
    bool opens_group{};
    PaintGroup group{};
};

class BlendWalk final {
public:
    BlendWalk(Painter& painter, const double scale, BlendStudyPlanes& planes, const Rect clip)
        : painter_(painter), scale_(scale), planes_(planes) {
        levels_.reserve(32U);
        WalkLevel base{};
        base.clip = clip;
        levels_.push_back(base);
    }

    [[nodiscard]] std::size_t depth() const noexcept { return levels_.size(); }

    // Drops the levels above `depth` (an occlusion band's saves), keeping
    // the base.
    void truncate(const std::size_t depth) {
        const std::size_t kept = std::max<std::size_t>(depth, 1U);
        while (levels_.size() > kept) levels_.pop_back();
    }

    void save() {
        WalkLevel level = levels_.back();
        level.opens_group = false;
        levels_.push_back(level);
    }

    void restore() {
        if (levels_.size() > 1U) levels_.pop_back();
    }

    void clip_rect(const Rect rect) {
        WalkLevel& level = levels_.back();
        level.clip = Rect::intersection(level.clip, placed(rect));
    }

    void command(const DisplayCommand& command);

private:
    [[nodiscard]] Rect placed(const Rect rect) const noexcept {
        const Point offset = levels_.back().offset;
        const Rect moved{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
        return moved;
    }

    [[nodiscard]] DevicePixelRect clip_device() const noexcept {
        const DevicePixelRect clip = planes_.clip_to_planes(device_outward(levels_.back().clip, scale_));
        return clip;
    }

    // An opaque source over `area` (current coordinates), its edges at
    // `device`: a copy hiding what lies beneath under a hard clip; under a
    // rounded one its corners blend, so it is charged as copied without
    // hiding anything.
    void charge_copy(const DevicePixelRect device) {
        const DevicePixelRect area = device_intersection(device, clip_device());
        if (device_empty(area)) return;
        if (levels_.back().rectangular) {
            planes_.replace(area);
        } else {
            planes_.layer(area, BlendWrite::copied);
        }
    }

    void charge_layer(const Rect area, const BlendWrite write) {
        const DevicePixelRect device = device_intersection(device_outward(placed(area), scale_), clip_device());
        if (device_empty(device)) return;
        planes_.layer(device, write);
    }

    // A copy of `inner` and a blended ring between it and `outer` (device;
    // inner inside outer): an antialiased opaque shape whose interior a
    // rectangle proves.
    void charge_ring(const DevicePixelRect outer, const DevicePixelRect proven) {
        const DevicePixelRect clip = clip_device();
        const DevicePixelRect bounds = device_intersection(outer, clip);
        if (device_empty(bounds)) return;
        const DevicePixelRect inner = device_intersection(proven, bounds);
        if (device_empty(inner)) {
            planes_.layer(bounds, BlendWrite::blended);
            return;
        }
        charge_copy(inner);
        const std::array<DevicePixelRect, 4> bands{{
            {bounds.left, bounds.top, bounds.right, inner.top},
            {bounds.left, inner.bottom, bounds.right, bounds.bottom},
            {bounds.left, inner.top, inner.left, inner.bottom},
            {inner.right, inner.top, bounds.right, inner.bottom},
        }};
        for (const DevicePixelRect band : bands) {
            if (!device_empty(band)) planes_.layer(band, BlendWrite::blended);
        }
    }

    // A rectangle fill (snapped to device pixels, as the CPU painter snaps
    // it).
    void fill_rectangle(const Rect rect, const Color color) {
        if (color.alpha == 255U) {
            planes_.note_opaque_fill_blended();
            charge_copy(device_nearest(placed(rect), scale_));
            return;
        }
        charge_layer(rect, BlendWrite::blended);
    }

    void fill_rounded(const Rect rect, const double radius, const Color color) {
        if (color.alpha != 255U) {
            charge_layer(rect, BlendWrite::blended);
            return;
        }
        planes_.note_opaque_fill_blended();
        const double inset = std::max(0.0, radius);
        const Rect moved = placed(rect);
        const Rect interior{moved.x + inset, moved.y + inset, moved.width - inset - inset,
                            moved.height - inset - inset};
        charge_ring(device_outward(moved, scale_), device_inward(interior, scale_));
    }

    void fill_gradient(const Rect rect, const std::vector<GradientStop>& stops) {
        if (stops_opaque(stops)) {
            planes_.note_opaque_fill_blended();
            charge_copy(device_nearest(placed(rect), scale_));
            return;
        }
        charge_layer(rect, BlendWrite::blended);
    }

    // An image over `destination` at `opacity`: interior copied when the
    // painter draws it as an opaque source at full opacity, its antialiased
    // edge blended; otherwise blended, and counted when its pixels are
    // all opaque but it is drawn blended.
    void draw_image(const ImageId image, const Rect destination, const double opacity) {
        const ImageDrawOpacity known = painter_.image_draw_opacity(image);
        const bool full = opacity >= 1.0;
        if (full && known.drawn_as_copy) {
            const Rect moved = placed(destination);
            charge_ring(device_outward(moved, scale_), device_inward(moved, scale_));
            return;
        }
        if (full && known.pixels_opaque) planes_.note_opaque_image_blended();
        charge_layer(destination, BlendWrite::blended);
    }

    void draw_live_surface(const DisplayCommand& command) {
        const Rect moved = placed(command.rect);
        // The painter is in its base state: asked in window coordinates.
        const Rect cover = Rect::intersection(
            painter_.opaque_live_surface_cover(command.live_surface, moved, command.scalar), moved);
        if (cover.empty() || !levels_.back().rectangular) {
            charge_layer(command.rect, BlendWrite::blended);
            return;
        }
        charge_ring(device_outward(moved, scale_), device_nearest(cover, scale_));
    }

    void draw_part_raster(const DisplayCommand& command, const double opacity) {
        if (!command.part_raster) return;
        // One raster pixel per device pixel, placed on the nearest device
        // pixel; an opaque raster at full opacity is copied exactly.
        if ((*command.part_raster).opaque() && opacity >= 1.0) {
            charge_copy(device_nearest(placed(command.rect), scale_));
            return;
        }
        charge_layer(command.rect, BlendWrite::blended);
    }

    void draw_text(const DisplayCommand& command) {
        const Size measured = painter_.measure_text_utf8(command.text, command.font);
        const double height = std::max(measured.height, command.font.size * 1.3);
        const bool horizontal =
            command.text_degrees == 0.0 && command.text_orientation == TextOrientation::horizontal;
        if (horizontal) {
            const Rect box{command.first.x, command.first.y - command.font.size, measured.width, height};
            charge_layer(box, BlendWrite::blended);
            return;
        }
        // Turned or rotated about its origin: the square it can sweep.
        const double side = std::max(measured.width, height);
        const Rect swept{command.first.x - side, command.first.y - side, side + side, side + side};
        charge_layer(swept, BlendWrite::blended);
    }

    void begin_group(const DisplayCommand& command) {
        WalkLevel level = levels_.back();
        level.opens_group = true;
        level.group.bounds = command.rect;
        level.group.opacity = command.scalar;
        const double composite = command.secondary_scalar;
        const bool known = composite >= 0.0 && composite <= static_cast<double>(PaintGroupComposite::copy);
        level.group.composite = known ? static_cast<PaintGroupComposite>(static_cast<std::uint8_t>(composite))
                                      : PaintGroupComposite::source_over;
        level.group.isolated = command.tertiary_scalar != 0.0;
        levels_.push_back(level);
        clip_rect(command.rect);
    }

    // The group's composite where it lands: none for a group drawn directly
    // (source-over at full opacity, not isolated); a copy group replaces
    // what lay beneath; any other isolated group blends once more.
    void end_group() {
        if (levels_.size() <= 1U) return;
        const WalkLevel ended = levels_.back();
        const DevicePixelRect landing = clip_device();
        levels_.pop_back();
        if (!ended.opens_group) return;
        const PaintGroup& group = ended.group;
        const bool isolated =
            group.isolated || group.composite != PaintGroupComposite::source_over || !(group.opacity >= 1.0);
        if (!isolated || device_empty(landing)) return;
        if (group.composite == PaintGroupComposite::copy && levels_.back().rectangular) {
            planes_.replace(landing);
            return;
        }
        planes_.layer(landing, BlendWrite::blended);
    }

    void marks(const DisplayCommand& command);

    Painter& painter_;
    double scale_{};
    BlendStudyPlanes& planes_;
    std::vector<WalkLevel> levels_;
};

// The batched marks and patterns, and the per-corner rounded shapes, as
// replay_marks dispatches them.
void BlendWalk::marks(const DisplayCommand& command) {
    if (command.operation == DisplayOperation::fill_conic_gradient) {
        fill_gradient(command.rect, command.gradient_stops);
        return;
    }
    if (command.operation == DisplayOperation::fill_rounded_rect_corners) {
        const double radius = std::max(std::max(command.first.x, command.first.y),
                                       std::max(command.second.x, command.second.y));
        fill_rounded(command.rect, radius, command.color);
        return;
    }
    if (command.operation == DisplayOperation::stroke_rounded_rect_corners) {
        charge_layer(outset(command.rect, command.scalar * 0.5 + 1.0), BlendWrite::blended);
        return;
    }
    if (!command.marks) return;
    const DisplayMarks& marks = *command.marks;
    const double stroke_reach = marks.stroke.width * 0.5 + 1.0;
    switch (command.operation) {
    case DisplayOperation::stroke_polyline:
    case DisplayOperation::stroke_path:
        charge_layer(outset(point_bounds(marks.points), stroke_reach), BlendWrite::blended);
        return;
    case DisplayOperation::fill_polygon:
    case DisplayOperation::fill_path:
        charge_layer(outset(point_bounds(marks.points), 1.0), BlendWrite::blended);
        return;
    case DisplayOperation::fill_band: {
        const Rect upper = point_bounds(marks.points);
        const Rect lower = point_bounds(marks.lower_points);
        charge_layer(outset(united(upper, lower), 1.0), BlendWrite::blended);
        return;
    }
    case DisplayOperation::fill_rects:
        for (const Rect rect : marks.rects) fill_rectangle(rect, command.color);
        return;
    case DisplayOperation::fill_rects_each: {
        const std::size_t count = std::min(marks.rects.size(), marks.colors.size());
        for (std::size_t index = 0U; index < count; ++index) fill_rectangle(marks.rects[index], marks.colors[index]);
        return;
    }
    case DisplayOperation::stroke_segments: {
        Rect bounds{0.0, 0.0, -1.0, -1.0};
        for (const PaintSegment& segment : marks.segments) {
            const Rect from{segment.from.x, segment.from.y, 0.0, 0.0};
            const Rect to{segment.to.x, segment.to.y, 0.0, 0.0};
            bounds = united(united(bounds, from), to);
        }
        if (bounds.width >= 0.0) charge_layer(outset(bounds, stroke_reach), BlendWrite::blended);
        return;
    }
    case DisplayOperation::draw_markers: {
        const double reach = marks.marker.size * 0.5 + marks.marker.edge_width + 1.0;
        charge_layer(outset(point_bounds(marks.points), reach), BlendWrite::blended);
        return;
    }
    case DisplayOperation::fill_checker: {
        // Drawn through rectangle fills, snapped: a copy when both colours
        // are opaque.
        const bool opaque = marks.checker.first.alpha == 255U && marks.checker.second.alpha == 255U;
        if (opaque) {
            planes_.note_opaque_fill_blended();
            charge_copy(device_nearest(placed(command.rect), scale_));
            return;
        }
        charge_layer(command.rect, BlendWrite::blended);
        return;
    }
    case DisplayOperation::fill_grid_pattern:
        charge_layer(command.rect, BlendWrite::blended);
        return;
    default:
        // Not a batched operation: command() handles it.
        return;
    }
}

void BlendWalk::command(const DisplayCommand& command) {
    switch (command.operation) {
    case DisplayOperation::save: save(); return;
    case DisplayOperation::restore: restore(); return;
    case DisplayOperation::translate: {
        WalkLevel& level = levels_.back();
        level.offset = Point{level.offset.x + command.first.x, level.offset.y + command.first.y};
        return;
    }
    case DisplayOperation::clip_rect: clip_rect(command.rect); return;
    case DisplayOperation::clip_rounded_rect:
    case DisplayOperation::clip_rounded_rect_corners:
        clip_rect(command.rect);
        levels_.back().rectangular = false;
        return;
    case DisplayOperation::begin_group: begin_group(command); return;
    case DisplayOperation::end_group: end_group(); return;
    default:
        break;
    }
    // Every other operation draws.
    planes_.note_draw();
    switch (command.operation) {
    case DisplayOperation::fill_rect: fill_rectangle(command.rect, command.color); return;
    case DisplayOperation::fill_rounded_rect: fill_rounded(command.rect, command.scalar, command.color); return;
    case DisplayOperation::stroke_rect:
        charge_layer(outset(command.rect, command.scalar * 0.5 + 1.0), BlendWrite::blended);
        return;
    case DisplayOperation::stroke_rounded_rect:
        charge_layer(outset(command.rect, command.secondary_scalar * 0.5 + 1.0), BlendWrite::blended);
        return;
    case DisplayOperation::fill_linear_gradient:
    case DisplayOperation::fill_linear_gradient_spread:
    case DisplayOperation::fill_radial_gradient:
        fill_gradient(command.rect, command.gradient_stops);
        return;
    case DisplayOperation::draw_box_shadow: {
        // scalar the corner radius, first the offset, secondary the blur,
        // tertiary the spread.
        const double reach = std::abs(command.secondary_scalar) + std::abs(command.tertiary_scalar) +
                             std::max(std::abs(command.first.x), std::abs(command.first.y)) + 1.0;
        charge_layer(outset(command.rect, reach), BlendWrite::blended);
        return;
    }
    case DisplayOperation::draw_inset_box_shadow: charge_layer(command.rect, BlendWrite::blended); return;
    case DisplayOperation::draw_line: {
        const Rect from{command.first.x, command.first.y, 0.0, 0.0};
        const Rect to{command.second.x, command.second.y, 0.0, 0.0};
        charge_layer(outset(united(from, to), command.scalar * 0.5 + 1.0), BlendWrite::blended);
        return;
    }
    case DisplayOperation::draw_text_utf8: draw_text(command); return;
    case DisplayOperation::draw_prepared_text: planes_.note_unmeasured_text(); return;
    case DisplayOperation::draw_image:
    case DisplayOperation::draw_image_region:
    case DisplayOperation::draw_image_region_sampled:
    case DisplayOperation::fill_image_pattern:
        draw_image(command.image, command.rect, command.scalar);
        return;
    case DisplayOperation::draw_live_surface: draw_live_surface(command); return;
    case DisplayOperation::draw_part_raster:
        // A baked material carries no opacity of its own: it is drawn at one.
        draw_part_raster(command, command.surface_material ? 1.0 : command.scalar);
        return;
    case DisplayOperation::draw_part_raster_scaled:
        // Sampled bilinearly, so blended even when opaque.
        charge_layer(command.rect, BlendWrite::blended);
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
    case DisplayOperation::fill_path:
    case DisplayOperation::stroke_path:
        marks(command);
        return;
    default:
        break;
    }
    // An operation outside the enumeration, or a state operation handled
    // above: nothing to charge.
    return;
}

} // namespace

DevicePixelRect device_outward(const Rect area, const double scale) noexcept {
    DevicePixelRect device{};
    if (!usable(area, scale)) return device;
    device.left = device_edge(std::floor(area.x * scale));
    device.top = device_edge(std::floor(area.y * scale));
    device.right = std::max(device.left, device_edge(std::ceil(area.right() * scale)));
    device.bottom = std::max(device.top, device_edge(std::ceil(area.bottom() * scale)));
    return device;
}

DevicePixelRect device_inward(const Rect area, const double scale) noexcept {
    DevicePixelRect device{};
    if (!usable(area, scale)) return device;
    device.left = device_edge(std::ceil(area.x * scale));
    device.top = device_edge(std::ceil(area.y * scale));
    device.right = std::max(device.left, device_edge(std::floor(area.right() * scale)));
    device.bottom = std::max(device.top, device_edge(std::floor(area.bottom() * scale)));
    return device;
}

DevicePixelRect device_nearest(const Rect area, const double scale) noexcept {
    DevicePixelRect device{};
    if (!usable(area, scale)) return device;
    device.left = device_edge(std::floor(area.x * scale + 0.5));
    device.top = device_edge(std::floor(area.y * scale + 0.5));
    device.right = std::max(device.left, device_edge(std::floor(area.right() * scale + 0.5)));
    device.bottom = std::max(device.top, device_edge(std::floor(area.bottom() * scale + 0.5)));
    return device;
}

void count_display_blend(const DisplayChunk& chunk, Painter& painter, const Rect clip, const double scale,
                         BlendStudyPlanes& planes) {
    if (!planes.active()) return;
    const DisplayCommandList& commands = chunk.commands();
    BlendWalk walk(painter, scale, planes, clip);
    const Rect hidden = display_chunk_hidden_rect(chunk, painter, clip);
    if (hidden.empty()) {
        for (const DisplayCommand& command : commands) walk.command(command);
        return;
    }
    // As replay_display_chunk_occluded: what lies before the occluder only
    // in the bands it leaves showing, then the state open at it, then the
    // occluder and the rest.
    const DisplayOccluder& found = *chunk.occluder();
    const std::array<Rect, 4> bands = display_occlusion_bands(clip, hidden);
    std::size_t index = 0U;
    for (const Rect band : bands) {
        if (band.empty()) continue;
        const std::size_t depth = walk.depth();
        walk.save();
        walk.clip_rect(band);
        for (index = 0U; index < found.command; ++index) walk.command(commands[index]);
        walk.truncate(depth);
    }
    for (index = found.group_start; index < found.command; ++index) {
        if (display_state_only(commands[index].operation)) walk.command(commands[index]);
    }
    for (index = found.command; index < commands.size(); ++index) walk.command(commands[index]);
}

} // namespace gui_forms::detail
