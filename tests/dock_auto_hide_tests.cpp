// DockSite auto-hide (stage W, docking phase D5): side strips built from
// PageStrip on the window's edges, flyouts as in-window overlays, the pin
// auto-hiding a whole root-level stack and docking it back exactly, the
// flyout's open and close order for hover, click, focus loss, an outside
// press and Escape, focus returned after Escape, the slide and reduced
// motion, the grip, the click-only workspace setting, the keyboard, a flyout
// never editing the layout, collapsing by priority into side tabs and back,
// and the semantics of strips and flyouts.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/dock_workspace_values.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

Rect to_window(Rect absolute, Rect local) {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const SemanticNode* nested = find_node(node.children, stable_id)) return nested;
    }
    return nullptr;
}
// The result borrows from nodes, so a temporary snapshot is refused (house style §1).
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

bool key(Window& window, std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical_key};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

// Runs the window's timers due `delay` from now.
void advance(Window& window, std::chrono::milliseconds delay) {
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + delay));
    window.perform_layout();
}

// Builds each panel's content: a panel holding a button
// (`content.<id>` and `content.<id>.edit`). A button, not a text box: until
// the TextBox Escape change (stage W) is in this stack, a focused TextBox
// keeps Escape from its container.
class PanelFactory final {
public:
    void create(DockPanelCreation& creation) {
        made.push_back(creation.panel.value);
        const std::string id = "content." + creation.panel.value;
        std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
        std::shared_ptr<Button> edit = make_control<Button>(StableId(id + ".edit"), creation.panel.value);
        (*edit).set_requested_bounds({6.0, 6.0, 80.0, 22.0});
        (*panel).add_child(edit);
        creation.content = panel;
    }

    std::vector<std::string> made;
};

DockPanelRegistration registration(PanelFactory& factory, std::string id, std::string title, DockPanelKind kind,
                                   DockSide side, std::int32_t priority, Size minimum) {
    DockPanelRegistration entry{};
    entry.descriptor.id = DockPanelId(std::move(id));
    entry.descriptor.kind = kind;
    entry.descriptor.collapse_priority = priority;
    entry.descriptor.minimum_size = minimum;
    entry.descriptor.default_placement.side = side;
    entry.descriptor.default_placement.ratio = 0.25;
    entry.title = std::move(title);
    entry.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return entry;
}

void apply(DockWorkspace& workspace, DockEdit edit) {
    DockTransaction transaction{};
    transaction.edits.push_back(std::move(edit));
    require(workspace.apply(transaction).applied, "a fixture edit applies");
}

// Records the flyout's changes and the focus's moves, in order.
struct Trace final {
    std::vector<std::string> lines;
};

std::string_view cause_name(DockFlyoutCause cause) {
    switch (cause) {
    case DockFlyoutCause::hover: return "hover";
    case DockFlyoutCause::pointer: return "pointer";
    case DockFlyoutCause::keyboard: return "keyboard";
    case DockFlyoutCause::programmatic: return "programmatic";
    case DockFlyoutCause::pointer_left: return "pointer_left";
    case DockFlyoutCause::outside_press: return "outside_press";
    case DockFlyoutCause::focus_lost: return "focus_lost";
    case DockFlyoutCause::escape: return "escape";
    case DockFlyoutCause::swapped: return "swapped";
    case DockFlyoutCause::layout: return "layout";
    }
    return "?";
}

struct FlyoutRecorder final {
    Trace* trace{};
    void operator()(const DockFlyoutChange& change) const {
        (*trace).lines.push_back(std::string(change.open ? "open " : "close ") + change.panel.value + " " +
                                 std::string(cause_name(change.cause)));
    }
};

struct FocusRecorder final {
    Trace* trace{};
    void operator()(const FocusedControlChange& change) const {
        (*trace).lines.push_back("focus " + change.stable_id);
    }
};

// Solution and Classes tabbed on the left (Classes selected), Properties on
// the right, Output at the bottom and two documents (Second selected), in an
// 800 by 500 window.
struct Fixture final {
    PanelFactory factory;
    std::unique_ptr<DockWorkspace> workspace;
    std::shared_ptr<DockSite> site;
    std::unique_ptr<Window> window;
    Trace trace;
    SubscriptionToken flyout_token;
    SubscriptionToken focus_token;
};

std::unique_ptr<Fixture> make_fixture() {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).workspace = std::make_unique<DockWorkspace>("workspace");
    DockWorkspace& workspace = *(*fixture).workspace;
    PanelFactory& factory = (*fixture).factory;
    require(workspace.register_panel(registration(factory, "solution", "Solution", DockPanelKind::tool, DockSide::left,
                                                  2, {120.0, 100.0})) &&
                workspace.register_panel(registration(factory, "classes", "Classes", DockPanelKind::tool,
                                                      DockSide::left, 2, {120.0, 100.0})) &&
                workspace.register_panel(registration(factory, "properties", "Properties", DockPanelKind::tool,
                                                      DockSide::right, 1, {140.0, 100.0})) &&
                workspace.register_panel(registration(factory, "output", "Output", DockPanelKind::tool,
                                                      DockSide::bottom, 0, {100.0, 60.0})) &&
                workspace.register_panel(registration(factory, "doc.first", "First.txt", DockPanelKind::document,
                                                      DockSide::left, 0, {120.0, 80.0})) &&
                workspace.register_panel(registration(factory, "doc.second", "Second.txt", DockPanelKind::document,
                                                      DockSide::left, 0, {120.0, 80.0})),
            "the panels register");
    apply(workspace, DockEdit::show(DockPanelId("doc.first")));
    apply(workspace, DockEdit::show(DockPanelId("doc.second")));
    apply(workspace, DockEdit::show(DockPanelId("solution")));
    const DockNodeId left = workspace.layout().locate(DockPanelId("solution")).stack;
    apply(workspace, DockEdit::dock_into(DockSubject::of_panel(DockPanelId("classes")), left));
    apply(workspace, DockEdit::show(DockPanelId("properties")));
    apply(workspace, DockEdit::show(DockPanelId("output")));
    (*fixture).site = make_control<DockSite>(StableId("site"), workspace);
    (*fixture).window = std::make_unique<Window>((*fixture).site, Size{800.0, 500.0});
    Window& window = *(*fixture).window;
    window.perform_layout();
    (*fixture).flyout_token = (*(*fixture).site).flyout_changed().subscribe(FlyoutRecorder{&(*fixture).trace});
    (*fixture).focus_token = window.focused_control_changed().subscribe(FocusRecorder{&(*fixture).trace});
    return fixture;
}

DockNodeId stack_of(const DockWorkspace& workspace, std::string_view panel) {
    const DockNodeId stack = workspace.layout().locate(DockPanelId(std::string(panel))).stack;
    return stack;
}

std::string focused_id(const Window& window) {
    const Control::Ptr focused = window.focused_control();
    if (!focused) return {};
    return std::string((*focused).stable_id().value());
}

// The centre of a panel's side tab, in window coordinates.
Point tab_point(const DockSite& site, DockSide edge, std::string_view panel) {
    const std::shared_ptr<PageStrip> strip = site.side_strip(edge);
    require(strip != nullptr, "the edge shows side tabs");
    const PageStrip::ItemParts parts = (*strip).tab_parts_of(PageId{std::string(panel)});
    require(parts.tab.present, "the panel has a side tab");
    const Point point = centre(to_window((*strip).absolute_bounds(), parts.tab.bounds));
    return point;
}

// A stack's caption button, in window coordinates.
const DockSite::Stack* find_stack(const std::vector<DockSite::Stack>& stacks, DockNodeId stack) {
    for (const DockSite::Stack& record : stacks) {
        if (record.stack == stack) return &record;
    }
    return nullptr;
}
// The result borrows from stacks, so a temporary list is refused (house style §1).
const DockSite::Stack* find_stack(std::vector<DockSite::Stack>&& stacks, DockNodeId stack) = delete;

Point pin_point(const DockSite& site, DockNodeId stack) {
    const std::vector<DockSite::Stack> items = site.realized_stack_items();
    const DockSite::Stack* const record = find_stack(items, stack);
    require(record != nullptr && (*record).parts.caption_pin.present, "the stack shows a pin");
    const Point point = centre(to_window(site.absolute_bounds(), (*record).parts.caption_pin.bounds));
    return point;
}

std::string joined(const Trace& trace) {
    std::string text;
    for (const std::string& line : trace.lines) {
        text += line;
        text += "; ";
    }
    return text;
}

void reduce_motion(Window& window) {
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
}

// -- the pin: auto-hide the whole stack and dock it back exactly ------------------------------

void pin_round_trip() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    reduce_motion(window);
    const std::string before = workspace.layout().write_xml().text;
    const std::uint64_t generation = workspace.layout().generation();
    const DockNodeId left = stack_of(workspace, "classes");
    const std::size_t created = (*fixture).factory.made.size();

    // The pin of the left group (Classes and Solution) auto-hides both.
    click(window, pin_point(site, left));
    require(workspace.layout().generation() == generation + 1U &&
                workspace.layout().locate(DockPanelId("classes")).placement == DockPlacementKind::auto_hidden &&
                workspace.layout().locate(DockPanelId("solution")).placement == DockPlacementKind::auto_hidden &&
                workspace.layout().state().auto_hidden.size() == 1U &&
                workspace.layout().state().auto_hidden[0].stack == left &&
                workspace.layout().state().auto_hidden[0].edge == DockSide::left,
            "the pin auto-hides the whole group onto the nearest edge in one transaction, keeping its node");
    window.perform_layout();
    const std::vector<DockPanelId> tabs = site.side_panels(DockSide::left);
    require(tabs.size() == 2U && tabs[0].value == "solution" && tabs[1].value == "classes",
            "the left side strip shows the group's panels in stack order");
    require(site.side_strip(DockSide::right) == nullptr && site.side_strip(DockSide::bottom) == nullptr,
            "edges without auto-hidden panels show no strip");
    require(site.parts().document_area.bounds.x >= (*site.side_strip(DockSide::left)).absolute_bounds().width,
            "the tree is laid out beside the strip's band");
    require(std::find(window.focus_regions().begin(), window.focus_regions().end(),
                      static_cast<Control::Ptr>(site.side_strip(DockSide::left))) != window.focus_regions().end(),
            "the side strip is a focus region");
    require((*fixture).factory.made.size() == created, "auto-hiding creates no content");

    // The flyout of Classes; its pin (unchecked) docks the group back.
    click(window, tab_point(site, DockSide::left, "classes"));
    window.perform_layout();
    require(site.flyout_open() && (*site.flyout_panel()).value == "classes" && site.flyout_reveal() == 1.0,
            "a click opens the flyout at once under reduced motion");
    require(workspace.layout().generation() == generation + 1U, "opening the flyout edits nothing");
    const std::vector<DockSite::Stack> items = site.realized_stack_items();
    const DockSite::Stack* const flyout_stack = find_stack(items, left);
    require(flyout_stack != nullptr && (*flyout_stack).parts.caption_pin.present &&
                !(*flyout_stack).parts.caption_pin.states.contains(AnatomyState::checked) &&
                !(*flyout_stack).parts.caption_pin.states.contains(AnatomyState::disabled),
            "the flyout's caption carries an unchecked, enabled pin");
    click(window, pin_point(site, left));
    window.perform_layout();
    require(!site.flyout_open() &&
                workspace.layout().locate(DockPanelId("classes")).placement == DockPlacementKind::docked &&
                workspace.layout().generation() == generation + 2U,
            "the flyout's pin docks the group back in one transaction");
    require(workspace.layout().write_xml().text == before,
            "after the pin and the unpin the saved layout is byte for byte what it was");
    require(site.side_strip(DockSide::left) == nullptr, "the side strip goes with its last tab");

    // The position menu's Auto Hide does the same for Properties.
    require(site.position_command_enabled(DockPanelId("properties"), DockPositionCommand::auto_hide) &&
                site.run_position_command(DockPanelId("properties"), DockPositionCommand::auto_hide) &&
                workspace.layout().state().auto_hidden.size() == 1U &&
                workspace.layout().state().auto_hidden[0].edge == DockSide::right,
            "Auto Hide pins Properties to the right edge");
    require(site.position_command_enabled(DockPanelId("properties"), DockPositionCommand::dock) &&
                !site.position_command_enabled(DockPanelId("properties"), DockPositionCommand::auto_hide) &&
                site.run_position_command(DockPanelId("properties"), DockPositionCommand::dock) &&
                workspace.layout().write_xml().text == before,
            "Dock returns it, and the layout is again what it was");
    require(static_cast<bool>(workspace.layout().check_invariants()), "the layout stays valid");
}

// -- hover: open after the delay, close after leaving --------------------------------------------

void hover_order() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(stack_of(workspace, "output")), DockSide::bottom, 150.0));
    window.perform_layout();
    require(workspace.activate_panel(DockPanelId("doc.second")) && focused_id(window) == "content.doc.second.edit",
            "focus is in the second document");
    const std::string before = workspace.layout().write_xml().text;
    const std::uint64_t generation = workspace.layout().generation();
    (*fixture).trace.lines.clear();

    move_to(window, tab_point(site, DockSide::bottom, "output"));
    advance(window, std::chrono::milliseconds(200));
    require(!site.flyout_open(), "the flyout waits for the hover delay");
    advance(window, std::chrono::milliseconds(450));
    require(site.flyout_open() && (*site.flyout_panel()).value == "output" &&
                focused_id(window) == "content.doc.second.edit",
            "after the delay the flyout opens and focus stays where it was");
    require((*fixture).trace.lines.size() == 1U && (*fixture).trace.lines[0] == "open output hover",
            "one open, by hover");
    advance(window, std::chrono::milliseconds(540));
    require(site.flyout_reveal() == 1.0, "the slide ends");

    // The pointer inside the flyout keeps it; leaving it starts the delay.
    const Rect flyout = (*site.flyout_control()).absolute_bounds();
    move_to(window, centre(flyout));
    advance(window, std::chrono::milliseconds(1200));
    require(site.flyout_open(), "the pointer resting in the flyout keeps it out");
    const Rect documents = site.parts().document_area.bounds;
    move_to(window, Point{documents.x + 30.0, documents.y + 30.0});
    advance(window, std::chrono::milliseconds(300));
    require(site.flyout_open(), "the leave delay has not passed");
    advance(window, std::chrono::milliseconds(600));
    require(!site.flyout_open() && (*fixture).trace.lines.size() == 2U &&
                (*fixture).trace.lines[1] == "close output pointer_left",
            "the leave delay after the pointer left closes it");
    require(workspace.layout().generation() == generation && workspace.layout().write_xml().text == before,
            "hovering, sliding and leaving never edit the layout");

    // Click only: hovering opens nothing.
    workspace.flyout_opening = DockFlyoutOpening::click;
    move_to(window, tab_point(site, DockSide::bottom, "output"));
    advance(window, std::chrono::milliseconds(3000));
    require(!site.flyout_open(), "with click-only opening, hover opens nothing");
    click(window, tab_point(site, DockSide::bottom, "output"));
    require(site.flyout_open(), "a click still opens it");
}

// -- click, an outside press, focus loss and Escape ------------------------------------------------

void click_focus_and_escape() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    reduce_motion(window);
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(stack_of(workspace, "output")), DockSide::bottom, 150.0));
    window.perform_layout();
    require(workspace.activate_panel(DockPanelId("doc.second")), "the second document is active");
    const std::uint64_t generation = workspace.layout().generation();
    Trace& trace = (*fixture).trace;

    // A click opens it and moves focus in; the pointer leaving does not
    // close a focused flyout.
    trace.lines.clear();
    click(window, tab_point(site, DockSide::bottom, "output"));
    window.perform_layout();
    require(site.flyout_open() && focused_id(window) == "content.output.edit" &&
                workspace.active_panel() && (*workspace.active_panel()).value == "output",
            "a click opens the flyout with focus in the panel, which becomes active");
    require(std::find(trace.lines.begin(), trace.lines.end(), "open output pointer") != trace.lines.end(),
            "the open is reported");
    const Rect documents = site.parts().document_area.bounds;
    move_to(window, Point{documents.x + 30.0, documents.y + 30.0});
    advance(window, std::chrono::milliseconds(800));
    require(site.flyout_open(), "a focused flyout stays when the pointer leaves");

    // Escape closes it and focus returns to the document.
    trace.lines.clear();
    require(key(window, PhysicalKey::escape) && !site.flyout_open(), "Escape closes the flyout");
    require(focused_id(window) == "content.doc.second.edit", "focus returns to where it was");
    require(trace.lines.size() == 2U && trace.lines[0] == "focus content.doc.second.edit" &&
                trace.lines[1] == "close output escape",
            "focus returns, then the close is reported");

    // An outside press closes it and goes on to what was pressed.
    click(window, tab_point(site, DockSide::bottom, "output"));
    window.perform_layout();
    trace.lines.clear();
    const Control::Ptr first_edit = window.find("content.doc.second.edit");
    require(first_edit != nullptr, "the document's editor exists");
    const Point inside_document = centre((*first_edit).absolute_bounds());
    click(window, inside_document);
    require(!site.flyout_open() &&
                std::find(trace.lines.begin(), trace.lines.end(), "close output outside_press") != trace.lines.end() &&
                focused_id(window) == "content.doc.second.edit",
            "an outside press closes the flyout and reaches the document");

    // Focus leaving closes it.
    click(window, tab_point(site, DockSide::bottom, "output"));
    window.perform_layout();
    trace.lines.clear();
    require(window.request_focus(window.find("content.doc.second.edit")), "focus moves to the document");
    require(!site.flyout_open() &&
                std::find(trace.lines.begin(), trace.lines.end(), "close output focus_lost") != trace.lines.end(),
            "focus leaving closes the flyout");

    // Shift+Escape hides the panel; the flyout goes with the layout change.
    click(window, tab_point(site, DockSide::bottom, "output"));
    window.perform_layout();
    trace.lines.clear();
    require(key(window, PhysicalKey::escape, Modifier::shift) &&
                workspace.layout().locate(DockPanelId("output")).placement == DockPlacementKind::hidden &&
                !site.flyout_open(),
            "Shift+Escape in the flyout hides its panel");
    require(trace.lines.back() == "close output layout", "the flyout closes with the layout change");
    require(workspace.layout().generation() == generation + 1U, "only the hide edited the layout");
}

// -- swapping, groups, the grip and the keyboard ---------------------------------------------------

void group_grip_and_keyboard() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    reduce_motion(window);
    const DockNodeId left = stack_of(workspace, "classes");
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(left), DockSide::left, 200.0));
    window.perform_layout();
    const std::string before = workspace.layout().write_xml().text;
    const std::uint64_t generation = workspace.layout().generation();
    Trace& trace = (*fixture).trace;

    click(window, tab_point(site, DockSide::left, "solution"));
    window.perform_layout();
    require(site.flyout_open() && (*site.flyout_panel()).value == "solution" &&
                focused_id(window) == "content.solution.edit",
            "a group's tab shows that panel");
    require(std::abs(site.flyout_extent() - 200.0) < 1e-6, "at the auto-hide entry's extent");
    trace.lines.clear();
    click(window, tab_point(site, DockSide::left, "classes"));
    window.perform_layout();
    require(site.flyout_open() && (*site.flyout_panel()).value == "classes" &&
                focused_id(window) == "content.classes.edit",
            "another tab of the group swaps the flyout");
    // The order of the flyout's changes: the old one closes as swapped
    // before the new one opens.
    const std::vector<std::string>::iterator closed =
        std::find(trace.lines.begin(), trace.lines.end(), "close solution swapped");
    const std::vector<std::string>::iterator opened =
        std::find(trace.lines.begin(), trace.lines.end(), "open classes pointer");
    require(closed != trace.lines.end() && opened != trace.lines.end() && closed < opened,
            "the old flyout closes as swapped, then the new one opens");

    // The grip on the inner edge resizes it for the session.
    const Rect grip = to_window(site.absolute_bounds(), site.parts().flyout_grip.bounds);
    require(site.parts().flyout_grip.present && site.parts().flyout_shadow.present &&
                site.parts().flyout_face.present,
            "the flyout publishes its shadow, face and grip");
    const Point start = centre(grip);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}));
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary, Point{start.x + 40.0, start.y}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, Point{start.x + 40.0, start.y}}));
    window.perform_layout();
    require(std::abs(site.flyout_extent() - 240.0) < 1e-6, "dragging the grip outwards widens the flyout");
    require(site.resize_flyout(10000.0) && site.flyout_extent() <= site.absolute_bounds().width,
            "the extent stays within the tree's");
    require(site.resize_flyout(260.0) && std::abs(site.flyout_extent() - 260.0) < 1e-6, "resize_flyout sets it");
    require(workspace.layout().generation() == generation && workspace.layout().write_xml().text == before,
            "swapping, resizing and closing flyouts never edit the layout");

    // The keyboard: the strip is a focus region; Enter opens the focused
    // tab's flyout with focus inside.
    require(key(window, PhysicalKey::escape) && !site.flyout_open(), "Escape closes it");
    const std::shared_ptr<PageStrip> strip = site.side_strip(DockSide::left);
    require(window.request_focus(strip), "the side strip takes focus");
    require((*strip).focused_page().has_value(), "the keyboard is on a tab");
    const std::string focused_tab = (*(*strip).focused_page()).value;
    require(key(window, PhysicalKey::enter) && site.flyout_open() && (*site.flyout_panel()).value == focused_tab &&
                focused_id(window) == "content." + focused_tab + ".edit",
            "Enter on a side tab opens its flyout with focus inside");
    require(std::find(trace.lines.begin(), trace.lines.end(), "open " + focused_tab + " keyboard") != trace.lines.end(),
            "opened by the keyboard");
    require(std::abs(site.flyout_extent() - 260.0) < 1e-6, "the resized extent lasts for the session");

    // Activating an auto-hidden panel opens its flyout.
    require(key(window, PhysicalKey::escape) && workspace.activate_panel(DockPanelId("solution")) &&
                site.flyout_open() && (*site.flyout_panel()).value == "solution" &&
                focused_id(window) == "content.solution.edit",
            "activating an auto-hidden panel opens it in its flyout");
    require(workspace.layout().generation() == generation, "without editing the stack's selection");
}

// -- collapsing by priority into side tabs, and back -----------------------------------------------

void collapse_into_side_tabs() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    reduce_motion(window);
    const std::string before = workspace.layout().write_xml().text;
    const std::uint64_t generation = workspace.layout().generation();
    window.resize({340.0, 500.0});
    window.perform_layout();
    const std::vector<DockNodeId> collapsed = site.collapsed_stacks();
    require(collapsed.size() == 1U && collapsed[0] == stack_of(workspace, "properties"),
            "Properties collapses first");
    const std::vector<DockPanelId> right = site.side_panels(DockSide::right);
    require(right.size() == 1U && right[0].value == "properties", "and waits behind a side tab on the right");
    require(workspace.layout().generation() == generation &&
                workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::docked,
            "collapsing leaves the layout model unchanged");

    click(window, tab_point(site, DockSide::right, "properties"));
    window.perform_layout();
    require(site.flyout_open() && focused_id(window) == "content.properties.edit",
            "a collapsed stack opens in a flyout like an auto-hidden one");
    const std::vector<DockSite::Stack> items = site.realized_stack_items();
    const DockSite::Stack* const flyout_stack = find_stack(items, stack_of(workspace, "properties"));
    require(flyout_stack != nullptr && (*flyout_stack).parts.caption_pin.states.contains(AnatomyState::checked),
            "its pin is checked: the stack is still docked");
    require(key(window, PhysicalKey::escape) && !site.flyout_open(), "Escape closes it");
    require(workspace.activate_panel(DockPanelId("properties")) && site.flyout_open(),
            "activating a collapsed panel opens its flyout");

    window.resize({800.0, 500.0});
    window.perform_layout();
    advance(window, std::chrono::milliseconds(20));
    require(!site.flyout_open() && (*site.stack_view(stack_of(workspace, "properties"))).effectively_visible() &&
                site.side_strip(DockSide::right) == nullptr,
            "with room again the flyout closes and the stack returns to the tree");
    require((*fixture).trace.lines.back() == "close properties layout", "closed by the layout");
    require(workspace.layout().generation() == generation && workspace.layout().write_xml().text == before,
            "collapsing, the flyout and the return leave the layout as it was");

    // Pressing a collapsed stack's checked pin auto-hides it for good.
    window.resize({340.0, 500.0});
    window.perform_layout();
    click(window, tab_point(site, DockSide::right, "properties"));
    window.perform_layout();
    click(window, pin_point(site, stack_of(workspace, "properties")));
    window.perform_layout();
    require(workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::auto_hidden &&
                site.side_panels(DockSide::right).size() == 1U,
            "the collapsed stack's pin auto-hides it");
}

// -- the slide -------------------------------------------------------------------------------

void slide() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(stack_of(workspace, "output")), DockSide::bottom, 150.0));
    window.perform_layout();
    require(site.open_flyout(DockPanelId("output"), DockFlyoutCause::programmatic) && site.flyout_reveal() == 0.0,
            "the slide starts from under the strip");
    window.perform_layout();
    const double strip_top = (*site.side_strip(DockSide::bottom)).absolute_bounds().y;
    require(std::abs((*site.flyout_control()).absolute_bounds().y - strip_top) < 1e-6 &&
                (*site.flyout_control()).absolute_bounds().height < 1e-6,
            "nothing of it shows yet");
    advance(window, std::chrono::milliseconds(90));
    require(site.flyout_reveal() > 0.2 && site.flyout_reveal() < 0.9, "halfway it is half out");
    advance(window, std::chrono::milliseconds(400));
    require(site.flyout_reveal() == 1.0 && std::abs((*site.flyout_control()).absolute_bounds().height - 150.0) < 1e-6,
            "after the duration it is all out, at its extent");
    require(std::abs((*site.flyout_control()).absolute_bounds().bottom() - strip_top) < 1e-6,
            "its outer edge meets the strip");
    site.close_flyout();
    require(!site.flyout_open() && (*fixture).trace.lines.back() == "close output programmatic",
            "close_flyout closes it");
}

// The slide's frames on the window's motion clock (T2 E3, GIVEN D1-1),
// from a test time source: 180 ms, linear, so 12 frames of 16 ms, each
// showing exactly its share; then nothing is scheduled. In an occluded
// window (GIVEN D4-1) it is all out at once with no frames, and occlusion
// mid-slide ends it.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

void slide_frames() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    ManualTime time{};
    window.motion_clock().set_time_source(&time);
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(stack_of(workspace, "output")), DockSide::bottom, 150.0));
    window.perform_layout();
    const std::uint64_t requests = window.metrics_snapshot().scheduled_frame_requests;
    require(site.open_flyout(DockPanelId("output"), DockFlyoutCause::programmatic) && site.flyout_reveal() == 0.0,
            "the slide starts under the strip");
    std::size_t frames = 0U;
    for (int k = 1; k <= 40 && window.motion_clock().armed(); ++k) {
        time.time += std::chrono::milliseconds(16);
        const FramePollResult result = window.poll_frame_schedule(time.time);
        window.perform_layout();
        require(result.motion_clock_ticks == 1U && result.ui_timer_ticks == 0U, "each frame is the motion clock's");
        frames += result.motion_clock_ticks;
        const double share = std::min(1.0, 16.0 * k / 180.0);
        require(std::abs(site.flyout_reveal() - share) < 1e-12, "each frame shows exactly its share of the slide");
    }
    require(frames == 12U && site.flyout_reveal() == 1.0 &&
                std::abs((*site.flyout_control()).absolute_bounds().height - 150.0) < 1e-6,
            "the slide takes 12 frames and ends all out");
    require(window.metrics_snapshot().scheduled_frame_requests - requests == 1U && !window.motion_clock().armed(),
            "one frame request, disconnected at the end");
    time.time += std::chrono::milliseconds(16);
    require(window.poll_frame_schedule(time.time).motion_clock_ticks == 0U, "an open flyout at rest has no frames");
    site.close_flyout();

    window.set_occluded(true, time.time);
    require(site.open_flyout(DockPanelId("output"), DockFlyoutCause::programmatic) && site.flyout_reveal() == 1.0 &&
                !window.motion_clock().armed(),
            "in an occluded window the flyout is all out at once and nothing is scheduled");
    site.close_flyout();
    window.set_occluded(false, time.time);

    require(site.open_flyout(DockPanelId("output"), DockFlyoutCause::programmatic), "the flyout opens again");
    for (int k = 0; k < 3; ++k) {
        time.time += std::chrono::milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(time.time));
    }
    require(site.flyout_reveal() > 0.2 && site.flyout_reveal() < 0.3, "three frames in");
    window.set_occluded(true, time.time);
    window.perform_layout();
    require(site.flyout_reveal() == 1.0 && !window.motion_clock().armed() &&
                std::abs((*site.flyout_control()).absolute_bounds().height - 150.0) < 1e-6,
            "occlusion mid-slide puts it all out");
    site.close_flyout();
}

// -- semantics --------------------------------------------------------------------------------

void semantics() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    reduce_motion(window);
    const DockNodeId properties = stack_of(workspace, "properties");
    apply(workspace, DockEdit::auto_hide(DockSubject::of_stack(properties), DockSide::right, 220.0));
    window.perform_layout();
    SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* strip = find_node(snapshot.roots, "site.side.right");
    require(strip != nullptr && (*strip).role == SemanticRole::tab_group && (*strip).name == "Auto-hidden panels" &&
                (*strip).orientation == SemanticOrientation::vertical,
            "a right side strip is a vertical tab list");
    const SemanticNode* tab = find_node(snapshot.roots, (*site.side_strip(DockSide::right)).tab_identity(PageId{"properties"}));
    require(tab != nullptr && (*tab).role == SemanticRole::tab && (*tab).name == "Properties", "with one tab per panel");

    require(window.perform_semantic_action((*site.side_strip(DockSide::right)).tab_identity(PageId{"properties"}),
                                           SemanticAction::press) || site.flyout_open(),
            "the tab's press opens the flyout");
    require(site.flyout_open(), "the flyout is open");
    window.perform_layout();
    snapshot = window.semantic_snapshot();
    const std::string pane = site.stack_identity(properties);
    const SemanticNode* pane_node = find_node(snapshot.roots, pane);
    require(pane_node != nullptr && (*pane_node).role == SemanticRole::pane && (*pane_node).name == "Properties",
            "the flyout's stack is a pane named by its panel");
    const SemanticNode* pin = find_node((*pane_node).children, pane + ".pin");
    require(pin != nullptr && (*pin).role == SemanticRole::check_box &&
                !has_semantic_state((*pin).states, SemanticState::checked) &&
                has_semantic_state((*pin).states, SemanticState::enabled),
            "its pin is an unchecked, enabled check box");
    require(window.perform_semantic_action(pane + ".pin", SemanticAction::press) &&
                workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::docked,
            "pressing the pin docks the stack back");
}

} // namespace

int main() {
    pin_round_trip();
    hover_order();
    click_focus_and_escape();
    group_grip_and_keyboard();
    collapse_into_side_tabs();
    slide();
    slide_frames();
    semantics();
    std::cout << "DockSite auto-hide: pin round trip, hover, click, focus, Escape, outside press, grip, keyboard, "
                 "collapse into side tabs, slide, its frames and semantics passed\n";
    return 0;
}
