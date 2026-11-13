// NodeCanvas's painting (node graph spec sections 3 and 4): the canvas
// background and world grid, the visible links and their flow, the visible
// nodes by level of detail with their ports, lamps and labels, the focus
// rings, and the gesture overlay. Every colour, line and the grid come from
// the theme's canvas look (canvas_look.hpp): the canvas-wide parts from
// look_, resolved once (ensure_look) and again only when the theme, colour
// scheme, contrast, classes or back_color change; an item's parts (a node,
// its ports, a link) from the item's own look, resolved the first time the
// item is shown after that and kept (resolve_node_look, resolve_link_look).
// Each frame paints from those records with the paint_canvas_ functions.
// Nothing here changes the graph, and nothing allocates per node except an
// item's first resolution: the visible sets use storage sized at the cache
// rebuilds.

#include "gui_forms/controls/node_canvas.hpp"

#include "node_canvas_detail.hpp"

#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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
constexpr std::size_t link_rest = static_cast<std::size_t>(CanvasLinkState::normal);
// A socket's item class: "socket-" and the socket type in decimal.
constexpr std::string_view socket_class_prefix = "socket-";

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

// Writes the socket class of `socket_type` into `text` (cleared first).
void write_socket_class(std::string& text, std::uint16_t socket_type) {
    text.assign(socket_class_prefix);
    std::array<char, 8U> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), socket_type);
    text.append(digits.data(), static_cast<std::size_t>(written.ptr - digits.data()));
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
    // Every kept item record is stale; the item paint calls start from the
    // new look.
    ++look_stamp_;
    item_look_ = look_;
}

// -- item looks -----------------------------------------------------------------------------

void NodeCanvas::resolve_node_look(std::uint32_t slot, const NodeRecord& node) {
    NodeItemLook& kept = node_looks_[slot];
    const Theme& theme = effective_theme();
    ControlVisualContext scheme{};
    scheme.dark = look_dark_;
    scheme.high_contrast = look_high_contrast_;
    // The node: its key as ID, no classes.
    const CanvasItem node_item{node.key, std::span<const std::string>{}};
    const CanvasLook node_resolved = resolve_canvas_look(theme, *this, scheme, node_item);
    kept.nodes = node_resolved.nodes;
    kept.headers = node_resolved.headers;
    kept.titles = node_resolved.titles;
    kept.selection = node_resolved.selection;
    kept.focus = node_resolved.focus;
    ++statistics_.item_looks_resolved;
    // Each port: "<node key>.<port name>" with its socket class.
    kept.ports.resize(node.ports.size());
    item_classes_.resize(1U);
    for (std::size_t port = 0U; port < node.ports.size(); ++port) {
        const PortRecord& record = node.ports[port];
        item_id_.assign(node.key);
        item_id_.push_back('.');
        item_id_.append(record.name);
        write_socket_class(item_classes_[0], record.socket_type);
        const CanvasItem port_item{item_id_, std::span<const std::string>(item_classes_.data(), item_classes_.size())};
        const CanvasLook port_resolved = resolve_canvas_look(theme, *this, scheme, port_item);
        PortItemLook& parts = kept.ports[port];
        parts.ports = port_resolved.ports;
        parts.labels = port_resolved.port_labels;
        parts.lamps = port_resolved.lamps;
        parts.focus = port_resolved.focus;
        parts.previews = port_resolved.link_previews;
        ++statistics_.item_looks_resolved;
    }
    kept.stamp = look_stamp_;
    kept.generation = node.generation;
}

void NodeCanvas::resolve_link_look(std::uint32_t slot, const LinkRecord& link) {
    LinkItemLook& kept = link_looks_[slot];
    const Theme& theme = effective_theme();
    ControlVisualContext scheme{};
    scheme.dark = look_dark_;
    scheme.high_contrast = look_high_contrast_;
    // The link: its key as ID, its output's socket class.
    const PortRecord* const from = (*graph_).port(link.from);
    std::span<const std::string> link_classes{};
    if (from != nullptr) {
        item_classes_.resize(1U);
        write_socket_class(item_classes_[0], (*from).socket_type);
        link_classes = std::span<const std::string>(item_classes_.data(), item_classes_.size());
    }
    const CanvasItem item{link.key, link_classes};
    const CanvasLook resolved = resolve_canvas_look(theme, *this, scheme, item);
    kept.links = resolved.links;
    kept.dashed_links = resolved.dashed_links;
    kept.stamp = look_stamp_;
    kept.generation = link.generation;
    ++statistics_.item_looks_resolved;
}

const CanvasLook& NodeCanvas::node_look(std::uint32_t slot, const NodeRecord& node) {
    // A slot beyond the records (the caches not yet rebuilt) paints in the
    // canvas's look.
    if (slot >= node_looks_.size()) {
        item_look_ = look_;
        return item_look_;
    }
    const NodeItemLook& kept = node_looks_[slot];
    const bool fresh = kept.stamp == look_stamp_ && kept.generation == node.generation &&
                       kept.ports.size() == node.ports.size();
    if (!fresh) resolve_node_look(slot, node);
    item_look_.nodes = kept.nodes;
    item_look_.headers = kept.headers;
    item_look_.titles = kept.titles;
    item_look_.selection = kept.selection;
    item_look_.focus = kept.focus;
    return item_look_;
}

const CanvasLook& NodeCanvas::port_look(std::uint32_t slot, const NodeRecord& node, std::size_t port) {
    // node_look runs first for the node, so its record is fresh here.
    const bool known = slot < node_looks_.size() && port < node_looks_[slot].ports.size() &&
                       node_looks_[slot].stamp == look_stamp_;
    if (!known) {
        static_cast<void>(node_look(slot, node));
        if (slot >= node_looks_.size() || port >= node_looks_[slot].ports.size()) return item_look_;
    }
    const PortItemLook& parts = node_looks_[slot].ports[port];
    item_look_.ports = parts.ports;
    item_look_.port_labels = parts.labels;
    item_look_.lamps = parts.lamps;
    item_look_.focus = parts.focus;
    item_look_.link_previews = parts.previews;
    return item_look_;
}

const CanvasLook& NodeCanvas::link_look(std::uint32_t slot, const LinkRecord& link) {
    if (slot >= link_looks_.size()) {
        item_look_ = look_;
        return item_look_;
    }
    const LinkItemLook& kept = link_looks_[slot];
    const bool fresh = kept.stamp == look_stamp_ && kept.generation == link.generation;
    if (!fresh) resolve_link_look(slot, link);
    item_look_.links = kept.links;
    item_look_.dashed_links = kept.dashed_links;
    return item_look_;
}

// -- the frame ------------------------------------------------------------------------------

void NodeCanvas::on_paint(Painter& painter, Rect) {
    ++statistics_.frames_painted;
    const Rect client = client_rectangle();
    ensure_look();
    // The background first, untranslated and unclipped: the region, the
    // texture and the grid anchored at world (0, 0).
    const CanvasView canvas_view{view_.to_view(WorldPoint{0.0, 0.0}), view_.zoom};
    paint_canvas_background(painter, *this, look_, client, canvas_view);
    visible_nodes_.clear();
    visible_links_.clear();
    flow_view_.links.count = 0U;
    if (graph_ == nullptr) {
        update_flow_ambient(true);
        return;
    }
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
    paint_flow(painter);
    paint_nodes(painter);
    paint_overlay(painter);
    painter.restore();

    // With no node to carry the focus cue, the canvas itself shows it: the
    // focus ring just inside the canvas.
    bool node_carries_focus = false;
    if (focused_node_.has_value()) {
        node_carries_focus = std::binary_search(visible_nodes_.begin(), visible_nodes_.end(), (*focused_node_).index);
    }
    const Window* const host = attached_window();
    if (focused_ && !node_carries_focus && host != nullptr && (*host).focus_cue_visible()) {
        const double inset = canvas_focus_gap + std::max(minimum_canvas_focus_width, look_.focus.stroke_width);
        const Rect inside{client.x + inset, client.y + inset, std::max(0.0, client.width - 2.0 * inset),
                          std::max(0.0, client.height - 2.0 * inset)};
        paint_canvas_focus(painter, look_, inside, 0.0);
        ++statistics_.focus_rings_painted;
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
        paint_canvas_link(painter, link_look(slot, link), path, state, CanvasLinkStyle::plain);
        ++painted;
        // A flowing link at rest joins the flow's snapshot, its path
        // sampled down to the effect's batch.
        const bool flows = !chosen && std::binary_search(flowing_.begin(), flowing_.end(), id, link_before);
        if (!flows || flow_view_.links.count >= maximum_flow_links) continue;
        EffectPoints& target = flow_view_.links.items[flow_view_.links.count];
        const std::size_t kept = std::min(count, maximum_effect_batch);
        for (std::size_t index = 0U; index < kept; ++index) {
            // Evenly spaced samples, the first and last points included.
            const std::size_t source = kept < 2U ? index : (index * (count - 1U)) / (kept - 1U);
            target.items[index] = path[source];
        }
        target.count = static_cast<std::uint32_t>(kept);
        flow_view_.links.count = flow_view_.links.count + 1U;
    }
    statistics_.links_painted += painted;
    statistics_.last_frame_links = painted;
}

void NodeCanvas::paint_flow(Painter& painter) {
    // The marks over the flowing links: the theme's flow on the link part,
    // in the canvas's own colour, at the link's width; still (phase 0)
    // while the flow does not move.
    flow_view_.style = look_.flow.style;
    flow_view_.speed = look_.flow.speed * view_.zoom;
    flow_view_.spacing = look_.flow.spacing;
    flow_view_.width = look_.links[link_rest].width;
    const EffectEntry* const entry = built_in_effect_registry().find(LinkFlow::declaration.name);
    if (entry != nullptr && flow_.entry() != entry) {
        flow_.attach(*entry, effect_default_seed(stable_id().value(), LinkFlow::declaration.name), FrameClock::now());
    }
    flow_paints_ = flow_paints_ + 1U;
    update_flow_ambient(true);
    if (flow_.entry() == nullptr || flow_view_.links.count == 0U) return;
    EffectColours colours{};
    colours.primary = link_flow_mark_colour(look_);
    flow_.set_colours(colours);
    const SurfaceRoot* const root = attached_surface_root();
    const bool suppressed = root != nullptr && (*root).motion_clock().ambient_suppressed();
    const bool still = !flow_moves() || suppressed;
    const Window* const host = attached_window();
    const double scale = host != nullptr ? (*host).scale() : 1.0;
    flow_.paint(painter, client_rectangle(), scale, still, flow_view_);
    statistics_.flow_links_painted += flow_view_.links.count;
}

void NodeCanvas::paint_nodes(Painter& painter) {
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    if (level_ == SceneDetailLevel::overview) {
        // Silhouettes only: two batches in the canvas look's resting and
        // selected node fills (no item looks at this distance).
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
        const std::span<const Rect> resting_bodies(overview_rects_.data(), overview_rects_.size());
        const std::span<const Rect> selected_bodies(overview_selected_.data(), overview_selected_.size());
        paint_canvas_overview(painter, look_, resting_bodies, CanvasNodeState::normal);
        paint_canvas_overview(painter, look_, selected_bodies, CanvasNodeState::selected);
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
    const double corner = node_corner_radius * zoom;
    const CanvasLook& look = node_look(slot, node);
    if (states.contains(AnatomyState::selected)) paint_canvas_node_selection(painter, look, rect);
    if (level_ == SceneDetailLevel::silhouette) {
        // The body alone: no header, ports, text or body callback.
        paint_canvas_node(painter, look, rect, 0.0, state);
        if (states.contains(AnatomyState::focus_visible)) {
            paint_canvas_focus(painter, look, rect, corner);
            ++statistics_.focus_rings_painted;
        }
        return;
    }

    // The body with its header band, then the title in the header's ink,
    // clipped to the band less the corners.
    const double caption = std::min(caption_height * zoom, rect.height);
    paint_canvas_node(painter, look, rect, caption, state);
    const double title_size = caption_text_size * zoom;
    if (title_size >= smallest_text) {
        const Rect band{rect.x + corner, rect.y, std::max(0.0, rect.width - 2.0 * corner), caption};
        FontSpec base{};
        base.size = title_size;
        const FontSpec font = effective_font(base);
        const Point baseline{band.x, band.y + band.height / 2.0 + title_baseline_share * title_size};
        paint_canvas_node_title(painter, look, band, baseline, node.title, font, state);
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
        // node_look again: the ports wrote their own focus record.
        paint_canvas_focus(painter, node_look(slot, node), rect, corner);
        ++statistics_.focus_rings_painted;
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
        const CanvasPortState state = port_state(slot, port);
        const CanvasLook& look = port_look(slot, node, port);
        paint_canvas_port(painter, look, anchor, radius, state);
        ++statistics_.ports_painted;
        if (record.direction == PortDirection::output) {
            const Point centre = lamp_centre(node, record, zoom, view_);
            paint_canvas_lamp(painter, look, centre, lamp, canvas_lamp_state(record.lamp));
            ++statistics_.lamps_painted;
        }
        if (labels) {
            // Inputs label to the inside of a left edge, outputs to the
            // inside of a right edge, clipped to their half of the node.
            const double gap = port_label_gap * zoom;
            const bool leading = effective_side(record) != PortSide::right;
            const double width = std::max(0.0, rect.width / 2.0 - gap);
            const double height = label_size * 1.4;
            const Rect room = leading ? Rect{anchor.x + gap, anchor.y - height / 2.0, width, height}
                                      : Rect{anchor.x - gap - width, anchor.y - height / 2.0, width, height};
            double left = room.x;
            if (!leading) {
                const Size measured = painter.measure_text_utf8(record.name, label_font);
                left = std::max(room.x, room.right() - measured.width);
            }
            const Point baseline{left, anchor.y + title_baseline_share * label_size};
            painter.save();
            painter.clip_rect(room);
            paint_canvas_port_label(painter, look, baseline, record.name, label_font, state);
            painter.restore();
        }
        if (node_focus_cue && focused_port_.has_value() && (*focused_port_).value == port) {
            const Rect disc{anchor.x - radius, anchor.y - radius, 2.0 * radius, 2.0 * radius};
            paint_canvas_focus(painter, look, disc, radius);
            ++statistics_.focus_rings_painted;
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
        const std::array<Point, 2> stroke{press_view_, current_view_};
        paint_canvas_knife(painter, look_, std::span<const Point>(stroke.data(), stroke.size()));
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
    // The preview in its state, in the look of the port it leaves (its
    // socket's rules).
    const std::optional<CanvasPreviewState> state = link_preview_state();
    const CanvasPreviewState shown = state.has_value() ? *state : CanvasPreviewState::seeking;
    const CanvasLook& look = port_look(link_from_.node.index, *from_node, link_from_.port.value);
    paint_canvas_link_preview(painter, look, std::span<const Point>(link_points_.data(), count), shown);
}

// -- flow motion ----------------------------------------------------------------------------

bool NodeCanvas::flow_moves() const noexcept {
    const SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr || flow_.entry() == nullptr) return false;
    const PresentationSettings& settings = (*root).presentation_settings();
    if (!settings.ambient_motion) return false;
    return link_flow_wants_frames(flow_view_, settings.reduced_motion);
}

void NodeCanvas::update_flow_ambient(bool painting) {
    SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr) return;
    MotionClock& clock = (*root).motion_clock();
    // No flowing link shown, no marks, reduced motion or the motion switch
    // off: the flow leaves the clock.
    if (!flow_moves()) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    if (!painting && !clock.control_ambient(*this)) return;
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(
        std::chrono::duration<double>(1.0 / LinkFlow::declaration.frames_per_second));
    const bool started = clock.enlist_control_ambient(*this, interval);
    if (!started) return;
    // The flow starts or resumes: its time restarts, with no catch-up for
    // the pause.
    flow_.rebase(clock.now());
    flow_paints_seen_ = ~std::uint64_t{0};
}

bool NodeCanvas::on_ambient_frame(FrameTime now) {
    if (flow_paints_ == flow_paints_seen_) return false;
    flow_paints_seen_ = flow_paints_;
    static_cast<void>(flow_.advance(now));
    ++statistics_.flow_frames;
    invalidate(Dirty::paint);
    return true;
}

void NodeCanvas::on_attached_to_window() {
    Control::on_attached_to_window();
    SurfaceRoot* const root = attached_surface_root();
    if (root != nullptr) {
        presentation_subscription_ = (*root).presentation_changed().subscribe(
            *this,
            Delegate<const PresentationSettings&>::bind<NodeCanvas, &NodeCanvas::on_presentation_changed>(*this));
    }
    update_flow_ambient(false);
}

void NodeCanvas::on_presentation_changed(const PresentationSettings&) {
    // Reduced motion or the motion switch withdraws the flow at once; the
    // next paint enlists it again when it may move.
    update_flow_ambient(false);
    invalidate(Dirty::paint);
}

} // namespace gui_forms
