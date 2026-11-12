// NodeCanvas's input: the per-event raisers in the window's input order
// (on_mouse_up before on_click, suppress_click after a planned drag), the
// DragTracker targets of the five gestures, and the keyboard graph of the
// node graph spec, section 6.
//
// Every gesture is one DragTracker session. Gestures that change the graph
// (move, a link detached from an input) open one undo group, closed when
// the session ends: committed as one undo step, or rolled back when the
// session is cancelled (Escape, which the window routes to the drag engine
// first, a detach, or the graph changing under it). A session that ends
// without a plan (a click, or Escape while armed) calls no finished target;
// settle_unplanned_gesture closes its group instead.

#include "gui_forms/controls/node_canvas.hpp"

#include "node_canvas_detail.hpp"

#include "../panel/input_control_utilities.hpp"

#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace gui_forms {

using namespace node_canvas_detail;
using input_control_detail::command_modifier;
using input_control_detail::includes;

namespace {

[[nodiscard]] bool arrow_key(std::uint32_t key) noexcept {
    const bool arrow = key == PhysicalKey::left || key == PhysicalKey::right || key == PhysicalKey::up ||
                       key == PhysicalKey::down;
    return arrow;
}

// The unit direction of an arrow key (y down).
[[nodiscard]] Point arrow_direction(std::uint32_t key) noexcept {
    if (key == PhysicalKey::left) return Point{-1.0, 0.0};
    if (key == PhysicalKey::right) return Point{1.0, 0.0};
    if (key == PhysicalKey::up) return Point{0.0, -1.0};
    if (key == PhysicalKey::down) return Point{0.0, 1.0};
    return Point{};
}

[[nodiscard]] bool letter_key(std::uint32_t key) noexcept {
    const bool letter = key >= PhysicalKey::a && key <= PhysicalKey::z;
    return letter;
}

// The lower-case ASCII letter of a letter key (the physical keys a to z
// are contiguous in the usage table).
[[nodiscard]] char letter_of(std::uint32_t key) noexcept {
    const char letter = static_cast<char>('a' + static_cast<int>(key - PhysicalKey::a));
    return letter;
}

[[nodiscard]] char folded(char character) noexcept {
    if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
    return character;
}

void append_count(std::string& text, std::size_t value) {
    const std::string digits = std::to_string(value);
    text.append(digits);
}

} // namespace

// -- gestures -------------------------------------------------------------------------------

void NodeCanvas::close_gesture_group(bool commit) {
    if (!gesture_group_open_) return;
    gesture_group_open_ = false;
    if (graph_ == nullptr) return;
    if (commit) {
        static_cast<void>((*graph_).end_undo_group());
    } else {
        static_cast<void>((*graph_).rollback_undo_group());
    }
}

void NodeCanvas::settle_unplanned_gesture() {
    if (gesture_ == NodeCanvasGesture::none || drag_ == nullptr || (*drag_).running()) return;
    if (gesture_group_open_) {
        // A detach whose drag never started: the link comes back.
        close_gesture_group(false);
        ++statistics_.gestures_cancelled;
        after_edit(NodeGraphEditKind::disconnected);
    }
    gesture_ = NodeCanvasGesture::none;
    link_hit_.reset();
}

void NodeCanvas::start_gesture(NodeCanvasGesture gesture, const MouseEventArgs& arguments, Rect window_source,
                               double threshold) {
    gesture_ = gesture;
    gesture_planned_ = false;
    press_view_ = arguments.location;
    current_view_ = arguments.location;
    pan_last_ = Point{window_source.x, window_source.y};
    link_hit_.reset();
    DragStart start{};
    start.key = static_cast<std::uint64_t>(gesture);
    start.source = window_source;
    start.threshold = threshold;
    const DragStartResult result = (*drag_).press(pointer_event_of(arguments), start);
    if (result == DragStartResult::armed || result == DragStartResult::started) {
        ++statistics_.gestures_started;
        return;
    }
    // Refused or not started: `finished` ran for a refused plan; otherwise
    // nothing started, and the gesture's group (a detach) is rolled back.
    settle_unplanned_gesture();
    gesture_ = NodeCanvasGesture::none;
}

void NodeCanvas::press_primary(MouseEventArgs& arguments) {
    NodeGraph& graph = *graph_;
    const Point local = arguments.location;
    const Point window = arguments.window_position;
    const Rect pointer_source{window.x, window.y, 1.0, 1.0};
    const bool shift = includes(arguments.modifiers, Modifier::shift);
    const bool command = command_modifier(arguments.modifiers);
    const NodeCanvasHit hit = hit_test(local);
    if (hit.kind == NodeCanvasHitKind::port) {
        const PortRef port{hit.node, hit.port};
        const PortRecord* record = graph.port(port);
        const std::optional<Point> anchor = port_view_anchor(port);
        if (record == nullptr || !anchor.has_value()) return;
        focused_node_ = hit.node;
        focused_port_ = hit.port;
        const Point anchor_window = point_to_window(*anchor);
        const Rect source{anchor_window.x - 1.0, anchor_window.y - 1.0, 2.0, 2.0};
        if ((*record).direction == PortDirection::output) {
            link_from_ = port;
            start_gesture(NodeCanvasGesture::link, arguments, source, default_drag_threshold);
            return;
        }
        const LinkId into = graph.link_into(port);
        const LinkRecord* link = graph.link(into);
        if (link == nullptr) return;
        // Detach: the link's output becomes the drag's origin, and detach
        // and reconnect are one undo step. The dangling end starts at the
        // input, so the drag needs no threshold.
        link_from_ = (*link).from;
        graph.begin_undo_group();
        gesture_group_open_ = true;
        static_cast<void>(graph.disconnect(into));
        ++statistics_.links_detached;
        start_gesture(NodeCanvasGesture::link, arguments, source, 0.0);
        after_edit(NodeGraphEditKind::disconnected);
        return;
    }
    if (hit.kind == NodeCanvasHitKind::node_body || hit.kind == NodeCanvasHitKind::node_caption) {
        if (shift) {
            add_to_selection(hit.node);
        } else if (command) {
            toggle_selection(hit.node);
        } else if (!selected(hit.node)) {
            select_only(hit.node);
        } else {
            // A press on a selected node keeps the selection for a drag; the
            // click after a release without a drag narrows it.
            pending_click_select_ = hit.node;
            focused_node_ = hit.node;
            focused_port_.reset();
        }
        if (!selected(hit.node)) return;
        move_anchor_ = hit.node;
        const NodeRecord* node = graph.node(hit.node);
        if (node == nullptr) return;
        const Rect source = rectangle_to_window(view_.rect_to_view((*node).bounds));
        start_gesture(NodeCanvasGesture::move, arguments, source, default_drag_threshold);
        return;
    }
    if (hit.kind == NodeCanvasHitKind::link) {
        select_link(hit.link);
        return;
    }
    if (!shift && !command) clear_selection();
    start_gesture(NodeCanvasGesture::marquee, arguments, pointer_source, default_drag_threshold);
}

// -- mouse ----------------------------------------------------------------------------------

void NodeCanvas::on_mouse_down(MouseEventArgs& arguments) {
    settle_unplanned_gesture();
    const bool first_press = arguments.clicks <= 1U;
    const bool idle = drag_ != nullptr && !(*drag_).running() && gesture_ == NodeCanvasGesture::none;
    if (graph_ != nullptr && first_press && idle && !in_callback_ && effectively_enabled()) {
        // A press on the canvas is outside the editor: the edit commits (as
        // a focus loss would), and a keyboard link ends.
        if (editor_ != nullptr) end_edit(NodeEditEnd::commit);
        keyboard_link_ = false;
        SurfaceRoot* root = attached_surface_root();
        if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
        pending_click_select_.reset();
        knife_finished_ = false;
        const Point window = arguments.window_position;
        const Rect pointer_source{window.x, window.y, 1.0, 1.0};
        const bool primary = arguments.button == PointerButton::primary;
        if (arguments.button == PointerButton::middle || (primary && space_held_)) {
            start_gesture(NodeCanvasGesture::pan, arguments, pointer_source, 0.0);
        } else if (arguments.button == PointerButton::secondary && command_modifier(arguments.modifiers)) {
            start_gesture(NodeCanvasGesture::knife, arguments, pointer_source, 2.0);
        } else if (primary) {
            press_primary(arguments);
        }
        if (gesture_ != NodeCanvasGesture::none) arguments.handled = true;
    }
    Control::on_mouse_down(arguments);
}

void NodeCanvas::on_mouse_move(MouseEventArgs& arguments) {
    if (drag_ != nullptr && (*drag_).running()) {
        const bool taken = (*drag_).pointer(pointer_event_of(arguments));
        if (taken) arguments.handled = true;
    } else if (graph_ != nullptr) {
        // Hover: the node under the pointer is hot.
        const NodeCanvasHit hit = hit_test(arguments.location);
        std::optional<std::uint32_t> hot{};
        if (hit.kind == NodeCanvasHitKind::node_body || hit.kind == NodeCanvasHitKind::node_caption ||
            hit.kind == NodeCanvasHitKind::port) {
            hot = hit.node.index;
        }
        if (hot != hot_node_) {
            hot_node_ = hot;
            invalidate(Dirty::paint);
        }
    }
    Control::on_mouse_move(arguments);
}

void NodeCanvas::on_mouse_leave() {
    if (hot_node_.has_value()) {
        hot_node_.reset();
        invalidate(Dirty::paint);
    }
    Control::on_mouse_leave();
}

void NodeCanvas::on_mouse_up(MouseEventArgs& arguments) {
    if (drag_ != nullptr && (*drag_).running()) {
        const bool planned = gesture_planned_;
        const bool taken = (*drag_).pointer(pointer_event_of(arguments));
        if (taken) arguments.handled = true;
        if (planned) {
            // The release of a drag is no click: the click handler does not
            // narrow the selection after a move.
            arguments.suppress_click = true;
            pending_click_select_.reset();
            ++statistics_.clicks_suppressed;
        } else {
            // A release while armed is the press's click: the session ended
            // without a plan, and `finished` was not called.
            settle_unplanned_gesture();
        }
    }
    Control::on_mouse_up(arguments);
}

void NodeCanvas::on_mouse_click(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary && pending_click_select_.has_value()) {
        const NodeId node = *pending_click_select_;
        pending_click_select_.reset();
        select_only(node);
    }
    Control::on_mouse_click(arguments);
}

void NodeCanvas::on_mouse_double_click(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary && graph_ != nullptr && !in_callback_) {
        const NodeCanvasHit hit = hit_test(arguments.location);
        if (hit.kind == NodeCanvasHitKind::node_body || hit.kind == NodeCanvasHitKind::node_caption) {
            select_only(hit.node);
            const NodeEditPart part =
                hit.kind == NodeCanvasHitKind::node_caption ? NodeEditPart::caption : NodeEditPart::body;
            static_cast<void>(begin_edit(hit.node, part));
        } else if (hit.kind == NodeCanvasHitKind::none) {
            request_palette(arguments.location, std::nullopt);
        }
        arguments.handled = true;
    }
    Control::on_mouse_double_click(arguments);
}

void NodeCanvas::on_mouse_wheel(MouseEventArgs& arguments) {
    double steps = arguments.wheel_delta.y;
    if (arguments.wheel_unit == WheelDeltaUnit::pixels) steps /= wheel_pixels_per_line;
    const double base = command_modifier(arguments.modifiers) ? fine_wheel_step : wheel_step;
    const double factor = std::pow(base, steps);
    zoom_about(arguments.location, factor);
    arguments.handled = true;
    Control::on_mouse_wheel(arguments);
}

void NodeCanvas::on_context_menu_requested(ContextMenuRequest& request) {
    if (knife_finished_) {
        // The knife's release (Ctrl + secondary) is no menu request.
        knife_finished_ = false;
        request.handled = true;
    } else if (request.origin == ContextRequestOrigin::keyboard && graph_ != nullptr) {
        // Shift+F10 or the menu key: the palette beside the focused node, or
        // at the middle of the view.
        const Rect client = client_rectangle();
        Point at{client.x + client.width / 2.0, client.y + client.height / 2.0};
        if (focused_node_.has_value()) {
            const NodeRecord* node = (*graph_).node(*focused_node_);
            if (node != nullptr) {
                const Rect shown = view_.rect_to_view((*node).bounds);
                at = Point{shown.x + shown.width + reveal_margin, shown.y};
            }
        }
        request_palette(at, std::nullopt);
        request.handled = true;
    }
    Control::on_context_menu_requested(request);
}

void NodeCanvas::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    // The tracker ends its session without a callback (subject_gone), so
    // the gesture's group is rolled back here.
    if (drag_ != nullptr) (*drag_).owner_detaching(former_window);
    if (gesture_ != NodeCanvasGesture::none) {
        if (gesture_group_open_ && graph_ != nullptr) static_cast<void>((*graph_).rollback_undo_group());
        gesture_group_open_ = false;
        gesture_ = NodeCanvasGesture::none;
        gesture_planned_ = false;
        link_hit_.reset();
    }
    keyboard_link_ = false;
    Control::on_detaching_from_window(former_window);
}

void NodeCanvas::request_palette(Point local, std::optional<PortRef> from) {
    NodePaletteRequest request{};
    request.world = view_.to_world(local);
    request.local = local;
    request.from = from;
    ++statistics_.palette_requests;
    palette_requested_.emit(request);
}

// -- drag targets -----------------------------------------------------------------------------

void NodeCanvas::free_placement(DragPlan& plan) const {
    // An unbounded canvas: the subject may go anywhere, with no grid.
    const Rect viewport = rectangle_to_window(client_rectangle());
    constexpr double far = 1.0e6;
    plan.kind = DragKind::placement;
    plan.placement.bounds = Rect{viewport.x - far, viewport.y - far, viewport.width + 2.0 * far,
                                 viewport.height + 2.0 * far};
    plan.placement.grid = Size{};
    plan.placement.grid_by_default = false;
    plan.placement.free_drop = true;
}

void NodeCanvas::plan_drag(DragPlan& plan) {
    gesture_planned_ = true;
    plan.container = this;
    const Rect viewport = rectangle_to_window(client_rectangle());
    switch (gesture_) {
    case NodeCanvasGesture::move:
        plan_move(plan);
        plan.auto_scroll.viewport = viewport;
        return;
    case NodeCanvasGesture::link:
        plan_link(plan);
        plan.auto_scroll.viewport = viewport;
        return;
    case NodeCanvasGesture::marquee:
    case NodeCanvasGesture::knife:
        free_placement(plan);
        plan.auto_scroll.viewport = viewport;
        return;
    case NodeCanvasGesture::pan:
        free_placement(plan);
        return;
    case NodeCanvasGesture::none:
        break;
    default:
        break;
    }
    // No gesture (or one this canvas does not know): nothing to drag.
    plan.refused = true;
}

void NodeCanvas::plan_move(DragPlan& plan) {
    // The snap grid is the world grid: bounds anchored at a far multiple of
    // the spacing, so the tracker's cells line up with world multiples.
    const double cell = grid_spacing_ * view_.zoom;
    const double far = -grid_spacing_ * snap_bounds_cells;
    const Point corner = point_to_window(view_.to_view(WorldPoint{far, far}));
    const double side = 2.0 * grid_spacing_ * snap_bounds_cells * view_.zoom;
    plan.kind = DragKind::placement;
    plan.placement.bounds = Rect{corner.x, corner.y, side, side};
    plan.placement.grid = Size{cell, cell};
    plan.placement.grid_by_default = snap_;
    plan.placement.grid_toggle = Modifier::control;
    plan.placement.free_drop = true;
    if (!gesture_group_open_ && graph_ != nullptr) {
        (*graph_).begin_undo_group();
        gesture_group_open_ = true;
    }
}

void NodeCanvas::plan_link(DragPlan& plan) {
    if (plan.epoch <= 1U) ++statistics_.link_drags;
    // The press moves with the content when an auto-scroll step plans
    // again, so the candidates are gathered around the press each time.
    collect_link_candidates(point_from_window(plan.press), true);
    targets_.clear();
    for (std::size_t index = 0U; index < candidates_.size(); ++index) {
        const Point window = point_to_window(candidates_[index].view_anchor);
        const Rect bounds{window.x - target_side / 2.0, window.y - target_side / 2.0, target_side, target_side};
        targets_.push_back(DragTarget{bounds, static_cast<std::uint32_t>(index), 0U});
    }
    free_placement(plan);
    plan.placement.targets = std::span<const DragTarget>(targets_.data(), targets_.size());
    plan.placement.snap_radius = magnet_snap_radius;
    plan.placement.release_radius = magnet_release_radius;
}

void NodeCanvas::preview_drag(const DragTrackerFrame& frame) {
    const Point top_left = point_from_window(frame.point);
    switch (gesture_) {
    case NodeCanvasGesture::move: {
        const NodeRecord* anchor = graph_ != nullptr ? (*graph_).node(move_anchor_) : nullptr;
        if (anchor == nullptr) {
            (*drag_).cancel(DragEnd::model_changed);
            return;
        }
        const WorldPoint world = view_.to_world(top_left);
        const WorldPoint delta{world.x - (*anchor).bounds.x, world.y - (*anchor).bounds.y};
        if (delta.x == 0.0 && delta.y == 0.0) return;
        const bool fresh = fresh_caches();
        // Inside the gesture's group, each frame's move joins the group's
        // first move record: a drag records once.
        const GraphResult moved =
            (*graph_).move_nodes(std::span<const NodeId>(selection_.data(), selection_.size()), delta);
        if (moved.status == GraphStatus::ok && fresh) update_moved_entries();
        if (editor_ != nullptr) invalidate(Dirty::arrange);
        invalidate(Dirty::paint);
        return;
    }
    case NodeCanvasGesture::link: {
        const bool had_target = link_hit_.has_value();
        link_hit_.reset();
        if (frame.hit == DragHit::target && frame.target < candidates_.size()) link_hit_ = frame.target;
        if (link_hit_.has_value() && !had_target) ++statistics_.magnet_captures;
        // The subject is the 2 x 2 square about the press: its centre is the
        // dangling end.
        current_view_ = Point{top_left.x + 1.0, top_left.y + 1.0};
        invalidate(Dirty::paint);
        return;
    }
    case NodeCanvasGesture::marquee:
    case NodeCanvasGesture::knife:
        current_view_ = top_left;
        invalidate(Dirty::paint);
        return;
    case NodeCanvasGesture::pan: {
        const Point delta{frame.point.x - pan_last_.x, frame.point.y - pan_last_.y};
        pan_last_ = frame.point;
        pan_by(delta);
        return;
    }
    case NodeCanvasGesture::none:
        break;
    default:
        break;
    }
    // A frame for no gesture is ignored.
}

void NodeCanvas::finish_move(const DragTrackerFrame& frame) {
    const bool commits = drag_end_commits(frame.end);
    close_gesture_group(commits);
    if (commits) {
        ++statistics_.gestures_committed;
    } else {
        ++statistics_.gestures_cancelled;
    }
    after_edit(NodeGraphEditKind::moved);
}

void NodeCanvas::finish_link(const DragTrackerFrame& frame) {
    const bool commits = drag_end_commits(frame.end);
    const bool on_target = frame.end == DragEnd::dropped && frame.hit == DragHit::target &&
                           frame.target < candidates_.size();
    std::optional<NodeLinkConnected> connected{};
    std::optional<Point> palette_at{};
    if (commits && on_target && graph_ != nullptr) {
        const PortRef to = candidates_[frame.target].port;
        const bool accepted = link_accepted(link_from_, to);
        const GraphResult result = accepted ? (*graph_).connect(link_from_, to) : GraphResult{GraphStatus::invalid_argument};
        if (result.status == GraphStatus::ok) {
            ++statistics_.connections_made;
            connected = NodeLinkConnected{result.link, link_from_, to};
        } else {
            ++statistics_.connections_refused;
        }
    } else if (commits) {
        // Released off every port: the palette, for kinds with a compatible
        // input, at the release point.
        palette_at = current_view_;
    }
    close_gesture_group(commits);
    if (commits) {
        ++statistics_.gestures_committed;
    } else {
        ++statistics_.gestures_cancelled;
    }
    after_edit(connected.has_value() ? NodeGraphEditKind::connected : NodeGraphEditKind::disconnected);
    if (connected.has_value()) link_connected_.emit(*connected);
    if (palette_at.has_value() && is_alive()) request_palette(*palette_at, link_from_);
}

void NodeCanvas::finish_drag(const DragTrackerFrame& frame) {
    const NodeCanvasGesture ended = gesture_;
    gesture_ = NodeCanvasGesture::none;
    switch (ended) {
    case NodeCanvasGesture::move:
        finish_move(frame);
        break;
    case NodeCanvasGesture::link:
        finish_link(frame);
        break;
    case NodeCanvasGesture::marquee:
        if (drag_end_commits(frame.end)) {
            const Point corner = point_from_window(frame.point);
            const Rect area{std::min(press_view_.x, corner.x), std::min(press_view_.y, corner.y),
                            std::fabs(corner.x - press_view_.x), std::fabs(corner.y - press_view_.y)};
            const bool touching = includes(frame.modifiers, Modifier::alt);
            const bool add = includes(frame.modifiers, Modifier::shift);
            const SpatialQueryMode mode = touching ? SpatialQueryMode::intersecting : SpatialQueryMode::contained;
            static_cast<void>(select_in_rect(view_.rect_to_world(area), mode, add));
            ++statistics_.marquee_selections;
            ++statistics_.gestures_committed;
        } else {
            ++statistics_.gestures_cancelled;
        }
        break;
    case NodeCanvasGesture::knife:
        if (drag_end_commits(frame.end)) {
            const Point end = point_from_window(frame.point);
            static_cast<void>(cut_links(press_view_, end));
            knife_finished_ = true;
            ++statistics_.gestures_committed;
        } else {
            ++statistics_.gestures_cancelled;
        }
        break;
    case NodeCanvasGesture::pan:
    case NodeCanvasGesture::none:
        break;
    default:
        break;
    }
    // A group a gesture left open (none should) is never kept.
    close_gesture_group(false);
    gesture_planned_ = false;
    link_hit_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

void NodeCanvas::announce_drag(DragAnnouncementRequest& request) {
    std::string text{};
    switch (gesture_) {
    case NodeCanvasGesture::move:
        text.assign("Moving ");
        append_count(text, selection_.size());
        text.append(selection_.size() == 1U ? " node" : " nodes");
        break;
    case NodeCanvasGesture::link:
        if (link_hit_.has_value() && *link_hit_ < candidates_.size() && graph_ != nullptr) {
            const PortRecord* port = (*graph_).port(candidates_[*link_hit_].port);
            text.assign("Over input ");
            if (port != nullptr) text.append((*port).name);
        } else {
            text.assign("Linking");
        }
        break;
    case NodeCanvasGesture::marquee: text.assign("Selecting"); break;
    case NodeCanvasGesture::knife: text.assign("Cutting links"); break;
    case NodeCanvasGesture::pan: break;
    case NodeCanvasGesture::none: break;
    default: break;
    }
    set_announcement(text);
    request.text = announcement_;
}

void NodeCanvas::scroll_drag(DragAutoScrollStep& step) {
    // An unbounded canvas: every requested step applies.
    pan_by(Point{-step.requested.x, -step.requested.y});
    step.applied = step.requested;
}

// -- keyboard -----------------------------------------------------------------------------

void NodeCanvas::announce_keyboard_target() {
    if (graph_ == nullptr || keyboard_target_ >= candidates_.size()) return;
    const PortRef target = candidates_[keyboard_target_].port;
    const NodeRecord* node = (*graph_).node(target.node);
    const PortRecord* port = (*graph_).port(target);
    std::string text{"Link to "};
    if (node != nullptr) text.append((*node).title);
    if (port != nullptr) {
        text.push_back(' ');
        text.append((*port).name);
    }
    text.append(", ");
    append_count(text, keyboard_target_ + 1U);
    text.append(" of ");
    append_count(text, candidates_.size());
    set_announcement(text);
}

bool NodeCanvas::start_keyboard_link() {
    if (!can_mutate() || !focused_node_.has_value()) return false;
    const NodeRecord* node = (*graph_).node(*focused_node_);
    if (node == nullptr) return false;
    std::optional<PortIndex> from{};
    if (focused_port_.has_value() && (*focused_port_).value < (*node).ports.size() &&
        (*node).ports[(*focused_port_).value].direction == PortDirection::output) {
        from = *focused_port_;
    }
    for (std::size_t port = 0U; port < (*node).ports.size() && !from.has_value(); ++port) {
        if ((*node).ports[port].direction == PortDirection::output) from = PortIndex{static_cast<std::uint16_t>(port)};
    }
    if (!from.has_value()) {
        set_announcement("No output to link");
        return false;
    }
    link_from_ = PortRef{*focused_node_, *from};
    const std::optional<Point> anchor = port_view_anchor(link_from_);
    if (!anchor.has_value()) return false;
    collect_link_candidates(*anchor, false);
    if (candidates_.empty()) {
        set_announcement("No compatible input");
        return false;
    }
    keyboard_link_ = true;
    keyboard_target_ = 0U;
    announce_keyboard_target();
    invalidate(Dirty::paint);
    return true;
}

bool NodeCanvas::handle_keyboard_link_key(std::uint32_t key) {
    const std::size_t count = candidates_.size();
    if (key == PhysicalKey::tab) return false;
    if (key == PhysicalKey::escape || count == 0U) {
        keyboard_link_ = false;
        set_announcement("Link cancelled");
        invalidate(Dirty::paint);
        return true;
    }
    if (key == PhysicalKey::up || key == PhysicalKey::left) {
        keyboard_target_ = keyboard_target_ == 0U ? count - 1U : keyboard_target_ - 1U;
        announce_keyboard_target();
        invalidate(Dirty::paint);
        return true;
    }
    if (key == PhysicalKey::down || key == PhysicalKey::right) {
        keyboard_target_ = keyboard_target_ + 1U >= count ? 0U : keyboard_target_ + 1U;
        announce_keyboard_target();
        invalidate(Dirty::paint);
        return true;
    }
    if (letter_key(key) && graph_ != nullptr) {
        // Type-ahead: the next target whose node's title starts with the
        // letter.
        const char wanted = letter_of(key);
        for (std::size_t step = 1U; step <= count; ++step) {
            const std::size_t at = (keyboard_target_ + step) % count;
            const NodeRecord* node = (*graph_).node(candidates_[at].port.node);
            if (node == nullptr || (*node).title.empty() || folded((*node).title.front()) != wanted) continue;
            keyboard_target_ = at;
            announce_keyboard_target();
            invalidate(Dirty::paint);
            break;
        }
        return true;
    }
    if (key == PhysicalKey::enter) {
        keyboard_link_ = false;
        const PortRef to = candidates_[keyboard_target_].port;
        const GraphResult result = connect_ports(link_from_, to);
        set_announcement(result.status == GraphStatus::ok ? "Linked" : "Link refused");
        invalidate(Dirty::paint);
        return true;
    }
    // The keyboard link is modal: every other key is taken and ignored.
    return true;
}

bool NodeCanvas::handle_navigation_key(std::uint32_t key, Modifier modifiers) {
    const bool command = command_modifier(modifiers);
    const bool shift = includes(modifiers, Modifier::shift);
    if (key == PhysicalKey::delete_forward || key == PhysicalKey::backspace) {
        // A focused input port: its link goes; otherwise the selection.
        if (focused_node_.has_value() && focused_port_.has_value() && can_mutate()) {
            const PortRef port{*focused_node_, *focused_port_};
            const LinkId into = (*graph_).link_into(port);
            if ((*graph_).link(into) != nullptr) {
                const GraphResult result = (*graph_).disconnect(into);
                if (result.status == GraphStatus::ok) after_edit(NodeGraphEditKind::disconnected);
                return true;
            }
        }
        static_cast<void>(delete_selection());
        return true;
    }
    if (key == PhysicalKey::z && command) {
        if (shift) {
            static_cast<void>(redo());
        } else {
            static_cast<void>(undo());
        }
        return true;
    }
    if (key == PhysicalKey::y && command) {
        static_cast<void>(redo());
        return true;
    }
    if (key == PhysicalKey::a && command) {
        select_all();
        return true;
    }
    if (key == PhysicalKey::home) {
        frame_all();
        return true;
    }
    if (key == PhysicalKey::f && !command) {
        frame_selection();
        return true;
    }
    const Rect client = client_rectangle();
    const Point middle{client.x + client.width / 2.0, client.y + client.height / 2.0};
    if (key == PhysicalKey::equal || key == PhysicalKey::keypad_plus) {
        zoom_about(middle, key_zoom_step);
        return true;
    }
    if (key == PhysicalKey::minus || key == PhysicalKey::keypad_minus) {
        zoom_about(middle, 1.0 / key_zoom_step);
        return true;
    }
    if (arrow_key(key)) {
        const Point direction = arrow_direction(key);
        if (command) {
            // Ctrl+arrows move the selection one grid cell: one step each.
            const WorldPoint delta{direction.x * grid_spacing_, direction.y * grid_spacing_};
            static_cast<void>(move_selection(delta));
            return true;
        }
        if (focused_node_.has_value() && focused_port_.has_value() && graph_ != nullptr) {
            // Inside the ports: the arrows walk them.
            const NodeRecord* node = (*graph_).node(*focused_node_);
            if (node != nullptr && !(*node).ports.empty()) {
                const std::size_t count = (*node).ports.size();
                std::size_t at = (*focused_port_).value;
                const bool back = key == PhysicalKey::up || key == PhysicalKey::left;
                at = back ? (at == 0U ? count - 1U : at - 1U) : (at + 1U >= count ? 0U : at + 1U);
                focused_port_ = PortIndex{static_cast<std::uint16_t>(at)};
                const PortRecord& port = (*node).ports[at];
                std::string text = port.direction == PortDirection::input ? "input " : "output ";
                text.append(port.name);
                set_announcement(text);
                invalidate(Dirty::paint | Dirty::semantics);
            }
            return true;
        }
        const std::optional<std::uint32_t> next = nearest_node_in_direction(direction.x, direction.y);
        if (next.has_value()) {
            const NodeId node = id_of_slot(*next);
            select_only(node);
            reveal_node(node);
            const NodeRecord* record = graph_ != nullptr ? (*graph_).node(node) : nullptr;
            if (record != nullptr) {
                std::string text = (*record).title;
                text.append(", ");
                text.append((*record).kind);
                set_announcement(text);
            }
        } else {
            pan_by(Point{-direction.x * key_pan_step, -direction.y * key_pan_step});
        }
        return true;
    }
    if (key == PhysicalKey::enter) {
        if (!focused_node_.has_value() || graph_ == nullptr) return false;
        const NodeRecord* node = (*graph_).node(*focused_node_);
        if (node == nullptr) return false;
        if (focused_port_.has_value()) {
            // Enter on an output starts the keyboard link from it.
            static_cast<void>(start_keyboard_link());
            return true;
        }
        if ((*node).ports.empty()) return false;
        focused_port_ = PortIndex{0U};
        const PortRecord& port = (*node).ports.front();
        std::string text = port.direction == PortDirection::input ? "input " : "output ";
        text.append(port.name);
        set_announcement(text);
        invalidate(Dirty::paint | Dirty::semantics);
        return true;
    }
    if (key == PhysicalKey::f2) {
        std::optional<NodeId> node = focused_node_;
        if (!node.has_value() && !selection_.empty()) node = selection_.front();
        if (!node.has_value()) return false;
        static_cast<void>(begin_edit(*node, shift ? NodeEditPart::caption : NodeEditPart::body));
        return true;
    }
    if (key == PhysicalKey::l && !command) {
        static_cast<void>(start_keyboard_link());
        return true;
    }
    if (key == PhysicalKey::space) {
        space_held_ = true;
        return true;
    }
    if (key == PhysicalKey::escape) {
        if (focused_port_.has_value()) {
            focused_port_.reset();
            invalidate(Dirty::paint | Dirty::semantics);
            return true;
        }
        if (selection_.empty() && !selected_link_.has_value()) return false;
        clear_selection();
        return true;
    }
    return false;
}

void NodeCanvas::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || in_callback_ || graph_ == nullptr) return;
    const std::uint32_t key = arguments.physical_key;
    const bool consumed =
        keyboard_link_ ? handle_keyboard_link_key(key) : handle_navigation_key(key, arguments.modifiers);
    if (!consumed) return;
    arguments.handled = true;
    // The canvas's letter keys (L, F, Space, type-ahead) produce no text.
    arguments.suppress_key_press = true;
    ++statistics_.key_presses_suppressed;
}

void NodeCanvas::on_key_up(KeyEventArgs& arguments) {
    Control::on_key_up(arguments);
    if (arguments.physical_key == PhysicalKey::space) space_held_ = false;
}

} // namespace gui_forms
