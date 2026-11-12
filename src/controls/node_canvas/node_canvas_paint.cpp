// NodeCanvas's painting (node graph spec sections 3 and 4): the canvas
// background and world grid, the visible links, the visible nodes by level
// of detail with their ports and lamps, and the gesture overlay. Every
// colour, line and the grid come from the theme's canvas look
// (canvas_look.hpp): resolved once into look_ (ensure_look) and again only
// when the theme, colour scheme, contrast, classes or back_color change,
// then painted each frame with the paint_canvas_ functions. Port labels and
// the keyboard focus cues, which the canvas anatomy has no part for, are
// painted through paint_part by role. Nothing here changes the graph, and
// nothing allocates per node: the visible sets use storage sized at the
// cache rebuilds.

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
#include <string>

namespace gui_forms {

using namespace node_canvas_detail;

namespace {

// Where a title's baseline sits below the middle of its band, as a share of
// the font size (about half the cap height).
constexpr double title_baseline_share = 0.35;
// The overview's silhouettes, the node state's fill.
constexpr std::size_t node_rest = static_cast<std::size_t>(CanvasNodeState::normal);
constexpr std::size_t node_chosen = static_cast<std::size_t>(CanvasNodeState::selected);

// A node's look state from its anatomy states: disabled first, then
// selected, then under the pointer.
[[nodiscard]] CanvasNodeState canvas_node_state(const AnatomyStateSet& states) noexcept {
    if (states.contains(AnatomyState::disabled)) return CanvasNodeState::disabled;
    if (states.contains(AnatomyState::selected)) return CanvasNodeState::selected;
    if (states.contains(AnatomyState::hot)) return CanvasNodeState::hot;
    return CanvasNodeState::normal;
}

[[nodiscard]] CanvasLampState canvas_lamp_state(PortLamp lamp) noexcept {
    switch (lamp) {
    case PortLamp::on: return CanvasLampState::on;
    case PortLamp::error: return CanvasLampState::error;
    default: break;
    }
    return CanvasLampState::off;
}

[[nodiscard]] bool same_classes(std::span<const std::string> current, const std::vector<std::string>& kept) noexcept {
    if (current.size() != kept.size()) return false;
    for (std::size_t index = 0U; index < current.size(); ++index) {
        if (current[index] != kept[index]) return false;
    }
    return true;
}

} // namespace

void NodeCanvas::ensure_look() {
    const Theme& theme = effective_theme();
    const CompiledThemeRules* const inline_rules = inline_style_rules();
    const ControlVisualContext context = visual_context();
    const std::span<const std::string> current_classes = classes.read();
    const std::optional<Color> authored = back_color.read();
    const bool unchanged = look_resolved_ && look_theme_ == &theme && look_inline_rules_ == inline_rules &&
                           look_dark_ == context.dark && look_high_contrast_ == context.high_contrast &&
                           look_back_color_ == authored && same_classes(current_classes, look_classes_);
    if (unchanged) return;
    // Each part resolves in its own states over the canvas's colour scheme
    // and contrast; the control's own interaction state is not the parts'.
    ControlVisualContext scheme{};
    scheme.dark = context.dark;
    scheme.high_contrast = context.high_contrast;
    look_ = resolve_canvas_look(theme, *this, scheme);
    // back_color is the per-control override of the region's colour, as
    // VirtualRowsView's is of its well; high contrast keeps the plate.
    if (authored.has_value() && !context.high_contrast) look_.background.fill = *authored;
    if (grid_spacing_set_.has_value()) look_.grid.spacing = *grid_spacing_set_;
    look_theme_ = &theme;
    look_inline_rules_ = inline_rules;
    look_dark_ = context.dark;
    look_high_contrast_ = context.high_contrast;
    look_back_color_ = authored;
    look_classes_.assign(current_classes.begin(), current_classes.end());
    look_resolved_ = true;
}

void NodeCanvas::on_paint(Painter& painter, Rect) {
    ++statistics_.frames_painted;
    const Rect client = client_rectangle();
    ensure_look();
    // The background first, untranslated and unclipped: the region, the
    // texture and the grid anchored at world (0, 0).
    const CanvasView canvas_view{view_.to_view(WorldPoint{0.0, 0.0}), view_.zoom};
    paint_canvas_background(painter, look_, client, canvas_view);
    visible_nodes_.clear();
    visible_links_.clear();
    if (graph_ == nullptr) return;
    ensure_caches();
    const SceneDetailLevel level = next_scene_detail_level(level_, view_.zoom, thresholds_);
    if (level != level_) ++statistics_.level_transitions;
    level_ = level;
    statistics_.level = level;

    painter.save();
    painter.clip_rect(client);
    // The visible nodes, in slot order: later slots on top, as hit testing
    // assumes.
    const SpatialQueryResult found = node_index_.query(view_.rect_to_world(client), SpatialQueryMode::intersecting,
                                                       std::span<std::uint32_t>(query_.data(), query_.size()));
    for (std::size_t index = 0U; index < found.written; ++index) visible_nodes_.push_back(query_[index]);
    std::sort(visible_nodes_.begin(), visible_nodes_.end());

    paint_links(painter);
    paint_nodes(painter);
    paint_overlay(painter);
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
        paint_part(painter, *this, focus);
    }
    statistics_.last_frame_nodes = visible_nodes_.size();
    painted_lamp_revision_ = (*graph_).lamp_revision();
}

void NodeCanvas::paint_links(Painter& painter) {
    const NodeGraph& graph = *graph_;
    const std::span<const LinkRecord> links = graph.link_slots();
    // Strokes reach half their width and their halo past their paths.
    const std::size_t selected_state = static_cast<std::size_t>(CanvasLinkState::selected);
    const CanvasLine& widest = look_.links[selected_state];
    const double reach_view = std::max(link_width_maximum, widest.width) / 2.0 + widest.halo_width;
    const double reach = reach_view / view_.zoom;
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
        const std::size_t count = link_path(slot, link, segments, points);
        if (count < 2U) continue;
        statistics_.link_points_flattened += count;
        const std::span<const Point> path(link_points_.data(), count);
        const LinkId id{slot, link.generation};
        const bool chosen = selected_link_.has_value() && *selected_link_ == id;
        const CanvasLinkState state = chosen ? CanvasLinkState::selected : CanvasLinkState::normal;
        paint_canvas_link(painter, look_, path, state, CanvasLinkStyle::plain);
        ++painted;
    }
    statistics_.links_painted += painted;
    statistics_.last_frame_links = painted;
}

void NodeCanvas::paint_nodes(Painter& painter) {
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    if (level_ == SceneDetailLevel::overview) {
        // Silhouettes only: two batched fills in the resting and the
        // selected node's colours.
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
        painter.fill_rects(std::span<const Rect>(overview_rects_.data(), overview_rects_.size()),
                           look_.nodes[node_rest].fill);
        painter.fill_rects(std::span<const Rect>(overview_selected_.data(), overview_selected_.size()),
                           look_.nodes[node_chosen].fill);
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
    const CanvasNodeState state = canvas_node_state(states);
    const double zoom = view_.zoom;
    if (states.contains(AnatomyState::selected)) paint_canvas_node_selection(painter, look_, rect);
    if (level_ == SceneDetailLevel::silhouette) {
        // The body alone: no header, ports, text or body callback.
        paint_canvas_node(painter, look_, rect, 0.0, state);
        if (states.contains(AnatomyState::focus_visible)) {
            PartPaint cue;
            cue.role = PartRole::focus;
            cue.family = ControlVisualRole::editor;
            cue.bounds = rect;
            cue.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, cue);
        }
        return;
    }

    // The body with its header band, then the title in the header's ink,
    // clipped to the band less the corners.
    const double caption = std::min(caption_height * zoom, rect.height);
    paint_canvas_node(painter, look_, rect, caption, state);
    const double title_size = caption_text_size * zoom;
    if (title_size >= smallest_text) {
        const double inset = node_corner_radius * zoom;
        const Rect band{rect.x + inset, rect.y, std::max(0.0, rect.width - 2.0 * inset), caption};
        FontSpec base{};
        base.size = title_size;
        const FontSpec font = effective_font(base);
        const Point baseline{band.x, band.y + band.height / 2.0 + title_baseline_share * title_size};
        painter.save();
        painter.clip_rect(band);
        painter.draw_text_utf8(baseline, node.title, font, look_.titles[static_cast<std::size_t>(state)]);
        painter.restore();
        ++statistics_.captions_drawn;
    } else {
        ++statistics_.text_skipped_small;
    }

    if (level_ == SceneDetailLevel::full) {
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
    // Ports over the body (and the body callback), so a port on the edge is
    // never covered by its own node.
    paint_ports(painter, slot, node);
    if (states.contains(AnatomyState::focus_visible) && !focused_port_.has_value()) {
        PartPaint cue;
        cue.role = PartRole::focus;
        cue.family = ControlVisualRole::editor;
        cue.bounds = rect;
        cue.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, cue);
    }
}

CanvasPortState NodeCanvas::port_state(std::uint32_t slot, std::size_t port) const {
    const bool linking = (gesture_ == NodeCanvasGesture::link && gesture_planned_) || keyboard_link_;
    if (!linking) return CanvasPortState::normal;
    // The dragged link's own end, and the input it is captured by, are hot.
    if (link_from_.node.index == slot && link_from_.port.value == port) return CanvasPortState::hot;
    std::optional<std::size_t> target{};
    if (keyboard_link_ && keyboard_target_ < candidates_.size()) target = keyboard_target_;
    if (!keyboard_link_ && link_hit_.has_value() && *link_hit_ < candidates_.size()) target = *link_hit_;
    if (target.has_value()) {
        const PortRef captured = candidates_[*target].port;
        if (captured.node.index == slot && captured.port.value == port) return CanvasPortState::hot;
    }
    // The magnet's candidates take the link; any other port refuses it.
    // Candidates are looked up only on flagged nodes.
    const bool flagged = slot < candidate_flags_.size() && candidate_flags_[slot] != 0U;
    if (flagged) {
        for (const LinkCandidate& candidate : candidates_) {
            if (candidate.port.node.index == slot && candidate.port.port.value == port) {
                return CanvasPortState::drop_accepted;
            }
        }
    }
    return CanvasPortState::drop_rejected;
}

void NodeCanvas::paint_ports(Painter& painter, std::uint32_t slot, const NodeRecord& node) {
    // Ports, lamps beside outputs, and at full detail the port labels.
    const double zoom = view_.zoom;
    const double radius = port_radius(zoom);
    const double lamp = lamp_radius(zoom);
    const bool full = level_ == SceneDetailLevel::full;
    const double label_size = port_label_size * zoom;
    const bool labels = full && label_size >= smallest_text;
    if (full && label_size < smallest_text) ++statistics_.text_skipped_small;
    FontSpec label_base{};
    label_base.size = label_size;
    const FontSpec label_font = effective_font(label_base);
    const Rect rect = view_.rect_to_view(node.bounds);
    const Window* const host = attached_window();
    const bool node_focus_cue = focused_node_.has_value() && (*focused_node_).index == slot && focused_ &&
                                host != nullptr && (*host).focus_cue_visible();
    for (std::size_t port = 0U; port < node.ports.size(); ++port) {
        const PortRecord& record = node.ports[port];
        const Point anchor = view_.to_view(port_world(node, record));
        paint_canvas_port(painter, look_, anchor, radius, port_state(slot, port));
        ++statistics_.ports_painted;
        if (record.direction == PortDirection::output) {
            const Point centre = lamp_centre(node, record, zoom, view_);
            paint_canvas_lamp(painter, look_, centre, lamp, canvas_lamp_state(record.lamp));
            ++statistics_.lamps_painted;
        }
        if (labels) {
            // Inputs label to the inside of a left edge, outputs to the
            // inside of a right edge; a label never leaves its node.
            const double gap = port_label_gap * zoom;
            const double height = label_size * 1.4;
            const bool leading = effective_side(record) != PortSide::right;
            const double width = std::max(0.0, rect.width / 2.0 - gap);
            PartPaint label;
            label.role = PartRole::label;
            label.family = ControlVisualRole::card;
            label.bounds = leading ? Rect{anchor.x + gap, anchor.y - height / 2.0, width, height}
                                   : Rect{anchor.x - gap - width, anchor.y - height / 2.0, width, height};
            label.alignment = leading ? PartTextAlignment::start : PartTextAlignment::end;
            label.font = label_font;
            label.text = lend_paint_text(record.name);
            paint_part(painter, *this, label);
        }
        if (node_focus_cue && focused_port_.has_value() && (*focused_port_).value == port) {
            const double ring = 2.0 * radius + 6.0;
            PartPaint cue;
            cue.role = PartRole::focus;
            cue.family = ControlVisualRole::editor;
            cue.bounds = Rect{anchor.x - ring / 2.0, anchor.y - ring / 2.0, ring, ring};
            cue.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, cue);
        }
    }
}

void NodeCanvas::paint_overlay(Painter& painter) {
    const bool linking = (gesture_ == NodeCanvasGesture::link && gesture_planned_) || keyboard_link_;
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::marquee) {
        const Rect marquee{std::min(press_view_.x, current_view_.x), std::min(press_view_.y, current_view_.y),
                           std::fabs(current_view_.x - press_view_.x), std::fabs(current_view_.y - press_view_.y)};
        paint_canvas_marquee(painter, look_, marquee);
        return;
    }
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::knife) {
        // The knife cuts: an invalid dashed link along the stroke.
        const std::array<Point, 2> stroke{press_view_, current_view_};
        paint_canvas_link(painter, look_, std::span<const Point>(stroke.data(), stroke.size()), CanvasLinkState::invalid,
                          CanvasLinkStyle::dashed);
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
    // The preview: a hot link, solid once captured, dashed while it dangles.
    const CanvasLinkStyle style = target.has_value() ? CanvasLinkStyle::plain : CanvasLinkStyle::dashed;
    paint_canvas_link(painter, look_, std::span<const Point>(link_points_.data(), count), CanvasLinkState::hot, style);
    if (!target.has_value()) return;
    // The drop target: the accepting port's area, ringed larger.
    const Point at = view_.to_view(end);
    paint_canvas_port(painter, look_, at, drop_ring / 2.0, CanvasPortState::drop_accepted);
}

} // namespace gui_forms
