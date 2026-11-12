// NodeCanvas's painting (node graph spec sections 3 and 4): the well and
// the world grid, the visible links, the visible nodes by level of detail,
// the ports in batches by socket type, the lamps, and the gesture overlay.
// Every colour comes from the theme: a part painted through paint_part
// (keyed by its anatomy name, and for a node's parts by the node's item, so
// `node-canvas::part(...)` rules reach it), or an ink resolved once per
// frame from a named part for the batched marks. Nothing here changes the
// graph, and nothing allocates per node: the visible sets and the marker
// batches use storage sized at the cache rebuilds.

#include "gui_forms/controls/node_canvas.hpp"

#include "node_canvas_detail.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace gui_forms {

using namespace node_canvas_detail;

namespace {

// A dimmed port's alpha (a link drag dims ports that cannot take it).
constexpr std::uint8_t dimmed_alpha = 60U;
constexpr double preview_width = 2.0;
constexpr double knife_width = 2.0;

// A rule's ink as paint_part draws rules: the border's colour, else the
// muted text's.
[[nodiscard]] Color rule_ink(const ControlVisualRecipe& recipe) noexcept {
    if (recipe.material.border) return (*recipe.material.border).color;
    return recipe.muted_text;
}

// A surface's flat ink: its first fill's colour, else its text colour.
[[nodiscard]] Color face_ink(const ControlVisualRecipe& recipe) noexcept {
    if (recipe.material.fills.empty()) return recipe.text;
    return recipe.material.fills.front().color;
}

[[nodiscard]] Color with_alpha(Color colour, std::uint8_t alpha) noexcept {
    colour.alpha = alpha;
    return colour;
}

[[nodiscard]] Rect grown(Rect rect, double margin) noexcept {
    const Rect bigger{rect.x - margin, rect.y - margin, rect.width + 2.0 * margin, rect.height + 2.0 * margin};
    return bigger;
}

[[nodiscard]] PartKey control_key(std::string_view part) noexcept {
    const PartKey key{part, {}, {}};
    return key;
}

[[nodiscard]] PartKey node_key(std::string_view part, std::string_view item) noexcept {
    const PartKey key{part, item, {}};
    return key;
}

} // namespace

void NodeCanvas::on_paint(Painter& painter, Rect) {
    ++statistics_.frames_painted;
    const Rect client = client_rectangle();
    // The well first, untranslated and unclipped, so that paint_part can
    // report an opaque cover. back_color is the per-control override of the
    // theme's ground, as VirtualRowsView's is.
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = client;
    well.authored_background = back_color.read();
    well.key = control_key(well_part);
    paint_part(painter, *this, well);
    visible_nodes_.clear();
    visible_links_.clear();
    if (graph_ == nullptr) return;
    ensure_caches();
    const SceneDetailLevel level = next_scene_detail_level(level_, view_.zoom, thresholds_);
    if (level != level_) ++statistics_.level_transitions;
    level_ = level;
    statistics_.level = level;

    FrameInks inks{};
    resolve_inks(inks);
    painter.save();
    painter.clip_rect(client);
    paint_grid(painter, inks);

    // The visible nodes, in slot order: later slots on top, as hit testing
    // assumes.
    const SpatialQueryResult found = node_index_.query(view_.rect_to_world(client), SpatialQueryMode::intersecting,
                                                       std::span<std::uint32_t>(query_.data(), query_.size()));
    for (std::size_t index = 0U; index < found.written; ++index) visible_nodes_.push_back(query_[index]);
    std::sort(visible_nodes_.begin(), visible_nodes_.end());

    paint_links(painter, inks);
    paint_nodes(painter, inks);
    paint_ports(painter, inks);
    paint_overlay(painter, inks);
    painter.restore();

    // With no node to carry the focus cue, the canvas itself shows it.
    bool node_carries_focus = false;
    if (focused_node_.has_value()) {
        node_carries_focus = std::binary_search(visible_nodes_.begin(), visible_nodes_.end(), (*focused_node_).index);
    }
    const Window* const host = attached_window();
    if (focused_ && !node_carries_focus && host != nullptr && (*host).focus_cue_visible()) {
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.bounds = client;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = control_key(focus_part);
        paint_part(painter, *this, focus);
    }
    statistics_.last_frame_nodes = visible_nodes_.size();
    painted_lamp_revision_ = (*graph_).lamp_revision();
}

void NodeCanvas::resolve_inks(FrameInks& inks) const {
    const Theme& theme = effective_theme();
    const AnatomyStateSet plain{};
    AnatomyStateSet chosen{};
    chosen.insert(AnatomyState::selected);
    const ControlVisualContext plain_context = part_visual_context(*this, plain);
    const ControlVisualContext chosen_context = part_visual_context(*this, chosen);
    const ResolvedControlRecipe minor =
        theme.resolve(*this, ControlVisualRole::editor, plain_context, plain, grid_minor_part);
    inks.grid_minor = rule_ink(minor.recipe());
    const ResolvedControlRecipe major =
        theme.resolve(*this, ControlVisualRole::editor, plain_context, plain, grid_major_part);
    inks.grid_major = rule_ink(major.recipe());
    const ResolvedControlRecipe face = theme.resolve(*this, ControlVisualRole::card, plain_context, plain, node_face_part);
    inks.node_fill = face_ink(face.recipe());
    const ResolvedControlRecipe plate =
        theme.resolve(*this, ControlVisualRole::selection, chosen_context, chosen, node_selection_part);
    inks.node_selected_fill = face_ink(plate.recipe());
    inks.selection = inks.node_selected_fill;
    const ResolvedControlRecipe port = theme.resolve(*this, ControlVisualRole::editor, plain_context, plain, port_part);
    inks.port_edge = port.recipe().glyph;
    const ResolvedControlRecipe knife = theme.resolve(*this, ControlVisualRole::editor, plain_context, plain, knife_part);
    inks.knife = knife.recipe().glyph;
    const ResolvedControlRecipe connector =
        theme.resolve(*this, ControlVisualRole::selection, chosen_context, chosen, connector_part);
    inks.preview = connector.recipe().glyph;
    // The socket types' colours: the theme's accent ramp, one entry per
    // bucket (the bucket's marker shape is the second cue).
    for (std::size_t bucket = 0U; bucket < port_buckets; ++bucket) {
        inks.sockets[bucket] = theme.accent(AccentIndex{static_cast<std::uint8_t>(bucket)});
    }
}

void NodeCanvas::paint_grid(Painter& painter, const FrameInks& inks) const {
    // One level of the s * 4^k grid, its minor lines faded across the band,
    // so the zoom crosses levels without a pop (select_grid_level).
    const GridLevel level = select_grid_level(grid_spacing_, view_.zoom, grid_band);
    const Point origin = view_.to_view(WorldPoint{0.0, 0.0});
    const GridPattern pattern = grid_pattern_at_level(level, origin, inks.grid_minor, inks.grid_major, 1.0);
    painter.fill_grid_pattern(client_rectangle(), pattern);
}

void NodeCanvas::paint_links(Painter& painter, const FrameInks& inks) {
    const NodeGraph& graph = *graph_;
    const std::span<const LinkRecord> links = graph.link_slots();
    const double width = std::clamp(link_width_world * view_.zoom, link_width_minimum, link_width_maximum);
    // Strokes reach half their width (and the halo) past their paths.
    const double reach = (width + selected_link_halo) / view_.zoom;
    const WorldRect area = world_rect_inflated(view_.rect_to_world(client_rectangle()), reach);
    const SpatialQueryResult found = link_index_.query(area, SpatialQueryMode::intersecting,
                                                       std::span<std::uint32_t>(query_.data(), query_.size()));
    for (std::size_t index = 0U; index < found.written; ++index) visible_links_.push_back(query_[index]);
    std::sort(visible_links_.begin(), visible_links_.end());
    const std::size_t segments = curve_segments();
    const std::span<Point> points(link_points_.data(), link_points_.size());
    std::size_t painted = 0U;
    for (const std::uint32_t slot : visible_links_) {
        if (slot >= links.size() || !links[slot].live) continue;
        const LinkRecord& link = links[slot];
        const PortRecord* from_port = graph.port(link.from);
        if (from_port == nullptr) continue;
        const std::size_t count = link_path(slot, link, segments, points);
        if (count < 2U) continue;
        statistics_.link_points_flattened += count;
        const std::span<const Point> path(link_points_.data(), count);
        StrokeStyle style{};
        style.color = inks.sockets[port_bucket((*from_port).socket_type)];
        style.width = width;
        style.join = StrokeJoin::round;
        style.cap = StrokeCap::round;
        const LinkId id{slot, link.generation};
        if (selected_link_.has_value() && *selected_link_ == id) {
            StrokeStyle halo = style;
            halo.color = inks.selection;
            halo.width = width + selected_link_halo;
            painter.stroke_polyline(path, halo);
        }
        painter.stroke_polyline(path, style);
        ++painted;
    }
    statistics_.links_painted += painted;
    statistics_.last_frame_links = painted;
}

void NodeCanvas::paint_nodes(Painter& painter, const FrameInks& inks) {
    for (std::vector<Point>& centres : port_centres_) centres.clear();
    for (std::vector<Point>& centres : dimmed_centres_) centres.clear();
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    if (level_ == SceneDetailLevel::overview) {
        // Silhouettes only: two batched fills in the face's and the
        // selection's inks.
        overview_rects_.clear();
        overview_selected_.clear();
        for (const std::uint32_t slot : visible_nodes_) {
            if (slot >= slots.size() || !slots[slot].live) continue;
            const Rect rect = view_.rect_to_view(slots[slot].bounds);
            const bool chosen = slot < selected_flags_.size() && selected_flags_[slot] != 0U;
            if (chosen) {
                overview_selected_.push_back(rect);
            } else {
                overview_rects_.push_back(rect);
            }
        }
        painter.fill_rects(std::span<const Rect>(overview_rects_.data(), overview_rects_.size()), inks.node_fill);
        painter.fill_rects(std::span<const Rect>(overview_selected_.data(), overview_selected_.size()),
                           inks.node_selected_fill);
        statistics_.nodes_painted += visible_nodes_.size();
        return;
    }
    for (const std::uint32_t slot : visible_nodes_) {
        if (slot >= slots.size() || !slots[slot].live) continue;
        paint_node(painter, slot, slots[slot]);
    }
    statistics_.nodes_painted += visible_nodes_.size();
}

void NodeCanvas::paint_node(Painter& painter, std::uint32_t slot, const NodeRecord& node) {
    const Rect rect = view_.rect_to_view(node.bounds);
    AnatomyStateSet states;
    node_states_into(slot, states);
    const std::string_view item = slot < node_items_.size() ? std::string_view(node_items_[slot]) : std::string_view{};
    const double zoom = view_.zoom;
    PartPaint part;
    part.states = states;
    part.corner_radius = node_corner_radius * zoom;
    if (states.contains(AnatomyState::selected)) {
        part.role = PartRole::selection;
        part.family = ControlVisualRole::selection;
        part.bounds = grown(rect, selection_outset * zoom);
        part.key = node_key(node_selection_part, item);
        paint_part(painter, *this, part);
    }
    part.role = PartRole::face;
    part.family = ControlVisualRole::card;
    part.bounds = rect;
    part.key = node_key(node_face_part, item);
    paint_part(painter, *this, part);
    part.corner_radius.reset();
    if (level_ == SceneDetailLevel::silhouette) {
        if (states.contains(AnatomyState::focus_visible)) {
            part.role = PartRole::focus;
            part.key = node_key(node_focus_part, item);
            paint_part(painter, *this, part);
        }
        return;
    }

    // The caption band and its title.
    const double caption = std::min(caption_height * zoom, rect.height);
    const Rect band{rect.x, rect.y, rect.width, caption};
    const double title_size = caption_text_size * zoom;
    if (title_size >= smallest_text) {
        FontSpec base{};
        base.size = title_size;
        part.role = PartRole::caption;
        part.bounds = Rect{band.x + node_corner_radius * zoom, band.y, std::max(0.0, band.width - 2.0 * node_corner_radius * zoom),
                           band.height};
        part.font = effective_font(base);
        part.text = lend_paint_text(node.title);
        part.key = node_key(node_caption_part, item);
        paint_part(painter, *this, part);
        part.text = {};
        ++statistics_.captions_drawn;
    } else {
        ++statistics_.text_skipped_small;
    }

    // Ports into the batches (painted after every node, so a port on an
    // edge is never covered by its neighbour's face), lamps beside outputs,
    // and at full detail the port labels.
    const bool dimming = gesture_ == NodeCanvasGesture::link && gesture_planned_;
    const bool full = level_ == SceneDetailLevel::full;
    const double label_size = port_label_size * zoom;
    const bool labels = full && label_size >= smallest_text;
    if (full && label_size < smallest_text) ++statistics_.text_skipped_small;
    FontSpec label_base{};
    label_base.size = label_size;
    const FontSpec label_font = effective_font(label_base);
    for (std::size_t port = 0U; port < node.ports.size(); ++port) {
        const PortRecord& record = node.ports[port];
        const Point anchor = view_.to_view(port_world(node, record));
        const std::size_t bucket = port_bucket(record.socket_type);
        bool dim = false;
        if (dimming) {
            // Only the magnet's candidates take the dragged link.
            dim = true;
            const bool flagged = slot < candidate_flags_.size() && candidate_flags_[slot] != 0U;
            for (std::size_t at = 0U; at < candidates_.size() && flagged && dim; ++at) {
                const PortRef candidate = candidates_[at].port;
                if (candidate.node.index == slot && candidate.port.value == port) dim = false;
            }
        }
        if (dim) {
            dimmed_centres_[bucket].push_back(anchor);
        } else {
            port_centres_[bucket].push_back(anchor);
        }
        ++statistics_.ports_painted;
        if (record.direction == PortDirection::output) {
            const double size = lamp_size * zoom;
            const double inward = effective_side(record) == PortSide::left ? lamp_inset * zoom : -lamp_inset * zoom;
            part.role = PartRole::indicator;
            part.family = ControlVisualRole::editor;
            part.bounds = Rect{anchor.x + inward - size / 2.0, anchor.y - size / 2.0, size, size};
            part.states = AnatomyStateSet{};
            if (record.lamp == PortLamp::on) part.states.insert(AnatomyState::checked);
            if (record.lamp == PortLamp::error) part.states.insert(AnatomyState::invalid);
            part.key = node_key(lamp_part, item);
            paint_part(painter, *this, part);
            part.states = states;
            ++statistics_.lamps_painted;
        }
        if (labels) {
            // Inputs label to the inside of a left edge, outputs to the
            // inside of a right edge; a label never leaves its node.
            const double gap = port_label_gap * zoom;
            const double height = label_size * 1.4;
            const bool leading = effective_side(record) != PortSide::right;
            const double width = std::max(0.0, rect.width / 2.0 - gap);
            part.role = PartRole::label;
            part.family = ControlVisualRole::card;
            part.bounds = leading ? Rect{anchor.x + gap, anchor.y - height / 2.0, width, height}
                                  : Rect{anchor.x - gap - width, anchor.y - height / 2.0, width, height};
            part.alignment = leading ? PartTextAlignment::start : PartTextAlignment::end;
            part.font = label_font;
            part.text = lend_paint_text(record.name);
            part.key = node_key(port_label_part, item);
            paint_part(painter, *this, part);
            part.text = {};
            part.alignment = PartTextAlignment::start;
        }
        if (focused_node_.has_value() && (*focused_node_).index == slot && focused_port_.has_value() &&
            (*focused_port_).value == port && states.contains(AnatomyState::focus_visible)) {
            const double ring = std::max(smallest_port_marker, port_marker_size * zoom) + 6.0;
            PartPaint cue;
            cue.role = PartRole::focus;
            cue.family = ControlVisualRole::editor;
            cue.bounds = Rect{anchor.x - ring / 2.0, anchor.y - ring / 2.0, ring, ring};
            cue.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, cue);
        }
    }

    if (full) {
        const Rect body{rect.x, rect.y + caption, rect.width, std::max(0.0, rect.height - caption)};
        if (callbacks_.paint_body && body.width > 0.0 && body.height > 0.0) {
            NodeBodyPaint request{};
            request.painter = &painter;
            request.node = NodeId{slot, node.generation};
            request.body = body;
            request.zoom = zoom;
            request.states = states;
            painter.save();
            painter.clip_rect(body);
            {
                const CallbackScope scope(*this);
                callbacks_.paint_body(request);
            }
            painter.restore();
        }
    }
    if (states.contains(AnatomyState::focus_visible) && !focused_port_.has_value()) {
        part.role = PartRole::focus;
        part.family = ControlVisualRole::editor;
        part.bounds = rect;
        part.key = node_key(node_focus_part, item);
        paint_part(painter, *this, part);
    }
}

void NodeCanvas::paint_ports(Painter& painter, const FrameInks& inks) {
    if (level_ != SceneDetailLevel::compact && level_ != SceneDetailLevel::full) return;
    const double size = std::max(smallest_port_marker, port_marker_size * view_.zoom);
    for (std::size_t bucket = 0U; bucket < port_buckets; ++bucket) {
        MarkerStyle style{};
        style.shape = port_shapes[bucket];
        style.size = size;
        style.fill = inks.sockets[bucket];
        style.edge = inks.port_edge;
        style.edge_width = 1.0;
        const std::vector<Point>& shown = port_centres_[bucket];
        if (!shown.empty()) painter.draw_markers(std::span<const Point>(shown.data(), shown.size()), style);
        const std::vector<Point>& dimmed = dimmed_centres_[bucket];
        if (dimmed.empty()) continue;
        style.fill = with_alpha(style.fill, dimmed_alpha);
        style.edge = with_alpha(style.edge, dimmed_alpha);
        painter.draw_markers(std::span<const Point>(dimmed.data(), dimmed.size()), style);
    }
}

void NodeCanvas::paint_overlay(Painter& painter, const FrameInks& inks) {
    const bool linking = (gesture_ == NodeCanvasGesture::link && gesture_planned_) || keyboard_link_;
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::marquee) {
        PartPaint marquee;
        marquee.role = PartRole::focus;
        marquee.family = ControlVisualRole::editor;
        marquee.bounds = Rect{std::min(press_view_.x, current_view_.x), std::min(press_view_.y, current_view_.y),
                              std::fabs(current_view_.x - press_view_.x), std::fabs(current_view_.y - press_view_.y)};
        marquee.states.insert(AnatomyState::focus_visible);
        marquee.key = control_key(marquee_part);
        paint_part(painter, *this, marquee);
        return;
    }
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::knife) {
        const std::array<Point, 2> stroke{press_view_, current_view_};
        StrokeStyle style{};
        style.color = inks.knife;
        style.width = knife_width;
        style.cap = StrokeCap::round;
        painter.stroke_polyline(std::span<const Point>(stroke.data(), stroke.size()), style);
        return;
    }
    if (!linking || graph_ == nullptr) return;
    const NodeRecord* from_node = (*graph_).node(link_from_.node);
    const PortRecord* from_port = (*graph_).port(link_from_);
    if (from_node == nullptr || from_port == nullptr) return;
    // The dangling end: the captured (or keyboard-chosen) input, else the
    // pointer.
    std::optional<std::size_t> target{};
    if (keyboard_link_ && keyboard_target_ < candidates_.size()) target = keyboard_target_;
    if (!keyboard_link_ && link_hit_.has_value() && *link_hit_ < candidates_.size()) target = *link_hit_;
    WorldPoint end = view_.to_world(current_view_);
    SceneEdge end_edge = SceneEdge::left;
    if (target.has_value()) {
        const PortRef to = candidates_[*target].port;
        const NodeRecord* to_node = (*graph_).node(to.node);
        const PortRecord* to_port = (*graph_).port(to);
        if (to_node != nullptr && to_port != nullptr) {
            end = port_world(*to_node, *to_port);
            end_edge = edge_of(effective_side(*to_port));
        }
    }
    const SceneCubic cubic = soft_link_cubic(port_world(*from_node, *from_port), edge_of(effective_side(*from_port)),
                                             end, end_edge, SoftLinkShape{});
    const std::size_t count =
        flatten_scene_cubic(cubic, 24U, view_, std::span<Point>(link_points_.data(), link_points_.size()));
    StrokeStyle style{};
    style.color = inks.preview;
    style.width = preview_width;
    style.cap = StrokeCap::round;
    painter.stroke_polyline(std::span<const Point>(link_points_.data(), count), style);
    if (!target.has_value()) return;
    const Point at = view_.to_view(end);
    PartPaint drop;
    drop.role = PartRole::drop;
    drop.family = ControlVisualRole::editor;
    drop.bounds = Rect{at.x - drop_ring / 2.0, at.y - drop_ring / 2.0, drop_ring, drop_ring};
    drop.states.insert(AnatomyState::drop_accepted);
    drop.key = control_key(drop_part);
    paint_part(painter, *this, drop);
}

} // namespace gui_forms
