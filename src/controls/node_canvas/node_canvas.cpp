// NodeCanvas's model side: construction, the view, selection and focus, the
// graph operations, the revision-keyed caches (the two spatial indexes and
// the per-frame storage), hit testing, editing, semantics and the anatomy.
// Input is in node_canvas_input.cpp and painting in node_canvas_paint.cpp.

#include "gui_forms/controls/node_canvas.hpp"

#include "node_canvas_detail.hpp"

#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gui_forms {

using namespace node_canvas_detail;

namespace {

// The digits of the largest slot or generation, for the node identities.
constexpr std::size_t identity_digits_capacity = 12U;
constexpr std::string_view node_segment = ".node-";
constexpr std::string_view port_segment = ".port-";
constexpr std::string_view announcements_segment = ".announcements";
constexpr double preferred_extent = 240.0;

[[nodiscard]] std::size_t write_decimal(std::span<char> digits, std::uint64_t value) noexcept {
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    const std::size_t length = static_cast<std::size_t>(written.ptr - digits.data());
    return length;
}

void append_decimal(std::string& text, std::uint64_t value) {
    std::array<char, 24U> digits{};
    const std::size_t length = write_decimal(digits, value);
    text.append(digits.data(), length);
}

// Parses a decimal at the front of `text` into `value`; returns how many
// characters it read (0 for none, a leading zero before more digits, or an
// overflow).
[[nodiscard]] std::size_t read_decimal(std::string_view text, std::uint32_t& value) noexcept {
    if (text.empty() || text.front() < '0' || text.front() > '9') return 0U;
    if (text.front() == '0' && text.size() > 1U && text[1] >= '0' && text[1] <= '9') return 0U;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{}) return 0U;
    const std::size_t read = static_cast<std::size_t>(parsed.ptr - text.data());
    return read;
}

constexpr std::array<PartField<NodeCanvas::Parts>, 9> node_canvas_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &NodeCanvas::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &NodeCanvas::Parts::well},
    {.declaration = {.name = "grid_minor", .role = PartRole::grid_minor, .composed_from = {}},
     .member = &NodeCanvas::Parts::grid_minor},
    {.declaration = {.name = "grid_major", .role = PartRole::grid_major, .composed_from = {}},
     .member = &NodeCanvas::Parts::grid_major},
    // The links: a series of curves (no connector role exists yet).
    {.declaration = {.name = "connector", .role = PartRole::series, .composed_from = {}},
     .member = &NodeCanvas::Parts::connector},
    {.declaration = {.name = "marquee", .role = PartRole::focus, .composed_from = {}},
     .member = &NodeCanvas::Parts::marquee},
    {.declaration = {.name = "knife", .role = PartRole::trace, .composed_from = {}},
     .member = &NodeCanvas::Parts::knife},
    {.declaration = {.name = "drop", .role = PartRole::drop, .composed_from = {}},
     .member = &NodeCanvas::Parts::drop},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &NodeCanvas::Parts::focus},
}};

constexpr std::array<StateField<NodeCanvas::States>, 5> node_canvas_state_fields{{
    {.state = AnatomyState::normal, .member = &NodeCanvas::States::normal},
    {.state = AnatomyState::enabled, .member = &NodeCanvas::States::enabled},
    {.state = AnatomyState::disabled, .member = &NodeCanvas::States::disabled},
    {.state = AnatomyState::focused, .member = &NodeCanvas::States::focused},
    {.state = AnatomyState::focus_visible, .member = &NodeCanvas::States::focus_visible},
}};

constexpr std::array<PartField<NodeCanvas::NodeParts>, 8> node_canvas_node_part_fields{{
    {.declaration = {.name = "node", .role = PartRole::region, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::node},
    {.declaration = {.name = "node_face", .role = PartRole::face, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::node_face},
    {.declaration = {.name = "node_selection", .role = PartRole::selection, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::node_selection},
    {.declaration = {.name = "node_caption", .role = PartRole::caption, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::node_caption},
    // A port's marker (no port role exists yet).
    {.declaration = {.name = "port", .role = PartRole::marker, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::port},
    {.declaration = {.name = "port_label", .role = PartRole::label, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::port_label},
    {.declaration = {.name = "lamp", .role = PartRole::indicator, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::lamp},
    {.declaration = {.name = "node_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &NodeCanvas::NodeParts::node_focus},
}};

constexpr std::array<StateField<NodeCanvas::NodeStates>, 5> node_canvas_node_state_fields{{
    {.state = AnatomyState::selected, .member = &NodeCanvas::NodeStates::selected},
    {.state = AnatomyState::hot, .member = &NodeCanvas::NodeStates::hot},
    {.state = AnatomyState::current, .member = &NodeCanvas::NodeStates::current},
    {.state = AnatomyState::disabled, .member = &NodeCanvas::NodeStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &NodeCanvas::NodeStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 9> node_canvas_parts = part_declarations(node_canvas_part_fields);
constexpr std::array<AnatomyState, 5> node_canvas_states = state_declarations(node_canvas_state_fields);
constexpr std::array<PartDeclaration, 8> node_canvas_node_parts = part_declarations(node_canvas_node_part_fields);
constexpr std::array<AnatomyState, 5> node_canvas_node_states = state_declarations(node_canvas_node_state_fields);

constexpr ItemSystemIndex node_system{0U};

constexpr std::array<ItemSystemDeclaration, 1> node_canvas_item_systems{{
    {.name = "node", .parts = node_canvas_node_parts, .states = node_canvas_node_states, .custom_states = {}},
}};

constexpr ControlAnatomy node_canvas_anatomy{
    .control_id = "node_canvas",
    .parts = node_canvas_parts,
    .states = node_canvas_states,
    .custom_states = {},
    .item_systems = node_canvas_item_systems,
};

// The events NodeCanvas adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 6> node_canvas_interactions{{
    {.name = "selection_changed", .payload = "NodeCanvasSelection", .mutable_payload = false},
    {.name = "link_connected", .payload = "NodeLinkConnected", .mutable_payload = false},
    {.name = "graph_edited", .payload = "NodeGraphEdited", .mutable_payload = false},
    {.name = "view_changed", .payload = "SceneViewTransform", .mutable_payload = false},
    {.name = "palette_requested", .payload = "NodePaletteRequest", .mutable_payload = false},
    {.name = "edit_ended", .payload = "NodeEditEnded", .mutable_payload = false},
}};

[[nodiscard]] Rect inflated(Rect rect, double margin) noexcept {
    const Rect grown{rect.x - margin, rect.y - margin, rect.width + 2.0 * margin, rect.height + 2.0 * margin};
    return grown;
}

[[nodiscard]] double distance_between(Point left, Point right) noexcept {
    const double distance = std::hypot(left.x - right.x, left.y - right.y);
    return distance;
}

} // namespace

bool NodeCanvas::CandidateNearer::operator()(const LinkCandidate& left, const LinkCandidate& right) const noexcept {
    return left.distance < right.distance;
}

// -- life ---------------------------------------------------------------------------------

NodeCanvas::NodeCanvas(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    link_points_.resize(maximum_link_points);
    candidates_.reserve(maximum_drag_targets);
    targets_.reserve(maximum_drag_targets);
    announcement_.reserve(96U);
    text_.reserve(96U);
    painted_lamp_revision_ = 0U;
    drag_ = std::make_unique<DragTracker>(*this);
    DragTrackerCallbacks drag_callbacks{};
    drag_callbacks.plan = Delegate<DragPlan&>::bind<NodeCanvas, &NodeCanvas::plan_drag>(*this);
    drag_callbacks.preview = Delegate<const DragTrackerFrame&>::bind<NodeCanvas, &NodeCanvas::preview_drag>(*this);
    drag_callbacks.finished = Delegate<const DragTrackerFrame&>::bind<NodeCanvas, &NodeCanvas::finish_drag>(*this);
    drag_callbacks.announce = Delegate<DragAnnouncementRequest&>::bind<NodeCanvas, &NodeCanvas::announce_drag>(*this);
    drag_callbacks.scroll = Delegate<DragAutoScrollStep&>::bind<NodeCanvas, &NodeCanvas::scroll_drag>(*this);
    (*drag_).callbacks = drag_callbacks;
}

NodeCanvas::~NodeCanvas() {
    // The tracker first: its end may still call finish_drag, which closes
    // the gesture's group. A group still open after it must not outlive
    // the canvas.
    drag_.reset();
    if (graph_ != nullptr && (gesture_group_open_ || edit_group_open_)) {
        static_cast<void>((*graph_).rollback_undo_group());
    }
}

Size NodeCanvas::measure(Size available) {
    // A canvas takes what it is given; unconstrained, a modest square.
    double width = available.width;
    double height = available.height;
    if (!std::isfinite(width)) width = preferred_extent;
    if (!std::isfinite(height)) height = preferred_extent;
    const Size size{width, height};
    return size;
}

void NodeCanvas::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // The edit host's slot follows the node through pans and zooms.
    if (editor_ != nullptr) set_child_layout(editor_, edit_bounds());
}

DragTrackerStatistics NodeCanvas::drag_statistics() const {
    if (drag_ == nullptr) return DragTrackerStatistics{};
    return (*drag_).statistics();
}

// -- graph and targets -------------------------------------------------------------------

void NodeCanvas::set_graph(NodeGraph* graph) {
    if (in_callback_ || graph == graph_) return;
    if (drag_ != nullptr && (*drag_).running()) (*drag_).cancel(DragEnd::model_changed);
    settle_unplanned_gesture();
    if (editor_ != nullptr) end_edit(NodeEditEnd::cancel);
    if (graph_ != nullptr && (gesture_group_open_ || edit_group_open_)) {
        static_cast<void>((*graph_).rollback_undo_group());
    }
    gesture_group_open_ = false;
    edit_group_open_ = false;
    keyboard_link_ = false;
    graph_ = graph;
    selection_.clear();
    selected_flags_.clear();
    selected_link_.reset();
    focused_node_.reset();
    focused_port_.reset();
    hot_node_.reset();
    pending_click_select_.reset();
    visible_nodes_.clear();
    visible_links_.clear();
    drop_caches();
    invalidate(Dirty::paint | Dirty::semantics);
}

void NodeCanvas::set_callbacks(NodeCanvasCallbacks callbacks) {
    if (in_callback_) return;
    callbacks_ = callbacks;
    invalidate(Dirty::paint);
}

void NodeCanvas::refresh() {
    refresh_selection_flags();
    if (graph_ != nullptr && (*graph_).lamp_revision() != painted_lamp_revision_) ++statistics_.lamp_repaints;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- view ---------------------------------------------------------------------------------

bool NodeCanvas::set_view(const SceneViewTransform& view) {
    if (!valid_scene_view(view)) return false;
    SceneViewTransform held = view;
    held.zoom = clamp_scene_zoom(view.zoom, zoom_range_);
    if (held == view_) return true;
    view_ = held;
    if (editor_ != nullptr) invalidate(Dirty::arrange);
    invalidate(Dirty::paint | Dirty::semantics);
    view_changed_.emit(view_);
    return true;
}

void NodeCanvas::zoom_about(Point local, double factor) {
    const SceneViewTransform zoomed = scene_zoomed_about(view_, local, factor, zoom_range_);
    static_cast<void>(set_view(zoomed));
}

void NodeCanvas::pan_by(Point delta) {
    const SceneViewTransform panned = scene_panned(view_, delta);
    static_cast<void>(set_view(panned));
}

void NodeCanvas::frame_all() {
    if (graph_ == nullptr) return;
    bool any = false;
    WorldRect box{};
    for (const NodeRecord& node : (*graph_).node_slots()) {
        if (!node.live) continue;
        box = any ? world_rect_union(box, node.bounds) : node.bounds;
        any = true;
    }
    if (!any) return;
    const Rect client = client_rectangle();
    const SceneViewTransform framed =
        scene_framed(view_, box, Size{client.width, client.height}, frame_all_margin, zoom_range_);
    static_cast<void>(set_view(framed));
}

void NodeCanvas::frame_selection() {
    if (graph_ == nullptr) return;
    bool any = false;
    WorldRect box{};
    for (const NodeId id : selection_) {
        const NodeRecord* node = (*graph_).node(id);
        if (node == nullptr) continue;
        box = any ? world_rect_union(box, (*node).bounds) : (*node).bounds;
        any = true;
    }
    if (!any) return;
    const Rect client = client_rectangle();
    const SceneViewTransform framed =
        scene_framed(view_, box, Size{client.width, client.height}, frame_selection_margin, zoom_range_);
    static_cast<void>(set_view(framed));
}

void NodeCanvas::reveal_node(NodeId node) {
    if (graph_ == nullptr) return;
    const NodeRecord* record = (*graph_).node(node);
    if (record == nullptr) return;
    const Rect client = client_rectangle();
    const Rect shown = inflated(view_.rect_to_view((*record).bounds), reveal_margin);
    double dx = 0.0;
    double dy = 0.0;
    if (shown.x < client.x) dx = client.x - shown.x;
    if (shown.x + shown.width > client.x + client.width) dx = client.x + client.width - (shown.x + shown.width);
    if (shown.y < client.y) dy = client.y - shown.y;
    if (shown.y + shown.height > client.y + client.height) dy = client.y + client.height - (shown.y + shown.height);
    // Larger than the view: its top-left corner wins.
    if (shown.width > client.width) dx = client.x - shown.x;
    if (shown.height > client.height) dy = client.y - shown.y;
    if (dx != 0.0 || dy != 0.0) pan_by(Point{dx, dy});
}

bool NodeCanvas::set_detail_thresholds(const SceneDetailThresholds& thresholds) {
    if (!valid_scene_detail_thresholds(thresholds)) return false;
    thresholds_ = thresholds;
    invalidate(Dirty::paint);
    return true;
}

void NodeCanvas::set_link_style(NodeLinkStyle style) {
    if (style == link_style_) return;
    link_style_ = style;
    invalidate(Dirty::paint);
}

void NodeCanvas::set_router_options(const LinkRouterOptions& options) {
    router_options_ = options;
    // The routes are keyed on the revision; new options route again.
    link_cache_revision_ = 0U;
    router_ = LinkRouter{};
    invalidate(Dirty::paint);
}

bool NodeCanvas::set_grid_spacing(double spacing) {
    if (!std::isfinite(spacing) || spacing < 1.0) return false;
    grid_spacing_ = spacing;
    invalidate(Dirty::paint);
    return true;
}

// -- caches ---------------------------------------------------------------------------------

bool NodeCanvas::fresh_caches() const noexcept {
    if (graph_ == nullptr) return false;
    const std::uint64_t revision = (*graph_).revision();
    const bool nodes = node_cache_revision_ == revision;
    const bool links = link_cache_revision_ == revision && link_cache_style_ == link_style_;
    return nodes && links;
}

void NodeCanvas::drop_caches() noexcept {
    node_cache_revision_ = 0U;
    link_cache_revision_ = 0U;
    painted_lamp_revision_ = 0U;
    node_index_.clear();
    link_index_.clear();
}

void NodeCanvas::ensure_caches() {
    if (graph_ == nullptr) return;
    const std::uint64_t revision = (*graph_).revision();
    if (node_cache_revision_ != revision) rebuild_node_cache();
    if (link_style_ == NodeLinkStyle::routed && router_.routed_revision() != revision) {
        const LinkRouteStatistics routed = router_.route(*graph_, router_options_);
        static_cast<void>(routed);
        ++statistics_.router_runs;
        link_cache_revision_ = 0U;
    }
    if (link_cache_revision_ != revision || link_cache_style_ != link_style_) rebuild_link_index();
}

void NodeCanvas::rebuild_node_cache() {
    const NodeGraph& graph = *graph_;
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const std::span<const LinkRecord> links = graph.link_slots();
    entries_.resize(std::max(nodes.size(), links.size()));
    std::array<std::size_t, port_buckets> bucket_ports{};
    std::size_t live_nodes = 0U;
    for (std::size_t slot = 0U; slot < nodes.size(); ++slot) {
        const NodeRecord& node = nodes[slot];
        entries_[slot] = SpatialEntry{node.bounds, node.live};
        if (!node.live) continue;
        ++live_nodes;
        for (const PortRecord& port : node.ports) ++bucket_ports[port_bucket(port.socket_type)];
    }
    node_index_.build(std::span<const SpatialEntry>(entries_.data(), nodes.size()), SpatialIndexOptions{});
    ++statistics_.node_index_builds;

    // The links touching each node (count, prefix, place).
    node_link_start_.assign(nodes.size() + 1U, 0U);
    std::size_t touching = 0U;
    for (const LinkRecord& link : links) {
        if (!link.live) continue;
        if (link.from.node.index < nodes.size()) ++node_link_start_[link.from.node.index + 1U];
        if (link.to.node.index < nodes.size()) ++node_link_start_[link.to.node.index + 1U];
        touching += 2U;
    }
    for (std::size_t slot = 0U; slot < nodes.size(); ++slot) node_link_start_[slot + 1U] += node_link_start_[slot];
    node_links_.resize(touching);
    query_.assign(nodes.size(), 0U);
    for (std::size_t link_slot = 0U; link_slot < links.size(); ++link_slot) {
        const LinkRecord& link = links[link_slot];
        if (!link.live) continue;
        const std::array<std::uint32_t, 2> ends{link.from.node.index, link.to.node.index};
        for (const std::uint32_t end : ends) {
            if (end >= nodes.size()) continue;
            // query_ serves as the per-node fill counter here.
            node_links_[node_link_start_[end] + query_[end]] = static_cast<std::uint32_t>(link_slot);
            ++query_[end];
        }
    }

    // The engine part keys' items, one string per slot, rewritten in place.
    node_items_.resize(nodes.size());
    for (std::size_t slot = 0U; slot < nodes.size(); ++slot) {
        std::string& item = node_items_[slot];
        item.assign("node-");
        append_decimal(item, slot);
        item.push_back('-');
        append_decimal(item, nodes[slot].generation);
    }

    // Per-frame storage: every query writes into whole-graph buffers, so a
    // frame never truncates and never allocates.
    query_.assign(std::max(nodes.size(), links.size()), 0U);
    visible_nodes_.reserve(live_nodes);
    overview_rects_.reserve(live_nodes);
    overview_selected_.reserve(live_nodes);
    for (std::size_t bucket = 0U; bucket < port_buckets; ++bucket) {
        port_centres_[bucket].reserve(bucket_ports[bucket]);
        dimmed_centres_[bucket].reserve(bucket_ports[bucket]);
    }
    candidate_flags_.assign(nodes.size(), 0U);
    refresh_selection_flags();
    node_cache_revision_ = graph.revision();
}

WorldRect NodeCanvas::link_world_bounds(std::uint32_t slot, const LinkRecord& link) const {
    const NodeGraph& graph = *graph_;
    if (link_style_ == NodeLinkStyle::routed) {
        const RoutedLink* route = router_.find(LinkId{slot, link.generation});
        if (route != nullptr) {
            const std::span<const WorldPoint> drawn = router_.drawn(*route);
            if (!drawn.empty()) {
                double left = drawn[0].x;
                double top = drawn[0].y;
                double right = drawn[0].x;
                double bottom = drawn[0].y;
                for (const WorldPoint point : drawn) {
                    left = std::min(left, point.x);
                    top = std::min(top, point.y);
                    right = std::max(right, point.x);
                    bottom = std::max(bottom, point.y);
                }
                const WorldRect bounds{left, top, right - left, bottom - top};
                return bounds;
            }
        }
    }
    const NodeRecord* from_node = graph.node(link.from.node);
    const NodeRecord* to_node = graph.node(link.to.node);
    const PortRecord* from_port = graph.port(link.from);
    const PortRecord* to_port = graph.port(link.to);
    if (from_node == nullptr || to_node == nullptr || from_port == nullptr || to_port == nullptr) {
        return WorldRect{};
    }
    const SceneCubic cubic =
        soft_link_cubic(port_world(*from_node, *from_port), edge_of(effective_side(*from_port)),
                        port_world(*to_node, *to_port), edge_of(effective_side(*to_port)), SoftLinkShape{});
    const WorldRect bounds = scene_cubic_bounds(cubic);
    return bounds;
}

void NodeCanvas::rebuild_link_index() {
    const NodeGraph& graph = *graph_;
    const std::span<const LinkRecord> links = graph.link_slots();
    entries_.resize(std::max(entries_.size(), links.size()));
    for (std::size_t slot = 0U; slot < links.size(); ++slot) {
        const LinkRecord& link = links[slot];
        bool present = link.live;
        WorldRect bounds{};
        if (present) bounds = link_world_bounds(static_cast<std::uint32_t>(slot), link);
        // A link the canvas cannot place (stale ends) is left out.
        if (!valid_world_rect(bounds)) present = false;
        entries_[slot] = SpatialEntry{bounds, present};
    }
    link_index_.build(std::span<const SpatialEntry>(entries_.data(), links.size()), SpatialIndexOptions{});
    ++statistics_.link_index_builds;
    if (query_.size() < links.size()) query_.resize(links.size());
    crossing_.resize(links.size());
    visible_links_.reserve(links.size());
    link_cache_revision_ = graph.revision();
    link_cache_style_ = link_style_;
}

void NodeCanvas::update_moved_entries() {
    // Called after a move of the selection that began with fresh caches:
    // the moved nodes and their links are updated in place, and the caches
    // stay keyed on the new revision. One refusal (the displaced list is
    // full) leaves the caches stale for a full rebuild instead.
    const NodeGraph& graph = *graph_;
    const std::span<const LinkRecord> links = graph.link_slots();
    bool kept = true;
    for (const NodeId id : selection_) {
        const NodeRecord* node = graph.node(id);
        if (node == nullptr) continue;
        if (!node_index_.move_entry(id.index, (*node).bounds)) {
            kept = false;
            break;
        }
        ++statistics_.index_moves_in_place;
        if (link_style_ != NodeLinkStyle::soft || id.index + 1U >= node_link_start_.size()) continue;
        for (std::uint32_t at = node_link_start_[id.index]; at < node_link_start_[id.index + 1U] && kept; ++at) {
            const std::uint32_t link_slot = node_links_[at];
            if (link_slot >= links.size() || !links[link_slot].live) continue;
            const WorldRect bounds = link_world_bounds(link_slot, links[link_slot]);
            if (!link_index_.move_entry(link_slot, bounds)) kept = false;
        }
        if (!kept) break;
    }
    if (!kept) {
        ++statistics_.index_moves_refused;
        return;
    }
    node_cache_revision_ = graph.revision();
    // Routed links are routed again for the new revision (ensure_caches).
    if (link_style_ == NodeLinkStyle::soft) link_cache_revision_ = graph.revision();
}

// -- model helpers --------------------------------------------------------------------------

bool NodeCanvas::can_mutate() const noexcept {
    const bool idle = gesture_ == NodeCanvasGesture::none && editor_ == nullptr && !keyboard_link_;
    const bool allowed = graph_ != nullptr && !in_callback_ && idle && !gesture_group_open_;
    return allowed;
}

NodeId NodeCanvas::id_of_slot(std::uint32_t slot) const noexcept {
    if (graph_ == nullptr) return NodeId{slot, 0U};
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    if (slot >= slots.size()) return NodeId{slot, 0U};
    const NodeId id{slot, slots[slot].generation};
    return id;
}

bool NodeCanvas::link_accepted(PortRef from, PortRef to) const {
    if (graph_ == nullptr) return false;
    const PortRecord* source = (*graph_).port(from);
    const PortRecord* target = (*graph_).port(to);
    if (source == nullptr || target == nullptr) return false;
    if ((*source).direction != PortDirection::output || (*target).direction != PortDirection::input) return false;
    NodeLinkCheck check{};
    check.from = from;
    check.to = to;
    check.from_type = (*source).socket_type;
    check.to_type = (*target).socket_type;
    check.accepted = check.from_type == check.to_type;
    if (callbacks_.check_link) {
        const CallbackScope scope(*this);
        callbacks_.check_link(check);
    }
    return check.accepted;
}

void NodeCanvas::refresh_selection_flags() {
    if (graph_ == nullptr) {
        selection_.clear();
        selected_flags_.clear();
        selected_link_.reset();
        return;
    }
    const NodeGraph& graph = *graph_;
    selected_flags_.assign(graph.node_slots().size(), 0U);
    // Drop stale identities (an undo can end a selected node's record) and
    // repeats.
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < selection_.size(); ++index) {
        const NodeId id = selection_[index];
        if (graph.node(id) == nullptr || selected_flags_[id.index] != 0U) continue;
        selected_flags_[id.index] = 1U;
        selection_[kept] = id;
        ++kept;
    }
    selection_.resize(kept);
    if (selected_link_.has_value() && graph.link(*selected_link_) == nullptr) selected_link_.reset();
    if (focused_node_.has_value() && graph.node(*focused_node_) == nullptr) {
        focused_node_.reset();
        focused_port_.reset();
    }
    if (focused_node_.has_value() && focused_port_.has_value()) {
        const NodeRecord* node = graph.node(*focused_node_);
        if (node == nullptr || (*focused_port_).value >= (*node).ports.size()) focused_port_.reset();
    }
    if (hot_node_.has_value() && *hot_node_ >= selected_flags_.size()) hot_node_.reset();
}

bool NodeCanvas::selected(NodeId node) const noexcept {
    if (node.index >= selected_flags_.size() || selected_flags_[node.index] == 0U) return false;
    const NodeId current = id_of_slot(node.index);
    return current == node;
}

void NodeCanvas::emit_selection() {
    invalidate(Dirty::paint | Dirty::semantics);
    const NodeCanvasSelection change{selection_.size(), selected_link_};
    selection_changed_.emit(change);
}

void NodeCanvas::after_edit(NodeGraphEditKind kind) {
    refresh_selection_flags();
    invalidate(Dirty::paint | Dirty::semantics);
    if (graph_ == nullptr) return;
    const NodeGraphEdited edited{kind, (*graph_).revision()};
    graph_edited_.emit(edited);
}

void NodeCanvas::set_announcement(std::string_view text) {
    if (announcement_ == text) return;
    announcement_.assign(text);
    invalidate(Dirty::semantics);
}

void NodeCanvas::node_states_into(std::uint32_t slot, AnatomyStateSet& states) const {
    if (slot < selected_flags_.size() && selected_flags_[slot] != 0U) states.insert(AnatomyState::selected);
    if (hot_node_.has_value() && *hot_node_ == slot) states.insert(AnatomyState::hot);
    const bool current = focused_node_.has_value() && (*focused_node_).index == slot && focused_;
    if (current) states.insert(AnatomyState::current);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (current && host != nullptr && (*host).focus_cue_visible()) states.insert(AnatomyState::focus_visible);
}

// -- selection ------------------------------------------------------------------------------

void NodeCanvas::select_only(NodeId node) {
    if (graph_ == nullptr || in_callback_) return;
    selection_.clear();
    selected_link_.reset();
    if ((*graph_).node(node) != nullptr) {
        selection_.push_back(node);
        focused_node_ = node;
        focused_port_.reset();
    }
    refresh_selection_flags();
    emit_selection();
}

void NodeCanvas::add_to_selection(NodeId node) {
    if (graph_ == nullptr || in_callback_ || (*graph_).node(node) == nullptr || selected(node)) return;
    selection_.push_back(node);
    selected_link_.reset();
    focused_node_ = node;
    refresh_selection_flags();
    emit_selection();
}

void NodeCanvas::toggle_selection(NodeId node) {
    if (graph_ == nullptr || in_callback_) return;
    if (!selected(node)) {
        add_to_selection(node);
        return;
    }
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < selection_.size(); ++index) {
        if (selection_[index] == node) continue;
        selection_[kept] = selection_[index];
        ++kept;
    }
    selection_.resize(kept);
    refresh_selection_flags();
    emit_selection();
}

void NodeCanvas::select_link(LinkId link) {
    if (graph_ == nullptr || in_callback_ || (*graph_).link(link) == nullptr) return;
    selection_.clear();
    selected_link_ = link;
    refresh_selection_flags();
    emit_selection();
}

void NodeCanvas::clear_selection() {
    if (in_callback_ || (selection_.empty() && !selected_link_.has_value())) return;
    selection_.clear();
    selected_link_.reset();
    refresh_selection_flags();
    emit_selection();
}

void NodeCanvas::select_all() {
    if (graph_ == nullptr || in_callback_) return;
    selection_.clear();
    selected_link_.reset();
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
        if (slots[slot].live) selection_.push_back(NodeId{static_cast<std::uint32_t>(slot), slots[slot].generation});
    }
    refresh_selection_flags();
    emit_selection();
}

std::size_t NodeCanvas::select_in_rect(const WorldRect& area, SpatialQueryMode mode, bool add) {
    if (graph_ == nullptr || in_callback_) return 0U;
    ensure_caches();
    const SpatialQueryResult found = node_index_.query(area, mode, std::span<std::uint32_t>(query_.data(), query_.size()));
    if (!add) selection_.clear();
    selected_link_.reset();
    refresh_selection_flags();
    std::size_t added = 0U;
    for (std::size_t index = 0U; index < found.written; ++index) {
        const std::uint32_t slot = query_[index];
        if (slot >= selected_flags_.size() || selected_flags_[slot] != 0U) continue;
        selection_.push_back(id_of_slot(slot));
        selected_flags_[slot] = 1U;
        ++added;
    }
    emit_selection();
    return added;
}

bool NodeCanvas::set_focused_node(NodeId node) {
    if (graph_ == nullptr || (*graph_).node(node) == nullptr) return false;
    focused_node_ = node;
    focused_port_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

// -- operations -----------------------------------------------------------------------------

bool NodeCanvas::delete_selection() {
    if (!can_mutate()) return false;
    NodeGraph& graph = *graph_;
    if (selection_.empty() && !selected_link_.has_value()) return false;
    graph.begin_undo_group();
    for (const NodeId id : selection_) static_cast<void>(graph.remove_node(id));
    if (selected_link_.has_value()) static_cast<void>(graph.disconnect(*selected_link_));
    static_cast<void>(graph.end_undo_group());
    selection_.clear();
    selected_link_.reset();
    after_edit(NodeGraphEditKind::removed);
    emit_selection();
    return true;
}

bool NodeCanvas::undo() {
    if (!can_mutate()) return false;
    const GraphResult result = (*graph_).undo();
    if (result.status != GraphStatus::ok) return false;
    after_edit(NodeGraphEditKind::undone);
    return true;
}

bool NodeCanvas::redo() {
    if (!can_mutate()) return false;
    const GraphResult result = (*graph_).redo();
    if (result.status != GraphStatus::ok) return false;
    after_edit(NodeGraphEditKind::redone);
    return true;
}

bool NodeCanvas::move_selection(WorldPoint delta) {
    if (!can_mutate() || selection_.empty()) return false;
    const bool fresh = fresh_caches();
    const GraphResult result =
        (*graph_).move_nodes(std::span<const NodeId>(selection_.data(), selection_.size()), delta);
    if (result.status != GraphStatus::ok) return false;
    if (fresh) update_moved_entries();
    after_edit(NodeGraphEditKind::moved);
    return true;
}

std::size_t NodeCanvas::cut_links(Point from, Point to) {
    if (!can_mutate()) return 0U;
    ensure_caches();
    const std::size_t crossing = links_crossing(from, to, std::span<LinkId>(crossing_.data(), crossing_.size()));
    const std::size_t listed = std::min(crossing, crossing_.size());
    if (listed == 0U) return 0U;
    NodeGraph& graph = *graph_;
    graph.begin_undo_group();
    std::size_t cut = 0U;
    for (std::size_t index = 0U; index < listed; ++index) {
        const GraphResult result = graph.disconnect(crossing_[index]);
        if (result.status == GraphStatus::ok) ++cut;
    }
    static_cast<void>(graph.end_undo_group());
    statistics_.knife_cuts += cut;
    after_edit(NodeGraphEditKind::disconnected);
    return cut;
}

GraphResult NodeCanvas::connect_ports(PortRef from, PortRef to) {
    GraphResult refused{};
    refused.status = GraphStatus::invalid_argument;
    if (!can_mutate() || !link_accepted(from, to)) {
        ++statistics_.connections_refused;
        return refused;
    }
    const GraphResult result = (*graph_).connect(from, to);
    if (result.status != GraphStatus::ok) {
        ++statistics_.connections_refused;
        return result;
    }
    ++statistics_.connections_made;
    after_edit(NodeGraphEditKind::connected);
    const NodeLinkConnected connected{result.link, from, to};
    link_connected_.emit(connected);
    return result;
}

void NodeCanvas::collect_link_candidates(Point around, bool within_margin) {
    for (const LinkCandidate& candidate : candidates_) {
        if (candidate.port.node.index < candidate_flags_.size()) candidate_flags_[candidate.port.node.index] = 0U;
    }
    candidates_.clear();
    targets_.clear();
    if (graph_ == nullptr) return;
    const NodeGraph& graph = *graph_;
    const PortRecord* source = graph.port(link_from_);
    if (source == nullptr) return;
    ensure_caches();
    const std::span<const NodeRecord> slots = graph.node_slots();
    std::size_t listed = 0U;
    if (within_margin) {
        const Rect area = inflated(client_rectangle(), candidate_margin);
        const SpatialQueryResult found = node_index_.query(view_.rect_to_world(area), SpatialQueryMode::intersecting,
                                                           std::span<std::uint32_t>(query_.data(), query_.size()));
        listed = found.written;
    } else {
        for (std::size_t slot = 0U; slot < slots.size() && listed < query_.size(); ++slot) {
            if (!slots[slot].live) continue;
            query_[listed] = static_cast<std::uint32_t>(slot);
            ++listed;
        }
    }
    for (std::size_t index = 0U; index < listed; ++index) {
        const std::uint32_t slot = query_[index];
        if (slot == link_from_.node.index || slot >= slots.size()) continue;
        const NodeRecord& node = slots[slot];
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const PortRecord& record = node.ports[port];
            if (record.direction != PortDirection::input) continue;
            const PortRef ref = port_ref(slot, node, port);
            if (graph.link(graph.link_into(ref)) != nullptr) continue;
            if (!link_accepted(link_from_, ref)) continue;
            const Point anchor = view_.to_view(port_world(node, record));
            candidates_.push_back(LinkCandidate{ref, anchor, distance_between(anchor, around)});
        }
    }
    // Nearest first, capped at the tracker's target limit (the tracker
    // refuses more rather than truncating; the provider caps).
    std::sort(candidates_.begin(), candidates_.end(), CandidateNearer{});
    if (candidates_.size() > maximum_drag_targets) candidates_.resize(maximum_drag_targets);
    for (const LinkCandidate& candidate : candidates_) {
        if (candidate.port.node.index < candidate_flags_.size()) candidate_flags_[candidate.port.node.index] = 1U;
    }
    statistics_.magnet_candidates += candidates_.size();
}

bool NodeCanvas::link_to_nearest(PortRef from) {
    if (!can_mutate()) return false;
    const PortRecord* source = (*graph_).port(from);
    if (source == nullptr || (*source).direction != PortDirection::output) return false;
    const std::optional<Point> anchor = port_view_anchor(from);
    if (!anchor.has_value()) return false;
    link_from_ = from;
    collect_link_candidates(*anchor, false);
    const std::size_t tries = std::min(candidates_.size(), keyboard_link_tries);
    for (std::size_t index = 0U; index < tries; ++index) {
        const GraphResult result = connect_ports(from, candidates_[index].port);
        if (result.status == GraphStatus::ok) return true;
    }
    return false;
}

std::optional<std::uint32_t> NodeCanvas::nearest_node_in_direction(double dx, double dy) const {
    if (graph_ == nullptr) return std::nullopt;
    const std::span<const NodeRecord> slots = (*graph_).node_slots();
    WorldPoint origin = view_.to_world(Point{client_rectangle().width / 2.0, client_rectangle().height / 2.0});
    std::optional<std::uint32_t> from{};
    if (focused_node_.has_value()) {
        const NodeRecord* node = (*graph_).node(*focused_node_);
        if (node != nullptr) {
            origin = WorldPoint{(*node).bounds.x + (*node).bounds.width / 2.0,
                                (*node).bounds.y + (*node).bounds.height / 2.0};
            from = (*focused_node_).index;
        }
    }
    std::optional<std::uint32_t> best{};
    double best_score = std::numeric_limits<double>::infinity();
    for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
        const NodeRecord& node = slots[slot];
        if (!node.live || (from.has_value() && *from == slot)) continue;
        const double cx = node.bounds.x + node.bounds.width / 2.0 - origin.x;
        const double cy = node.bounds.y + node.bounds.height / 2.0 - origin.y;
        const double along = cx * dx + cy * dy;
        const double across = std::fabs(cx * dy - cy * dx);
        // Within 45 degrees either side of the direction (a focused node's
        // own centre is never a candidate; without one, any node ahead).
        if (along <= 0.0 || across > along) continue;
        const double score = along + 2.0 * across;
        if (score < best_score) {
            best_score = score;
            best = static_cast<std::uint32_t>(slot);
        }
    }
    return best;
}

// -- geometry -------------------------------------------------------------------------------

std::optional<Point> NodeCanvas::port_view_anchor(PortRef port) const {
    if (graph_ == nullptr) return std::nullopt;
    const NodeRecord* node = (*graph_).node(port.node);
    const PortRecord* record = (*graph_).port(port);
    if (node == nullptr || record == nullptr) return std::nullopt;
    const Point view = view_.to_view(port_world(*node, *record));
    return view;
}

std::size_t NodeCanvas::curve_segments() const noexcept {
    switch (level_) {
    case SceneDetailLevel::overview: return 4U;
    case SceneDetailLevel::silhouette: return 8U;
    case SceneDetailLevel::compact: return 16U;
    case SceneDetailLevel::full: return 24U;
    default: break;
    }
    return 24U;
}

std::size_t NodeCanvas::link_path(std::uint32_t slot, const LinkRecord& link, std::size_t segments,
                                  std::span<Point> out) const {
    if (graph_ == nullptr) return 0U;
    if (link_style_ == NodeLinkStyle::routed) {
        const RoutedLink* route = router_.find(LinkId{slot, link.generation});
        if (route != nullptr) {
            const std::span<const WorldPoint> drawn = router_.drawn(*route);
            const std::size_t count = std::min(drawn.size(), out.size());
            for (std::size_t index = 0U; index < count; ++index) out[index] = view_.to_view(drawn[index]);
            return count;
        }
    }
    const NodeGraph& graph = *graph_;
    const NodeRecord* from_node = graph.node(link.from.node);
    const NodeRecord* to_node = graph.node(link.to.node);
    const PortRecord* from_port = graph.port(link.from);
    const PortRecord* to_port = graph.port(link.to);
    if (from_node == nullptr || to_node == nullptr || from_port == nullptr || to_port == nullptr) return 0U;
    const SceneCubic cubic =
        soft_link_cubic(port_world(*from_node, *from_port), edge_of(effective_side(*from_port)),
                        port_world(*to_node, *to_port), edge_of(effective_side(*to_port)), SoftLinkShape{});
    const std::size_t count = flatten_scene_cubic(cubic, segments, view_, out);
    return count;
}

// -- hit testing ----------------------------------------------------------------------------

NodeCanvasHit NodeCanvas::hit_test(Point local) {
    ++statistics_.hit_tests;
    NodeCanvasHit hit{};
    if (graph_ == nullptr) return hit;
    ensure_caches();
    const NodeGraph& graph = *graph_;
    const std::span<const NodeRecord> slots = graph.node_slots();
    const WorldPoint world = view_.to_world(local);
    const double reach = port_hit_radius / view_.zoom;
    const WorldRect around{world.x - reach, world.y - reach, 2.0 * reach, 2.0 * reach};
    const std::span<std::uint32_t> storage(query_.data(), query_.size());
    const SpatialQueryResult near_nodes = node_index_.query(around, SpatialQueryMode::intersecting, storage);
    // Ports first, where they are drawn: the nearest within the radius.
    const bool ports_drawn = level_ == SceneDetailLevel::compact || level_ == SceneDetailLevel::full;
    double nearest = port_hit_radius;
    for (std::size_t index = 0U; index < near_nodes.written && ports_drawn; ++index) {
        const std::uint32_t slot = query_[index];
        const NodeRecord& node = slots[slot];
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const Point anchor = view_.to_view(port_world(node, node.ports[port]));
            const double distance = distance_between(anchor, local);
            if (distance > nearest) continue;
            nearest = distance;
            hit.kind = NodeCanvasHitKind::port;
            hit.node = NodeId{slot, node.generation};
            hit.port = PortIndex{static_cast<std::uint16_t>(port)};
        }
    }
    if (hit.kind == NodeCanvasHitKind::port) return hit;
    // Then bodies: the topmost (the latest slot, painted last).
    std::optional<std::uint32_t> top{};
    for (std::size_t index = 0U; index < near_nodes.written; ++index) {
        const std::uint32_t slot = query_[index];
        if (!slots[slot].bounds.contains(world)) continue;
        if (!top.has_value() || slot > *top) top = slot;
    }
    if (top.has_value()) {
        const NodeRecord& node = slots[*top];
        hit.node = NodeId{*top, node.generation};
        const bool caption = world.y <= node.bounds.y + std::min(caption_height, node.bounds.height);
        hit.kind = caption ? NodeCanvasHitKind::node_caption : NodeCanvasHitKind::node_body;
        return hit;
    }
    // Then links within link_hit_distance, from the link index.
    const double link_reach = link_hit_distance / view_.zoom;
    const WorldRect link_area{world.x - link_reach, world.y - link_reach, 2.0 * link_reach, 2.0 * link_reach};
    const SpatialQueryResult near_links = link_index_.query(link_area, SpatialQueryMode::intersecting, storage);
    const std::span<const LinkRecord> links = graph.link_slots();
    const std::span<Point> points(link_points_.data(), link_points_.size());
    double closest = link_hit_distance;
    for (std::size_t index = 0U; index < near_links.written; ++index) {
        const std::uint32_t slot = query_[index];
        if (slot >= links.size() || !links[slot].live) continue;
        const std::size_t count = link_path(slot, links[slot], 24U, points);
        const double distance =
            scene_distance_to_polyline(std::span<const Point>(link_points_.data(), count), local);
        if (distance > closest) continue;
        closest = distance;
        hit.kind = NodeCanvasHitKind::link;
        hit.link = LinkId{slot, links[slot].generation};
    }
    return hit;
}

std::size_t NodeCanvas::links_crossing(Point from, Point to, std::span<LinkId> out) {
    if (graph_ == nullptr) return 0U;
    ensure_caches();
    const NodeGraph& graph = *graph_;
    const std::span<const LinkRecord> links = graph.link_slots();
    const Rect segment_box{std::min(from.x, to.x), std::min(from.y, to.y), std::fabs(to.x - from.x),
                           std::fabs(to.y - from.y)};
    const SpatialQueryResult found =
        link_index_.query(view_.rect_to_world(segment_box), SpatialQueryMode::intersecting,
                          std::span<std::uint32_t>(query_.data(), query_.size()));
    const std::span<Point> points(link_points_.data(), link_points_.size());
    std::size_t crossing = 0U;
    for (std::size_t index = 0U; index < found.written; ++index) {
        const std::uint32_t slot = query_[index];
        if (slot >= links.size() || !links[slot].live) continue;
        const std::size_t count = link_path(slot, links[slot], 24U, points);
        if (!scene_polyline_crosses_segment(std::span<const Point>(link_points_.data(), count), from, to)) continue;
        if (crossing < out.size()) out[crossing] = LinkId{slot, links[slot].generation};
        ++crossing;
    }
    return crossing;
}

// -- editing --------------------------------------------------------------------------------

std::optional<NodeId> NodeCanvas::edited_node() const noexcept {
    if (editor_ == nullptr) return std::nullopt;
    return edit_node_;
}

Rect NodeCanvas::edit_bounds() const {
    if (graph_ == nullptr) return Rect{};
    const NodeRecord* node = (*graph_).node(edit_node_);
    if (node == nullptr) return Rect{};
    const Rect whole = view_.rect_to_view((*node).bounds);
    const double caption = std::min(caption_height * view_.zoom, whole.height);
    if (edit_part_ == NodeEditPart::caption) {
        const Rect band{whole.x, whole.y, whole.width, caption};
        return band;
    }
    const Rect body{whole.x, whole.y + caption, whole.width, whole.height - caption};
    return body;
}

bool NodeCanvas::begin_edit(NodeId node, NodeEditPart part) {
    if (!can_mutate() || (*graph_).node(node) == nullptr) return false;
    ++statistics_.edit_requests;
    if (!callbacks_.edit) return false;
    edit_node_ = node;
    edit_part_ = part;
    NodeEditRequest request{};
    request.node = node;
    request.part = part;
    request.bounds = edit_bounds();
    {
        const CallbackScope scope(*this);
        callbacks_.edit(request);
    }
    if (request.editor == nullptr || !is_alive()) return false;
    NodeGraph& graph = *graph_;
    graph.begin_undo_group();
    try {
        add_child(request.editor);
    } catch (...) {
        static_cast<void>(graph.rollback_undo_group());
        throw;
    }
    edit_group_open_ = true;
    editor_ = std::move(request.editor);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(editor_));
    return true;
}

void NodeCanvas::end_edit(NodeEditEnd end) {
    if (editor_ == nullptr) return;
    const Control::Ptr editor = std::move(editor_);
    editor_.reset();
    const Control::Ptr removed = remove_child((*editor).runtime_id());
    static_cast<void>(removed);
    if (graph_ != nullptr && edit_group_open_) {
        if (end == NodeEditEnd::commit) {
            static_cast<void>((*graph_).end_undo_group());
        } else {
            static_cast<void>((*graph_).rollback_undo_group());
        }
    }
    edit_group_open_ = false;
    if (end == NodeEditEnd::commit) {
        ++statistics_.edits_committed;
    } else {
        ++statistics_.edits_cancelled;
    }
    after_edit(NodeGraphEditKind::edited);
    const NodeEditEnded ended{edit_node_, edit_part_, end};
    edit_ended_.emit(ended);
    if (!is_alive()) return;
    SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
}

// -- focus ----------------------------------------------------------------------------------

void NodeCanvas::on_focus_changed(bool focused) {
    focused_ = focused;
    // Tab into the canvas: the first visible node (or the first live one)
    // becomes current, so arrows have somewhere to start.
    if (focused && !focused_node_.has_value() && graph_ != nullptr) {
        std::optional<std::uint32_t> first{};
        if (!visible_nodes_.empty()) first = visible_nodes_.front();
        const std::span<const NodeRecord> slots = (*graph_).node_slots();
        for (std::size_t slot = 0U; slot < slots.size() && !first.has_value(); ++slot) {
            if (slots[slot].live) first = static_cast<std::uint32_t>(slot);
        }
        if (first.has_value() && *first < slots.size() && slots[*first].live) focused_node_ = id_of_slot(*first);
    }
    invalidate(Dirty::paint | Dirty::semantics);
    Control::on_focus_changed(focused);
}

// -- semantics ------------------------------------------------------------------------------

void NodeCanvas::node_identity(std::uint32_t slot, std::string& identity) const {
    identity.assign(stable_id().value());
    identity.append(node_segment);
    append_decimal(identity, slot);
    identity.push_back('-');
    append_decimal(identity, id_of_slot(slot).generation);
}

std::optional<NodeId> NodeCanvas::node_of_child_id(std::string_view stable_id_text) const {
    const std::string_view own = stable_id().value();
    if (stable_id_text.size() <= own.size() + node_segment.size()) return std::nullopt;
    if (stable_id_text.substr(0U, own.size()) != own) return std::nullopt;
    std::string_view rest = stable_id_text.substr(own.size());
    if (rest.substr(0U, node_segment.size()) != node_segment) return std::nullopt;
    rest = rest.substr(node_segment.size());
    std::uint32_t slot = 0U;
    const std::size_t slot_digits = read_decimal(rest, slot);
    if (slot_digits == 0U || slot_digits >= rest.size() || rest[slot_digits] != '-') return std::nullopt;
    rest = rest.substr(slot_digits + 1U);
    std::uint32_t generation = 0U;
    const std::size_t generation_digits = read_decimal(rest, generation);
    if (generation_digits == 0U) return std::nullopt;
    rest = rest.substr(generation_digits);
    // A port child names its node too: "<node>.port-<n>".
    if (!rest.empty()) {
        if (rest.substr(0U, port_segment.size()) != port_segment) return std::nullopt;
        std::uint32_t port = 0U;
        const std::string_view digits = rest.substr(port_segment.size());
        if (read_decimal(digits, port) != digits.size() || digits.empty()) return std::nullopt;
    }
    const NodeId node{slot, generation};
    if (graph_ == nullptr || (*graph_).node(node) == nullptr) return std::nullopt;
    return node;
}

SemanticDescriptor NodeCanvas::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    if (graph_ != nullptr) {
        std::string value{};
        append_decimal(value, (*graph_).node_count());
        value.append(" nodes, ");
        append_decimal(value, (*graph_).link_count());
        value.append(" links");
        descriptor.value = std::move(value);
    }
    descriptor.states |= SemanticState::multiselectable;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> NodeCanvas::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    const Rect absolute = absolute_bounds();
    const bool enabled = effectively_enabled();
    if (graph_ != nullptr) {
        const NodeGraph& graph = *graph_;
        const std::span<const NodeRecord> slots = graph.node_slots();
        // The visible set, then the focused node wherever it is (the focused
        // path is always realized, spec 6).
        std::size_t count = visible_nodes_.size();
        bool focused_listed = !focused_node_.has_value();
        for (const std::uint32_t slot : visible_nodes_) {
            if (focused_node_.has_value() && (*focused_node_).index == slot) focused_listed = true;
        }
        if (!focused_listed) ++count;
        children.reserve(count + 1U);
        for (std::size_t index = 0U; index < count; ++index) {
            const std::uint32_t slot = index < visible_nodes_.size() ? visible_nodes_[index] : (*focused_node_).index;
            if (slot >= slots.size() || !slots[slot].live) continue;
            const NodeRecord& node = slots[slot];
            SemanticNode item{};
            node_identity(slot, item.stable_id);
            item.runtime_id = virtual_semantic_runtime_id(item.stable_id);
            item.role = SemanticRole::list_item;
            item.name = node.title;
            item.name.append(", ");
            item.name.append(node.kind);
            const Rect shown = view_.rect_to_view(node.bounds);
            item.bounds = Rect{absolute.x + shown.x, absolute.y + shown.y, shown.width, shown.height};
            item.states = SemanticState::visible | SemanticState::focusable;
            if (enabled) item.states |= SemanticState::enabled;
            if (slot < selected_flags_.size() && selected_flags_[slot] != 0U) item.states |= SemanticState::selected;
            const bool current = focused_node_.has_value() && (*focused_node_).index == slot;
            if (current && focused_ && !focused_port_.has_value()) item.states |= SemanticState::focused;
            item.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
            item.children.reserve(node.ports.size());
            for (std::size_t port = 0U; port < node.ports.size(); ++port) {
                const PortRecord& record = node.ports[port];
                SemanticNode child{};
                child.stable_id = item.stable_id;
                child.stable_id.append(port_segment);
                append_decimal(child.stable_id, port);
                child.runtime_id = virtual_semantic_runtime_id(child.stable_id);
                child.role = SemanticRole::static_text;
                child.name = record.direction == PortDirection::input ? "input " : "output ";
                child.name.append(record.name);
                // No relations yet (gap 13): an input names its source.
                if (record.direction == PortDirection::input) {
                    const LinkRecord* link = graph.link(graph.link_into(port_ref(slot, node, port)));
                    const NodeRecord* source = link != nullptr ? graph.node((*link).from.node) : nullptr;
                    if (source != nullptr) {
                        child.name.append(", connected to ");
                        child.name.append((*source).title);
                    }
                }
                if (record.lamp == PortLamp::on) child.value = "on";
                if (record.lamp == PortLamp::error) child.value = "error";
                const Point anchor = view_.to_view(port_world(node, record));
                child.bounds = Rect{absolute.x + anchor.x - port_hit_radius, absolute.y + anchor.y - port_hit_radius,
                                    2.0 * port_hit_radius, 2.0 * port_hit_radius};
                child.position_in_set = port + 1U;
                child.set_size = node.ports.size();
                child.states = SemanticState::visible;
                if (enabled) child.states |= SemanticState::enabled;
                if (current && focused_ && focused_port_.has_value() && (*focused_port_).value == port) {
                    child.states |= SemanticState::focused;
                }
                item.children.push_back(std::move(child));
            }
            children.push_back(std::move(item));
        }
    }
    // Always present, so a host sees the name change rather than a new node.
    SemanticNode announcements{};
    announcements.stable_id.assign(stable_id().value());
    announcements.stable_id.append(announcements_segment);
    announcements.runtime_id = virtual_semantic_runtime_id(announcements.stable_id);
    announcements.role = SemanticRole::live_region;
    announcements.name = announcement_;
    const Rect client = client_rectangle();
    announcements.bounds = Rect{absolute.x + client.x, absolute.y + client.y, client.width, client.height};
    announcements.states = SemanticState::visible;
    if (enabled) announcements.states |= SemanticState::enabled;
    children.push_back(std::move(announcements));
    return children;
}

bool NodeCanvas::on_semantic_child_action(std::string_view stable_id_text, SemanticAction action, std::string_view) {
    if (!effectively_enabled() || in_callback_) return false;
    if (action != SemanticAction::focus && action != SemanticAction::select && action != SemanticAction::press) {
        return false;
    }
    const std::optional<NodeId> node = node_of_child_id(stable_id_text);
    if (!node.has_value()) return false;
    const Control::Ptr lifetime = shared_from_this();
    SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(lifetime));
    if (!is_alive()) return true;
    select_only(*node);
    reveal_node(*node);
    if (action == SemanticAction::press && is_alive()) static_cast<void>(begin_edit(*node, NodeEditPart::body));
    return true;
}

// -- anatomy --------------------------------------------------------------------------------

const ControlAnatomy& NodeCanvas::anatomy() const noexcept {
    return node_canvas_anatomy;
}

void NodeCanvas::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), node_canvas_interactions.begin(), node_canvas_interactions.end());
}

void NodeCanvas::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.well = PartGeometry::from_bounds(client);
    record.well.authored_background = back_color.read();
    record.grid_minor = PartGeometry::from_bounds(client);
    record.grid_major = PartGeometry::from_bounds(client);
    if (graph_ != nullptr && (*graph_).link_count() > 0U) record.connector = PartGeometry::from_bounds(client);
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::marquee) {
        const Rect marquee{std::min(press_view_.x, current_view_.x), std::min(press_view_.y, current_view_.y),
                           std::fabs(current_view_.x - press_view_.x), std::fabs(current_view_.y - press_view_.y)};
        record.marquee = PartGeometry::from_bounds(marquee);
    }
    if (gesture_planned_ && gesture_ == NodeCanvasGesture::knife) {
        const Rect knife{std::min(press_view_.x, current_view_.x), std::min(press_view_.y, current_view_.y),
                         std::fabs(current_view_.x - press_view_.x), std::fabs(current_view_.y - press_view_.y)};
        record.knife = PartGeometry::from_bounds(knife);
    }
    if (gesture_ == NodeCanvasGesture::link && link_hit_.has_value() && *link_hit_ < candidates_.size()) {
        const Point at = candidates_[*link_hit_].view_anchor;
        record.drop = PartGeometry::from_bounds(Rect{at.x - drop_ring / 2.0, at.y - drop_ring / 2.0, drop_ring, drop_ring});
    }
    if (focused_) record.focus = PartGeometry::from_bounds(client);
    write_parts(anatomy(), node_canvas_part_fields, record, geometry);
}

NodeCanvas::Parts NodeCanvas::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), node_canvas_part_fields, published.parts(), record);
    return record;
}

NodeCanvas::States NodeCanvas::states() const {
    States record;
    read_states(node_canvas_state_fields, anatomy_states(), record);
    return record;
}

std::size_t NodeCanvas::realized_item_count() const {
    return visible_nodes_.size();
}

std::string NodeCanvas::realized_item_stable_id(std::size_t item) const {
    if (item >= visible_nodes_.size()) throw std::out_of_range("NodeCanvas realized item past the end");
    std::string identity;
    node_identity(visible_nodes_[item], identity);
    return identity;
}

AnatomyStateSet NodeCanvas::realized_item_anatomy_states(std::size_t item) const {
    if (item >= visible_nodes_.size()) throw std::out_of_range("NodeCanvas realized item past the end");
    AnatomyStateSet states;
    node_states_into(visible_nodes_[item], states);
    return states;
}

void NodeCanvas::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    NodeParts record;
    if (item < visible_nodes_.size() && graph_ != nullptr) {
        const std::uint32_t slot = visible_nodes_[item];
        const std::span<const NodeRecord> slots = (*graph_).node_slots();
        if (slot < slots.size() && slots[slot].live) {
            const NodeRecord& node = slots[slot];
            const Rect whole = view_.rect_to_view(node.bounds);
            const double caption = std::min(caption_height * view_.zoom, whole.height);
            AnatomyStateSet states;
            node_states_into(slot, states);
            record.node = PartGeometry::from_bounds(whole);
            record.node_face = PartGeometry::from_bounds(whole);
            if (states.contains(AnatomyState::selected)) {
                record.node_selection = PartGeometry::from_bounds(inflated(whole, selection_outset * view_.zoom));
            }
            record.node_caption = PartGeometry::from_bounds(Rect{whole.x, whole.y, whole.width, caption});
            if (!node.ports.empty()) {
                const Point anchor = view_.to_view(port_world(node, node.ports.front()));
                const double size = std::max(smallest_port_marker, port_marker_size * view_.zoom);
                record.port = PartGeometry::from_bounds(Rect{anchor.x - size / 2.0, anchor.y - size / 2.0, size, size});
                const double label_height = port_label_size * view_.zoom * 1.4;
                record.port_label = PartGeometry::from_bounds(
                    Rect{anchor.x + port_label_gap * view_.zoom, anchor.y - label_height / 2.0,
                         port_label_width * view_.zoom, label_height});
            }
            for (const PortRecord& port : node.ports) {
                if (port.direction != PortDirection::output) continue;
                const Point anchor = view_.to_view(port_world(node, port));
                const double size = lamp_size * view_.zoom;
                record.lamp = PartGeometry::from_bounds(
                    Rect{anchor.x - lamp_inset * view_.zoom - size / 2.0, anchor.y - size / 2.0, size, size});
                break;
            }
            if (states.contains(AnatomyState::focus_visible)) record.node_focus = PartGeometry::from_bounds(whole);
        }
    }
    const ItemSystemDeclaration& system = anatomy().item_systems[node_system.value];
    write_item_parts(system, node_canvas_node_part_fields, record, geometry);
}

} // namespace gui_forms
