// Vertical ToolStrips, ToolStripPanel and ToolStripContainer (stage W):
// vertical item layout and keys, panel stacking and orientation, and the
// container's four panels around its content.

#include "gui_forms/gui_forms.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct RecordClick final {
    std::vector<std::string>* seen{};
    void operator()(const ToolStripItemClick& click) const { (*seen).push_back(click.item_id); }
};

const ToolStrip::Item* find_item(const std::vector<ToolStrip::Item>& items, std::string_view id) {
    for (const ToolStrip::Item& item : items) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const ToolStrip::Item* find_item(std::vector<ToolStrip::Item>&& items, std::string_view id) = delete;

std::vector<ToolStripItemSpec> tools() {
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::vector<ToolStripItemSpec> items;
    items.push_back({.stable_id = "pen", .text = "Pen"});
    items.push_back({.stable_id = "rule", .kind = ToolStripItemKind::separator});
    items.push_back({.stable_id = "brush", .text = "Brush"});
    items.push_back({.stable_id = "shapes", .kind = ToolStripItemKind::drop_down_button, .text = "Shapes",
                     .drop_down = {MenuItemSpec("shapes.square", MenuItemKind::command, choice, "Square")}});
    return items;
}

std::shared_ptr<ToolStrip> strip_named(std::string_view id) {
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId(std::string(id)));
    (*strip).set_items(tools());
    return strip;
}

void vertical_strip() {
    std::shared_ptr<ToolStrip> strip = strip_named("tools");
    (*strip).set_orientation(Orientation::vertical);
    require((*strip).orientation() == Orientation::vertical, "a strip turns vertical");
    const Size measured = (*strip).measure({400.0, 300.0});
    require(measured.width >= 25.0 && measured.width < 400.0 && measured.height == 300.0,
            "a vertical strip is as wide as its widest item and takes the height offered");
    Window window(strip, {measured.width, 300.0});
    window.perform_layout();
    const std::vector<ToolStrip::Item> items = (*strip).realized_items();
    const ToolStrip::Item* pen = find_item(items, "pen");
    const ToolStrip::Item* brush = find_item(items, "brush");
    const ToolStrip::Item* rule = find_item(items, "rule");
    require(pen != nullptr && brush != nullptr && rule != nullptr &&
                (*brush).parts.item_face.bounds.y > (*pen).parts.item_face.bounds.y &&
                (*brush).parts.item_face.bounds.x == (*pen).parts.item_face.bounds.x,
            "a vertical strip stacks its items top to bottom");
    require((*rule).parts.item_separator.bounds.width > (*rule).parts.item_separator.bounds.height,
            "a vertical strip's separator is a level rule");
    require((*strip).parts().grip.bounds.width > (*strip).parts().grip.bounds.height,
            "a vertical strip's grip lies across its top");

    std::vector<std::string> clicks;
    SubscriptionToken token = (*strip).item_clicked().subscribe(RecordClick{&clicks});
    require(window.request_focus(strip), "the strip takes focus");
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    require(window.dispatch_key(down) && window.dispatch_key(enter) && clicks.size() == 1U && clicks[0] == "brush",
            "Down moves to the next item of a vertical strip");
    require(window.dispatch_key(down), "Down reaches the drop-down button");
    KeyEvent right{KeyAction::down, PhysicalKey::right};
    require(window.dispatch_key(right) && (*strip).drop_down_open(), "Right opens a vertical strip's menu");
    (*strip).close_drop_down();

    std::shared_ptr<StatusStrip> status = make_control<StatusStrip>(StableId("status"));
    bool threw = false;
    try {
        (*status).set_orientation(Orientation::vertical);
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw && (*status).orientation() == Orientation::horizontal, "a StatusStrip stays horizontal");
}

void panels() {
    std::shared_ptr<ToolStripPanel> panel = make_control<ToolStripPanel>(StableId("panel"));
    require(validate_control_anatomy((*panel).anatomy()).defect == AnatomyDefect::none &&
                (*panel).anatomy().control_id == "tool_strip_panel",
            "ToolStripPanel publishes a valid anatomy");
    require((*panel).measure({500.0, 300.0}).height == 0.0, "an empty panel has no thickness");
    std::shared_ptr<ToolStrip> first = strip_named("first");
    std::shared_ptr<ToolStrip> second = strip_named("second");
    (*panel).join(first);
    (*panel).join(second);
    require((*panel).strips().size() == 2U && (*panel).measure({500.0, 300.0}).height == 50.0,
            "a top panel is as tall as its strips' rows together");
    Window window(panel, {500.0, 50.0});
    window.perform_layout();
    require((*first).committed_arranged_bounds() == Rect{0.0, 0.0, 500.0, 25.0} &&
                (*second).committed_arranged_bounds() == Rect{0.0, 25.0, 500.0, 25.0},
            "each strip takes its own row across the panel");
    bool threw = false;
    try {
        (*panel).join(first);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a strip that already has a parent is refused");
    (*panel).set_edge(ToolStripPanelEdge::left);
    require((*first).orientation() == Orientation::vertical && (*second).orientation() == Orientation::vertical,
            "moving the panel to the left turns its strips vertical");
    std::shared_ptr<StatusStrip> status = make_control<StatusStrip>(StableId("status"));
    threw = false;
    try {
        (*panel).join(status);
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw, "a StatusStrip cannot join a left panel");
    const std::shared_ptr<ToolStrip> removed = (*panel).remove(*second);
    require(removed == second && (*panel).strips().size() == 1U && !(*second).parent(),
            "remove hands a strip back");
    require((*panel).remove(*second) == nullptr, "removing a strip that is not joined returns null");
}

void container() {
    std::shared_ptr<ToolStripContainer> frame = make_control<ToolStripContainer>(StableId("frame"));
    require(validate_control_anatomy((*frame).anatomy()).defect == AnatomyDefect::none &&
                (*frame).anatomy().control_id == "tool_strip_container",
            "ToolStripContainer publishes a valid anatomy");
    require((*(*frame).top_panel()).stable_id().value() == "frame.top" &&
                (*(*frame).left_panel()).edge() == ToolStripPanelEdge::left &&
                (*(*frame).content_panel()).stable_id().value() == "frame.content",
            "the container owns its four panels and its content panel");
    std::shared_ptr<ToolStrip> top = strip_named("top-tools");
    std::shared_ptr<ToolStrip> left = strip_named("left-tools");
    std::shared_ptr<StatusStrip> status = make_control<StatusStrip>(StableId("status"));
    (*status).set_items({{.stable_id = "ready", .kind = ToolStripItemKind::label, .text = "Ready"}});
    (*(*frame).top_panel()).join(top);
    (*(*frame).left_panel()).join(left);
    (*(*frame).bottom_panel()).join(status);
    std::shared_ptr<Button> body = make_control<Button>(StableId("body"), "Body");
    (*body).set_dock(DockStyle::fill);
    (*(*frame).content_panel()).add_child(body);
    Window window(frame, {600.0, 400.0});
    window.perform_layout();
    ToolStripContainer::Parts parts = (*frame).parts();
    require(parts.top_panel.present && parts.top_panel.bounds == Rect{0.0, 0.0, 600.0, 25.0},
            "the top panel spans the width, as tall as its strip");
    require(parts.bottom_panel.present && parts.bottom_panel.bounds.y + parts.bottom_panel.bounds.height == 400.0,
            "the bottom panel sits on the bottom edge");
    require(parts.left_panel.present && parts.left_panel.bounds.x == 0.0 && parts.left_panel.bounds.y == 25.0 &&
                parts.left_panel.bounds.width >= 25.0 && (*left).orientation() == Orientation::vertical,
            "the left panel holds a vertical strip between the top and bottom panels");
    require(!parts.right_panel.present, "an empty right panel takes no room");
    require(parts.content_panel.present &&
                parts.content_panel.bounds.x == parts.left_panel.bounds.width &&
                parts.content_panel.bounds.y == 25.0 &&
                parts.content_panel.bounds.width == 600.0 - parts.left_panel.bounds.width,
            "the content panel takes what the panels leave");
    (*frame).set_top_panel_visible(false);
    window.perform_layout();
    parts = (*frame).parts();
    require(!parts.top_panel.present && parts.content_panel.bounds.y == 0.0 && !(*top).effectively_visible(),
            "a hidden panel takes no room and hides its strips");
    require((*frame).semantic_descriptor().role == SemanticRole::group, "the container is a group");
}

struct RecordMove final {
    std::vector<ToolStripMove>* seen{};
    void operator()(const ToolStripMove& move) const { (*seen).push_back(move); }
};

// The middle of a strip's grip, in window coordinates.
Point grip_point(const ToolStrip& strip) {
    const Rect grip = strip.parts().grip.bounds;
    const Rect placed = strip.absolute_bounds();
    const Point middle{placed.x + grip.x + grip.width * 0.5, placed.y + grip.y + grip.height * 0.5};
    return middle;
}

void pointer(Window& window, PointerAction action, Point at, Modifier modifiers = Modifier::none) {
    const PointerButton button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    PointerEvent event{action, button, at};
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(event));
}

// Runs the frames well past the stamp's motion, so a settling stamp ends.
void settle(Window& window) {
    window.perform_layout();
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    window.perform_layout();
}

std::size_t row_of(const ToolStripPanel& panel, const ToolStrip& strip) {
    const std::vector<std::shared_ptr<ToolStrip>> joined = panel.strips();
    for (std::size_t row = 0U; row < joined.size(); ++row) {
        if (joined[row].get() == &strip) return row;
    }
    return joined.size();
}

// Dragging strips by their grips (owner decision DK-36, K2-1).
void dragging() {
    std::shared_ptr<ToolStripContainer> frame = make_control<ToolStripContainer>(StableId("frame"));
    std::shared_ptr<ToolStrip> first = strip_named("first");
    std::shared_ptr<ToolStrip> second = strip_named("second");
    std::shared_ptr<StatusStrip> status = make_control<StatusStrip>(StableId("status"));
    (*status).set_items({{.stable_id = "ready", .kind = ToolStripItemKind::label, .text = "Ready"}});
    (*(*frame).top_panel()).join(first);
    (*(*frame).top_panel()).join(second);
    (*(*frame).bottom_panel()).join(status);
    Window window(frame, {600.0, 400.0});
    window.perform_layout();
    std::vector<ToolStripMove> moves;
    SubscriptionToken token = (*frame).strip_moved().subscribe(RecordMove{&moves});
    require((*frame).drag_threshold() == 4.0 && (*frame).drag_phase() == ToolStripDragPhase::idle,
            "a container starts with no drag and the 4-unit threshold");

    // A press on the grip arms; the threshold starts the drag.
    const Point press = grip_point(*first);
    pointer(window, PointerAction::move, press);
    pointer(window, PointerAction::down, press);
    require((*frame).drag_phase() == ToolStripDragPhase::armed && (*frame).dragged_strip() == first,
            "a press on a grip arms a drag of that strip");
    pointer(window, PointerAction::move, {press.x + 2.0, press.y});
    require((*frame).drag_phase() == ToolStripDragPhase::armed, "a move under the threshold does not drag");
    pointer(window, PointerAction::move, {6.0, 200.0});
    require((*frame).drag_phase() == ToolStripDragPhase::dragging && !(*frame).drag_stamp().empty(),
            "past the threshold the strip drags and shows its stamp");
    std::optional<ToolStripDropTarget> target = (*frame).drag_target();
    require(target && (*target).edge == ToolStripPanelEdge::left && (*target).row == 0U &&
                (*frame).parts().drop_marker.present,
            "the empty left panel's band takes the strip, marked by its line");
    pointer(window, PointerAction::move, {6.0, 200.0}, Modifier::control);
    require((*frame).drag_suppressed() && !(*frame).drag_target(), "Ctrl suppresses the targets");
    pointer(window, PointerAction::move, {6.0, 200.0});
    pointer(window, PointerAction::up, {6.0, 200.0});
    require((*frame).drag_phase() == ToolStripDragPhase::settling, "after the drop the stamp settles");
    settle(window);
    require((*frame).drag_phase() == ToolStripDragPhase::idle && (*frame).drag_stamp().empty() &&
                (*first).parent() == (*frame).left_panel() &&
                (*first).orientation() == Orientation::vertical,
            "a release on the left band moves the strip there and turns it vertical");
    require(moves.size() == 1U && moves[0].strip_id == "first" && moves[0].from == ToolStripPanelEdge::top &&
                moves[0].from_row == 0U && moves[0].to == ToolStripPanelEdge::left && moves[0].to_row == 0U,
            "the move is reported once, from the top row to the left panel");

    // Escape cancels: nothing moves.
    const Point again = grip_point(*second);
    pointer(window, PointerAction::move, again);
    pointer(window, PointerAction::down, again);
    pointer(window, PointerAction::move, {300.0, 396.0});
    target = (*frame).drag_target();
    require((*frame).drag_phase() == ToolStripDragPhase::dragging && target &&
                (*target).edge == ToolStripPanelEdge::bottom,
            "the bottom panel, holding a status strip, is a target");
    KeyEvent escape{KeyAction::down, PhysicalKey::escape};
    static_cast<void>(window.dispatch_key(escape));
    pointer(window, PointerAction::up, {300.0, 396.0});
    settle(window);
    require((*frame).drag_phase() != ToolStripDragPhase::dragging && (*second).parent() == (*frame).top_panel() &&
                moves.size() == 1U,
            "Escape cancels the drag and leaves the strip where it was");

    // A release outside every band leaves the strip too (no floating).
    pointer(window, PointerAction::down, grip_point(*second));
    pointer(window, PointerAction::move, {300.0, 200.0});
    require(!(*frame).drag_target(), "the content area is no target");
    pointer(window, PointerAction::up, {300.0, 200.0});
    settle(window);
    require((*second).parent() == (*frame).top_panel() && moves.size() == 1U,
            "a release where nothing accepts leaves the strip; strips never float");

    // The right panel's band takes an ordinary strip.
    pointer(window, PointerAction::down, grip_point(*second));
    pointer(window, PointerAction::move, {594.0, 200.0});
    target = (*frame).drag_target();
    require(target && (*target).edge == ToolStripPanelEdge::right, "the empty right panel's band is a target");
    require((*frame).cancel_drag() && (*second).parent() == (*frame).top_panel(), "cancel_drag moves nothing");
    settle(window);

    // move_strip: refusals change nothing; a move reorders and reports.
    require(!(*frame).move_strip(*status, ToolStripPanelEdge::left, 0U) &&
                !(*frame).move_strip(*second, ToolStripPanelEdge::left, 2U) &&
                (*second).parent() == (*frame).top_panel(),
            "move_strip refuses a StatusStrip on the left and a row past the end");
    require((*frame).move_strip(*second, ToolStripPanelEdge::left, 0U) &&
                row_of(*(*frame).left_panel(), *second) == 0U && row_of(*(*frame).left_panel(), *first) == 1U &&
                moves.size() == 2U && moves[1].to == ToolStripPanelEdge::left && moves[1].to_row == 0U,
            "move_strip puts a strip before another in a panel");
    require((*frame).move_strip(*second, ToolStripPanelEdge::left, 0U) && moves.size() == 2U,
            "a strip already in place reports nothing");

    // Reordering within a panel by dragging: after the other strip.
    window.perform_layout();
    const Rect behind = (*first).absolute_bounds();
    pointer(window, PointerAction::down, grip_point(*second));
    pointer(window, PointerAction::move, {behind.x + behind.width * 0.75, 200.0});
    target = (*frame).drag_target();
    require(target && (*target).edge == ToolStripPanelEdge::left && (*target).row == 1U,
            "past the middle of the other strip the target is after it");
    pointer(window, PointerAction::up, {behind.x + behind.width * 0.75, 200.0});
    settle(window);
    require(row_of(*(*frame).left_panel(), *second) == 1U && moves.size() == 3U && moves[2].from_row == 0U &&
                moves[2].to_row == 1U,
            "the drop reorders the panel's strips");
    bool threw = false;
    try {
        (*frame).set_drag_threshold(0.5);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && (*frame).drag_threshold() == 4.0, "a threshold under 1 is refused");

    bool refused = false;
    try {
        (*(*frame).right_panel()).insert(strip_named("late"), 1U);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*(*frame).right_panel()).strips().empty(), "insert refuses a row past the joined strips");

    // Reduced motion: no settling.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    window.perform_layout();
    pointer(window, PointerAction::down, grip_point(*second));
    pointer(window, PointerAction::move, {300.0, 6.0});
    pointer(window, PointerAction::up, {300.0, 6.0});
    require((*frame).drag_phase() == ToolStripDragPhase::idle && (*second).parent() == (*frame).top_panel() &&
                (*second).orientation() == Orientation::horizontal && moves.size() == 4U,
            "under reduced motion the drop ends the drag at once");
}

} // namespace

int main() {
    vertical_strip();
    panels();
    container();
    dragging();
    std::cout << "Vertical ToolStrip, ToolStripPanel and ToolStripContainer: layout, keys, orientation, "
                 "panels and content passed\n";
    return 0;
}
