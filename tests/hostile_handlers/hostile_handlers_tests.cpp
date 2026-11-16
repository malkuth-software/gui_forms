// Hostile handlers (owner decision 2026-11-16, "Handlers: commit, then
// notify"; house style section 5): a component completes its state change
// before it raises a notification, so nothing it took from its state is
// carried across the call into application code. These tests attach a
// handler to each event a stock control raises while it changes state, and
// from inside the first notification the handler does one destructive
// thing: clears the items, replaces them with a different count, removes
// the control from its parent, removes it and drops the last strong
// reference (the harness then holds it only weakly), or destroys the
// window. After the input script the control and the window must be
// consistent: no crash, no throw out of dispatch, no sanitizer report (the
// suite runs in the sanitizer tree, tools/ci_build.py --sanitize), focus and
// capture only on attached controls, every index the control reports inside
// its model, and a layout, a paint and a semantic snapshot that succeed.
//
// The cases come first from the 2026-11-15 audit's use-after-handler and
// throw-mid-apply findings: ListView's press across request_focus
// (A-gf-controls-08-F1), ContextMenu's command destroying the menu
// (A-gf-controls-06-F1), CheckedListBox's check index across item_checking
// (A-gf-controls-08-F2), DataGridView's pressed row (A-gf-controls-02-F6),
// SplitContainer's keyboard step past the edge (A-gf-controls-12-F1) and the
// range controls' refusals from input (A-gf-controls-11-F8); then the other
// stock controls that raise events while they change.
//
// One run: subject x event x action, each on a fresh window. The handler
// acts on the event's first emission only; later emissions (including those
// its own action raises) only count. Events the script never raises are
// listed on stdout, so the coverage is visible. Usage:
//   gui_forms_hostile_handlers_tests [--subject NAME] [--actions mutate|release|all]
// mutate runs clear, replace, detach and destroy_window; release runs the
// last-reference drop alone (registered as its own CTest test, since a
// control destroyed inside its own notification is the hardest case).

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/checked_list_box.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_row_order.hpp"
#include "gui_forms/controls/data_grid_view_rows.hpp"
#include "gui_forms/controls/data_grid_view_types.hpp"
#include "gui_forms/controls/h_scroll_bar.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/list_box_values.hpp"
#include "gui_forms/controls/list_control.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/controls/list_view_values.hpp"
#include "gui_forms/controls/menu_strip.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/range_control_values.hpp"
#include "gui_forms/controls/split_container.hpp"
#include "gui_forms/controls/split_container_values.hpp"
#include "gui_forms/controls/tab_control.hpp"
#include "gui_forms/controls/tab_page.hpp"
#include "gui_forms/controls/track_bar.hpp"
#include "gui_forms/controls/tree_view.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

constexpr double window_width = 400.0;
constexpr double window_height = 300.0;

// What the handler does from inside the first notification.
enum class HostileAction : std::uint8_t {
    clear_items,
    replace_items,
    detach,
    release,
    destroy_window,
};

const char* action_name(HostileAction action) {
    switch (action) {
    case HostileAction::clear_items: return "clear_items";
    case HostileAction::replace_items: return "replace_items";
    case HostileAction::detach: return "detach";
    case HostileAction::release: return "release";
    case HostileAction::destroy_window: return "destroy_window";
    }
    return "unknown";
}

constexpr HostileAction mutating_actions[] = {
    HostileAction::clear_items,
    HostileAction::replace_items,
    HostileAction::detach,
    HostileAction::destroy_window,
};
constexpr HostileAction releasing_actions[] = {HostileAction::release};

struct Stage;
using StageStep = void (*)(Stage&);

// The failures of one run of the binary, each prefixed with its case.
struct Findings final {
    std::string context{};
    std::vector<std::string> failures{};

    void fail(std::string_view message) {
        std::string line = context;
        line += ": ";
        line += message;
        failures.push_back(std::move(line));
    }
};

using StageCheck = void (*)(Stage&, Findings&);

// The application's handler: a component that owns its subscriptions (on()),
// subscribed with no arguments to whichever event the case names. It acts
// on the first emission and only counts the rest.
class HostileHandler final : public Component {
public:
    void fire();

    Stage* stage{};
};

// One case's world. The handler is declared first so it outlives the
// window and the controls (members are destroyed in reverse order); the
// window goes before the controls, as an application's form does.
struct Stage final {
    HostileHandler handler{};
    HostileAction action{HostileAction::clear_items};
    std::size_t fired{};
    std::shared_ptr<Panel> root{};
    // The harness's strong reference to the subject (the ContextMenu case's
    // owner control); release drops it. The input script reaches the
    // subject only through `watched`, so release leaves nothing alive.
    std::shared_ptr<Control> subject{};
    std::weak_ptr<Control> watched{};
    std::unique_ptr<Window> window{};
    // The ContextMenu case only: the menu, which release destroys.
    std::shared_ptr<ContextMenu> menu{};
    // The subject's own state changes for clear_items and replace_items.
    StageStep clear_items{};
    StageStep replace_items{};
};

// Removes the subject from the root; with release, the harness's reference
// goes too, so the subject dies here unless its own code holds it.
void detach_subject(Stage& stage, bool release) {
    if (stage.subject == nullptr) return;
    const RuntimeId identity = (*stage.subject).runtime_id();
    Control::Ptr removed = (*stage.root).remove_child(identity);
    if (release) stage.subject.reset();
    removed.reset();
}

void perform(Stage& stage) {
    switch (stage.action) {
    case HostileAction::clear_items:
        if (stage.clear_items != nullptr) (*stage.clear_items)(stage);
        return;
    case HostileAction::replace_items:
        if (stage.replace_items != nullptr) (*stage.replace_items)(stage);
        return;
    case HostileAction::detach:
        detach_subject(stage, false);
        return;
    case HostileAction::release:
        // The ContextMenu case destroys the menu inside its own event
        // (A-gf-controls-06-F1); the others drop the control.
        if (stage.menu != nullptr) {
            stage.menu.reset();
            return;
        }
        detach_subject(stage, true);
        return;
    case HostileAction::destroy_window:
        stage.window.reset();
        return;
    }
    // An unknown action does nothing: the case then only checks consistency.
}

void HostileHandler::fire() {
    Stage& current = *stage;
    ++current.fired;
    if (current.fired != 1U) return;
    perform(current);
}

// The subject as its own type, or null once released. The pointer is the
// caller's to drop before it dispatches input, so that release still leaves
// the subject held only by its parent.
template <typename ControlType>
std::shared_ptr<ControlType> subject_as(Stage& stage) {
    const std::shared_ptr<Control> control = stage.watched.lock();
    std::shared_ptr<ControlType> typed = std::static_pointer_cast<ControlType>(control);
    return typed;
}

template <typename Owner, typename EventType>
using EventAccessor = EventType& (Owner::*)();

// Subscribes the handler to the subject's event `accessor`. Owner is the
// class that declares the accessor.
template <typename Owner, typename EventType, EventAccessor<Owner, EventType> accessor>
void attach_to_subject(Stage& stage) {
    const std::shared_ptr<Owner> owner = std::static_pointer_cast<Owner>(stage.subject);
    EventType& event = ((*owner).*accessor)();
    on(event, stage.handler, &HostileHandler::fire);
}

void attach_focus(Stage& stage) {
    attach_to_subject<Control, Event<bool>, &Control::focus_observed>(stage);
}

struct HostileEvent final {
    const char* name{};
    StageStep attach{};
};

struct Subject final {
    const char* name{};
    // Makes the subject, adds it to the root and names its state changes.
    StageStep build{};
    std::span<const HostileEvent> events{};
    // The input script; every step tolerates a destroyed window or subject.
    StageStep drive{};
    // The subject's own consistency, run while it is alive.
    StageCheck check{};
};

// --- Input that tolerates what the handler destroyed --------------------

Point centre_in_window(const Control& control, Rect local) {
    const Rect absolute = control.absolute_bounds();
    const Point point{absolute.x + local.x + local.width * 0.5, absolute.y + local.y + local.height * 0.5};
    return point;
}

void press_key(Stage& stage, std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    Window* const window = stage.window.get();
    if (window == nullptr) return;
    KeyEvent event{KeyAction::down, physical_key, modifiers};
    static_cast<void>((*window).dispatch_key(event));
}

void pointer(Stage& stage, PointerAction action, PointerButton button, Point position,
             std::uint32_t clicks = 1U) {
    Window* const window = stage.window.get();
    if (window == nullptr) return;
    PointerEvent event{action, button, position};
    event.click_count = clicks;
    event.pointer_id = 1U;
    static_cast<void>((*window).dispatch_pointer(event));
}

void click_at(Stage& stage, Point position, std::uint32_t clicks = 1U) {
    pointer(stage, PointerAction::down, PointerButton::primary, position, clicks);
    pointer(stage, PointerAction::up, PointerButton::primary, position, clicks);
}

// An assistive technology's press on the subject's virtual child named
// `suffix` after the subject's stable ID (empty: the ID alone).
void semantic_press(Stage& stage, std::string_view suffix) {
    Window* const window = stage.window.get();
    if (window == nullptr) return;
    std::string identity;
    {
        const std::shared_ptr<Control> control = stage.watched.lock();
        if (control == nullptr) return;
        identity = std::string((*control).stable_id().value());
    }
    identity += suffix;
    static_cast<void>((*window).perform_semantic_action(identity, SemanticAction::press));
}

void focus_subject(Stage& stage) {
    Window* const window = stage.window.get();
    if (window == nullptr) return;
    const std::shared_ptr<Control> control = stage.watched.lock();
    if (control == nullptr) return;
    static_cast<void>((*window).request_focus(control));
}

// --- Common consistency --------------------------------------------------

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// A control the window reports (focused, captured, hovered) must be
// attached to that window.
void check_reported(const Window& window, const Control::Ptr& control, const char* role, Findings& findings) {
    if (control == nullptr) return;
    const Window* const attached = (*control).attached_window();
    if (attached == &window) return;
    std::string message = "the window's ";
    message += role;
    message += " control is not attached to it";
    findings.fail(message);
}

void check_window(Stage& stage, Findings& findings) {
    Window* const window = stage.window.get();
    if (window == nullptr) return;
    check_reported(*window, (*window).focused_control(), "focused", findings);
    check_reported(*window, (*window).captured_control(), "captured", findings);
    check_reported(*window, (*window).hovered_control(), "hovered", findings);
    (*window).perform_layout();
    NullPainter painter;
    static_cast<void>((*window).paint(painter, Rect{0.0, 0.0, window_width, window_height}));
    const SemanticSnapshot snapshot = (*window).semantic_snapshot();
    static_cast<void>(snapshot);
}

// More input after the handler acted: the control must keep working on
// whatever state the handler left (no stale index used by the next event).
void drive_afterwards(Stage& stage) {
    focus_subject(stage);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::up);
    press_key(stage, PhysicalKey::home);
    press_key(stage, PhysicalKey::end);
    click_at(stage, Point{window_width * 0.5, window_height * 0.5});
    press_key(stage, PhysicalKey::escape);
}

void check_detached(Stage& stage, Findings& findings) {
    if (stage.action != HostileAction::detach || stage.fired == 0U) return;
    const std::shared_ptr<Control> control = stage.watched.lock();
    if (control == nullptr) {
        findings.fail("a detached subject the harness still holds was destroyed");
        return;
    }
    if ((*control).attached()) findings.fail("the detached subject is still attached");
}

// --- ListView (A-gf-controls-08-F1) ----------------------------------------

std::vector<ListViewColumn> list_view_columns() {
    std::vector<ListViewColumn> columns;
    columns.push_back({.stable_id = "name", .text = "Name", .width = 160.0});
    columns.push_back({.stable_id = "size", .text = "Size", .width = 80.0});
    return columns;
}

std::vector<ListViewItem> list_view_items(std::size_t count, const char* prefix) {
    std::vector<ListViewItem> items;
    items.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        ListViewItem item;
        item.stable_id = std::string(prefix) + "-" + std::to_string(index);
        item.text = std::string(prefix) + " " + std::to_string(index);
        item.sub_items = {std::to_string(index + 1U) + " KB"};
        items.push_back(std::move(item));
    }
    return items;
}

void list_view_clear(Stage& stage) {
    const std::shared_ptr<ListView> view = subject_as<ListView>(stage);
    if (view != nullptr) (*view).set_items({});
}

void list_view_replace(Stage& stage) {
    const std::shared_ptr<ListView> view = subject_as<ListView>(stage);
    if (view != nullptr) (*view).set_items(list_view_items(2U, "replaced"));
}

void build_list_view(Stage& stage) {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("hostile.list-view"));
    (*view).set_columns(list_view_columns());
    (*view).set_items(list_view_items(8U, "item"));
    (*view).check_boxes = true;
    (*view).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = view;
    stage.clear_items = &list_view_clear;
    stage.replace_items = &list_view_replace;
}

// Fills `point` with the centre of row `index`'s text (or check well) in
// window coordinates; false when the view is gone or the row is not shown.
bool list_view_row(Stage& stage, std::size_t index, bool check_well, Point& point) {
    const std::shared_ptr<ListView> view = subject_as<ListView>(stage);
    if (view == nullptr) return false;
    const std::vector<ListView::Item> items = (*view).realized_items();
    for (const ListView::Item& item : items) {
        if (item.index != index) continue;
        const PartGeometry& part = check_well ? item.parts.check_well : item.parts.text;
        if (!part.present) return false;
        point = centre_in_window(*view, part.bounds);
        return true;
    }
    return false;
}

bool list_view_column(Stage& stage, std::size_t index, Point& point) {
    const std::shared_ptr<ListView> view = subject_as<ListView>(stage);
    if (view == nullptr) return false;
    const std::vector<ListView::Column> columns = (*view).realized_columns();
    for (const ListView::Column& column : columns) {
        if (column.index != index || !column.parts.column_label.present) continue;
        point = centre_in_window(*view, column.parts.column_label.bounds);
        return true;
    }
    return false;
}

void drive_list_view(Stage& stage) {
    Point point{};
    // The first press also takes focus: the audit's press across request_focus.
    if (list_view_row(stage, 1U, false, point)) click_at(stage, point);
    if (list_view_row(stage, 0U, true, point)) click_at(stage, point);
    if (list_view_row(stage, 2U, false, point)) {
        click_at(stage, point);
        click_at(stage, point, 2U);
    }
    focus_subject(stage);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::space);
    press_key(stage, PhysicalKey::down, Modifier::shift);
    press_key(stage, PhysicalKey::enter);
    if (list_view_column(stage, 0U, point)) click_at(stage, point);
}

void check_list_view(Stage& stage, Findings& findings) {
    const std::shared_ptr<ListView> view = subject_as<ListView>(stage);
    if (view == nullptr) return;
    const std::size_t count = (*view).item_count();
    const std::vector<std::size_t> selected = (*view).selected_indices();
    if (selected.size() != (*view).selected_count()) findings.fail("ListView's selected count disagrees with its indices");
    for (const std::size_t index : selected) {
        if (index >= count) findings.fail("ListView selects an index past its items");
    }
    const stxlib::Optional<std::size_t> focused = (*view).focused_index();
    const std::size_t* const focused_index = focused.value();
    if (focused_index != nullptr && *focused_index >= count) findings.fail("ListView's focused index is past its items");
}

const HostileEvent list_view_events[] = {
    {"focus_observed", &attach_focus},
    {"selection_changed",
     &attach_to_subject<ListView, Event<const ListViewSelectionChange&>, &ListView::selection_changed>},
    {"item_activated",
     &attach_to_subject<ListView, Event<const ListViewItemActivation&>, &ListView::item_activated>},
    {"item_checking", &attach_to_subject<ListView, Event<ListViewItemCheck&>, &ListView::item_checking>},
    {"item_checked", &attach_to_subject<ListView, Event<const ListViewItemCheck&>, &ListView::item_checked>},
    {"column_click", &attach_to_subject<ListView, Event<const ListViewColumnClick&>, &ListView::column_click>},
};

// --- ListBox and CheckedListBox (A-gf-controls-08-F2) ----------------------

std::vector<std::string> replacement_texts() {
    std::vector<std::string> texts;
    texts.push_back("Replaced");
    return texts;
}

void list_box_clear(Stage& stage) {
    const std::shared_ptr<ListBox> list = subject_as<ListBox>(stage);
    if (list != nullptr) (*list).items.clear();
}

void list_box_replace(Stage& stage) {
    const std::shared_ptr<ListBox> list = subject_as<ListBox>(stage);
    if (list != nullptr) (*list).items = replacement_texts();
}

std::vector<std::string> list_box_texts() {
    std::vector<std::string> texts;
    for (std::size_t index = 0U; index < 8U; ++index) texts.push_back("Row " + std::to_string(index));
    return texts;
}

void build_list_box(Stage& stage) {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("hostile.list-box"));
    (*list).items = list_box_texts();
    (*list).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = list;
    stage.clear_items = &list_box_clear;
    stage.replace_items = &list_box_replace;
}

void build_checked_list_box(Stage& stage) {
    std::shared_ptr<CheckedListBox> list = make_control<CheckedListBox>(StableId("hostile.checked-list-box"));
    (*list).add_item("Renderer", CheckState::checked);
    (*list).add_item("Host", CheckState::unchecked);
    (*list).add_item("Accessibility", CheckState::unchecked);
    (*list).add_item("Telemetry", CheckState::indeterminate);
    (*list).add_item("Fonts", CheckState::unchecked);
    (*list).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = list;
    stage.clear_items = &list_box_clear;
    stage.replace_items = &list_box_replace;
}

bool list_box_row(Stage& stage, std::size_t index, Point& point) {
    const std::shared_ptr<ListBox> list = subject_as<ListBox>(stage);
    if (list == nullptr) return false;
    const std::vector<ListBox::Item> items = (*list).realized_items();
    for (const ListBox::Item& item : items) {
        if (item.index != index || !item.parts.row.present) continue;
        point = centre_in_window(*list, item.parts.row.bounds);
        return true;
    }
    return false;
}

void drive_list_box(Stage& stage) {
    // First, while unfocused: the press focuses the list, then selects the
    // row it named before the focus handlers ran.
    semantic_press(stage, ".item.2");
    Point point{};
    if (list_box_row(stage, 1U, point)) click_at(stage, point);
    // A second press on the selected row toggles a CheckedListBox's check.
    if (list_box_row(stage, 1U, point)) click_at(stage, point);
    if (list_box_row(stage, 0U, point)) click_at(stage, point, 2U);
    focus_subject(stage);
    press_key(stage, PhysicalKey::space);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::space);
    press_key(stage, PhysicalKey::down, Modifier::shift);
    press_key(stage, PhysicalKey::enter);
}

void check_list_box(Stage& stage, Findings& findings) {
    const std::shared_ptr<ListBox> list = subject_as<ListBox>(stage);
    if (list == nullptr) return;
    const std::size_t count = (*list).items.read().size();
    const stxlib::Optional<std::size_t> selected = (*list).selected_index();
    const std::size_t* const selected_index = selected.value();
    if (selected_index != nullptr && *selected_index >= count) findings.fail("ListBox's selected index is past its items");
    const ListBox& view = *list;
    for (const std::size_t index : view.selected_indices()) {
        if (index >= count) findings.fail("ListBox selects an index past its items");
    }
}

void check_checked_list_box(Stage& stage, Findings& findings) {
    check_list_box(stage, findings);
    const std::shared_ptr<CheckedListBox> list = subject_as<CheckedListBox>(stage);
    if (list == nullptr) return;
    const std::size_t count = (*list).items.read().size();
    const std::vector<std::size_t> checked = (*list).checked_indices();
    for (const std::size_t index : checked) {
        if (index >= count) findings.fail("CheckedListBox checks an index past its items");
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const CheckState state = (*list).item_check_state(index);
        if (state != CheckState::unchecked && state != CheckState::checked && state != CheckState::indeterminate) {
            findings.fail("CheckedListBox holds an invalid check state");
        }
    }
}

const HostileEvent list_box_events[] = {
    {"focus_observed", &attach_focus},
    {"selection_changed", &attach_to_subject<ListBox, Event<const ListSelectionChange&>, &ListBox::selection_changed>},
    {"item_activated", &attach_to_subject<ListBox, Event<std::size_t>, &ListBox::item_activated>},
    {"selected_index_changed",
     &attach_to_subject<ListControl, Event<stxlib::Optional<std::size_t>>, &ListControl::selected_index_changed>},
};

const HostileEvent checked_list_box_events[] = {
    {"focus_observed", &attach_focus},
    {"item_checking", &attach_to_subject<CheckedListBox, Event<ItemCheckEvent&>, &CheckedListBox::item_checking>},
    {"item_check_state_changed",
     &attach_to_subject<CheckedListBox, Event<std::size_t, CheckState>, &CheckedListBox::item_check_state_changed>},
    {"selection_changed", &attach_to_subject<ListBox, Event<const ListSelectionChange&>, &ListBox::selection_changed>},
    {"item_activated", &attach_to_subject<ListBox, Event<std::size_t>, &ListBox::item_activated>},
};

// --- ComboBox ----------------------------------------------------------------

void combo_box_clear(Stage& stage) {
    const std::shared_ptr<ComboBox> combo = subject_as<ComboBox>(stage);
    if (combo != nullptr) (*combo).items.clear();
}

void combo_box_replace(Stage& stage) {
    const std::shared_ptr<ComboBox> combo = subject_as<ComboBox>(stage);
    if (combo != nullptr) (*combo).items = replacement_texts();
}

void build_combo_box(Stage& stage) {
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("hostile.combo-box"));
    (*combo).items = list_box_texts();
    (*combo).set_requested_bounds({0.0, 0.0, 240.0, 28.0});
    stage.subject = combo;
    stage.clear_items = &combo_box_clear;
    stage.replace_items = &combo_box_replace;
}

void drive_combo_box(Stage& stage) {
    focus_subject(stage);
    press_key(stage, PhysicalKey::f4);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::down, Modifier::alt);
    press_key(stage, PhysicalKey::up);
    press_key(stage, PhysicalKey::escape);
    click_at(stage, Point{228.0, 14.0});
    click_at(stage, Point{120.0, 60.0});
}

void check_combo_box(Stage& stage, Findings& findings) {
    const std::shared_ptr<ComboBox> combo = subject_as<ComboBox>(stage);
    if (combo == nullptr) return;
    const std::size_t count = (*combo).items.read().size();
    const stxlib::Optional<std::size_t> selected = (*combo).selected_index();
    const std::size_t* const selected_index = selected.value();
    if (selected_index != nullptr && *selected_index >= count) findings.fail("ComboBox's selected index is past its items");
    const ComboBox& view = *combo;
    for (const std::size_t index : view.drop_down_items()) {
        if (index >= count) findings.fail("ComboBox's drop-down lists an index past its items");
    }
}

const HostileEvent combo_box_events[] = {
    {"focus_observed", &attach_focus},
    {"drop_down_changed", &attach_to_subject<ComboBox, Event<bool>, &ComboBox::drop_down_changed>},
    {"text_changed", &attach_to_subject<ComboBox, Event<const std::string&>, &ComboBox::text_changed>},
    {"selected_index_changed",
     &attach_to_subject<ListControl, Event<stxlib::Optional<std::size_t>>, &ListControl::selected_index_changed>},
};

// --- TreeView ------------------------------------------------------------------

TreeViewItem tree_item(const char* stable_id, const char* text, std::size_t depth, bool expandable) {
    TreeViewItem item;
    item.stable_id = stable_id;
    item.text = text;
    item.depth = depth;
    item.expandable = expandable;
    return item;
}

void tree_view_clear(Stage& stage) {
    const std::shared_ptr<TreeView> tree = subject_as<TreeView>(stage);
    if (tree != nullptr) (*tree).set_items({});
}

void tree_view_replace(Stage& stage) {
    const std::shared_ptr<TreeView> tree = subject_as<TreeView>(stage);
    if (tree == nullptr) return;
    std::vector<TreeViewItem> items;
    items.push_back(tree_item("replaced", "Replaced", 0U, false));
    (*tree).set_items(std::move(items));
}

void build_tree_view(Stage& stage) {
    std::shared_ptr<TreeView> tree = make_control<TreeView>(StableId("hostile.tree-view"));
    std::vector<TreeViewItem> items;
    items.push_back(tree_item("root", "Root", 0U, true));
    items.push_back(tree_item("root.a", "A", 1U, true));
    items.push_back(tree_item("root.a.leaf", "Leaf", 2U, false));
    items.push_back(tree_item("root.b", "B", 1U, false));
    items.push_back(tree_item("second", "Second", 0U, false));
    (*tree).set_items(std::move(items));
    (*tree).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = tree;
    stage.clear_items = &tree_view_clear;
    stage.replace_items = &tree_view_replace;
}

void drive_tree_view(Stage& stage) {
    focus_subject(stage);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::up);
    press_key(stage, PhysicalKey::right);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::right);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    press_key(stage, PhysicalKey::left);
    press_key(stage, PhysicalKey::left);
    press_key(stage, PhysicalKey::end);
    click_at(stage, Point{40.0, 10.0});
    click_at(stage, Point{40.0, 10.0}, 2U);
}

void check_tree_view(Stage& stage, Findings& findings) {
    const std::shared_ptr<TreeView> tree = subject_as<TreeView>(stage);
    if (tree == nullptr) return;
    const TreeView& view = *tree;
    const std::string_view active = view.active_id();
    if (active.empty()) return;
    bool found = false;
    for (const TreeViewItem& item : view.items()) {
        if (item.stable_id == active) found = true;
    }
    if (!found) findings.fail("TreeView's active item is not among its items");
}

const HostileEvent tree_view_events[] = {
    {"focus_observed", &attach_focus},
    {"selection_changed", &attach_to_subject<TreeView, Event<const TreeSelectionChange&>, &TreeView::selection_changed>},
    {"expansion_changed", &attach_to_subject<TreeView, Event<const TreeExpansionChange&>, &TreeView::expansion_changed>},
    {"item_activated", &attach_to_subject<TreeView, Event<const std::string&>, &TreeView::item_activated>},
};

// --- TabControl ------------------------------------------------------------------

void tab_control_remove_pages(TabControl& tabs) {
    while (tabs.page_count() > 0U) {
        const std::shared_ptr<TabPage> page = tabs.page_at(0U);
        if (page == nullptr) return;
        const std::shared_ptr<TabPage> removed = tabs.remove_page(*page);
        if (removed == nullptr) return;
    }
}

void tab_control_clear(Stage& stage) {
    const std::shared_ptr<TabControl> tabs = subject_as<TabControl>(stage);
    if (tabs != nullptr) tab_control_remove_pages(*tabs);
}

void tab_control_replace(Stage& stage) {
    const std::shared_ptr<TabControl> tabs = subject_as<TabControl>(stage);
    if (tabs == nullptr) return;
    tab_control_remove_pages(*tabs);
    (*tabs).add_page(make_control<TabPage>(StableId("hostile.tabs.replaced"), "Replaced"));
}

void build_tab_control(Stage& stage) {
    std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId("hostile.tabs"));
    (*tabs).add_page(make_control<TabPage>(StableId("hostile.tabs.general"), "General"));
    (*tabs).add_page(make_control<TabPage>(StableId("hostile.tabs.advanced"), "Advanced"));
    (*tabs).add_page(make_control<TabPage>(StableId("hostile.tabs.about"), "About"));
    (*tabs).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = tabs;
    stage.clear_items = &tab_control_clear;
    stage.replace_items = &tab_control_replace;
}

bool tab_header(Stage& stage, std::size_t index, Point& point) {
    const std::shared_ptr<TabControl> tabs = subject_as<TabControl>(stage);
    if (tabs == nullptr || index >= (*tabs).page_count()) return false;
    const Rect header = (*tabs).tab_bounds(index);
    point = Point{header.x + header.width * 0.5, header.y + header.height * 0.5};
    return true;
}

void drive_tab_control(Stage& stage) {
    // First, while unfocused: the press focuses the control, then selects
    // the page it found before the focus handlers ran.
    semantic_press(stage, ".advanced.tab");
    Point point{};
    if (tab_header(stage, 1U, point)) click_at(stage, point);
    focus_subject(stage);
    press_key(stage, PhysicalKey::tab, Modifier::control);
    press_key(stage, PhysicalKey::left);
    press_key(stage, PhysicalKey::home);
    press_key(stage, PhysicalKey::tab, Modifier::control | Modifier::shift);
    if (tab_header(stage, 2U, point)) click_at(stage, point);
}

void check_tab_control(Stage& stage, Findings& findings) {
    const std::shared_ptr<TabControl> tabs = subject_as<TabControl>(stage);
    if (tabs == nullptr) return;
    const stxlib::Optional<std::size_t> selected = (*tabs).selected_index.read();
    const std::size_t* const selected_index = selected.value();
    if (selected_index != nullptr && *selected_index >= (*tabs).page_count()) {
        findings.fail("TabControl's selected index is past its pages");
    }
}

const HostileEvent tab_control_events[] = {
    {"focus_observed", &attach_focus},
    {"deselecting", &attach_to_subject<TabControl, Event<TabControlCancelEvent&>, &TabControl::deselecting>},
    {"selecting", &attach_to_subject<TabControl, Event<TabControlCancelEvent&>, &TabControl::selecting>},
    {"deselected", &attach_to_subject<TabControl, Event<const TabControlEvent&>, &TabControl::deselected>},
    {"selected", &attach_to_subject<TabControl, Event<const TabControlEvent&>, &TabControl::selected>},
    {"selected_index_changed",
     &attach_to_subject<TabControl, Event<const TabSelectionChange&>, &TabControl::selected_index_changed>},
};

// --- SplitContainer (A-gf-controls-12-F1) ------------------------------------
// It has no items: clear collapses the first panel, replace turns the seam.

void split_container_clear(Stage& stage) {
    const std::shared_ptr<SplitContainer> split = subject_as<SplitContainer>(stage);
    if (split != nullptr) (*split).set_first_collapsed(true);
}

void split_container_replace(Stage& stage) {
    const std::shared_ptr<SplitContainer> split = subject_as<SplitContainer>(stage);
    if (split != nullptr) (*split).orientation = Orientation::horizontal;
}

void build_split_container(Stage& stage) {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("hostile.split"));
    (*split).set_requested_bounds({0.0, 0.0, window_width, window_height});
    (*split).splitter_distance = 120.0;
    stage.subject = split;
    stage.clear_items = &split_container_clear;
    stage.replace_items = &split_container_replace;
}

void drive_split_container(Stage& stage) {
    // A seam drag, then keyboard steps past the near edge (Shift+Left).
    pointer(stage, PointerAction::down, PointerButton::primary, Point{121.0, 70.0});
    pointer(stage, PointerAction::move, PointerButton::none, Point{181.0, 70.0});
    pointer(stage, PointerAction::move, PointerButton::none, Point{-50.0, 70.0});
    pointer(stage, PointerAction::up, PointerButton::primary, Point{-50.0, 70.0});
    for (std::size_t step = 0U; step < 12U; ++step) press_key(stage, PhysicalKey::left, Modifier::shift);
    press_key(stage, PhysicalKey::right);
    for (std::size_t step = 0U; step < 40U; ++step) press_key(stage, PhysicalKey::right, Modifier::shift);
}

void check_split_container(Stage& stage, Findings& findings) {
    const std::shared_ptr<SplitContainer> split = subject_as<SplitContainer>(stage);
    if (split == nullptr) return;
    const double distance = (*split).splitter_distance.read();
    if (!std::isfinite(distance) || distance < 0.0) findings.fail("SplitContainer's distance is negative or not finite");
}

const HostileEvent split_container_events[] = {
    {"focus_observed", &attach_focus},
    {"splitter_changed", &attach_to_subject<SplitContainer, Event<const SplitChangeEvent&>, &SplitContainer::splitter_changed>},
};

// --- Range controls: TrackBar, HScrollBar (A-gf-controls-11-F8) -------------
// clear narrows the range to one point; replace moves it past the value.

void range_clear(Stage& stage) {
    const std::shared_ptr<RangeControl> range = subject_as<RangeControl>(stage);
    if (range != nullptr) (*range).set_range(0.0, 0.0);
}

void range_replace(Stage& stage) {
    const std::shared_ptr<RangeControl> range = subject_as<RangeControl>(stage);
    if (range != nullptr) (*range).set_range(20.0, 30.0);
}

void build_track_bar(Stage& stage) {
    std::shared_ptr<TrackBar> track = make_control<TrackBar>(StableId("hostile.track-bar"));
    (*track).set_range(0.0, 10.0);
    (*track).value = 5.0;
    (*track).set_requested_bounds({0.0, 0.0, 300.0, 40.0});
    stage.subject = track;
    stage.clear_items = &range_clear;
    stage.replace_items = &range_replace;
}

void build_h_scroll_bar(Stage& stage) {
    std::shared_ptr<HScrollBar> bar = make_control<HScrollBar>(StableId("hostile.h-scroll-bar"));
    (*bar).set_range(0.0, 10.0);
    (*bar).value = 5.0;
    (*bar).set_requested_bounds({0.0, 0.0, 300.0, 18.0});
    stage.subject = bar;
    stage.clear_items = &range_clear;
    stage.replace_items = &range_replace;
}

void drive_range(Stage& stage) {
    focus_subject(stage);
    press_key(stage, PhysicalKey::right);
    press_key(stage, PhysicalKey::page_up);
    press_key(stage, PhysicalKey::end);
    press_key(stage, PhysicalKey::home);
    press_key(stage, PhysicalKey::left);
    press_key(stage, PhysicalKey::page_down);
    // The arrows or ends, the track, and a drag from the middle off the far end.
    click_at(stage, Point{5.0, 9.0});
    click_at(stage, Point{295.0, 9.0});
    click_at(stage, Point{80.0, 9.0});
    pointer(stage, PointerAction::down, PointerButton::primary, Point{150.0, 9.0});
    pointer(stage, PointerAction::move, PointerButton::none, Point{400.0, 9.0});
    pointer(stage, PointerAction::up, PointerButton::primary, Point{400.0, 9.0});
    PointerEvent wheel{PointerAction::wheel, PointerButton::none, Point{150.0, 9.0}};
    wheel.wheel_delta = Point{0.0, 3.0};
    Window* const window = stage.window.get();
    if (window != nullptr) static_cast<void>((*window).dispatch_pointer(wheel));
}

void check_range(Stage& stage, Findings& findings) {
    const std::shared_ptr<RangeControl> range = subject_as<RangeControl>(stage);
    if (range == nullptr) return;
    const double minimum = (*range).minimum.read();
    const double maximum = (*range).maximum.read();
    const double value = (*range).value.read();
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        findings.fail("the range control's value is outside its range");
    }
}

const HostileEvent range_events[] = {
    {"focus_observed", &attach_focus},
    {"value_changed", &attach_to_subject<RangeControl, Event<double>, &RangeControl::value_changed>},
    {"scroll", &attach_to_subject<RangeControl, Event<const RangeScrollEvent&>, &RangeControl::scroll>},
};

// --- NumericUpDown ---------------------------------------------------------------

void numeric_clear(Stage& stage) {
    const std::shared_ptr<NumericUpDown> spin = subject_as<NumericUpDown>(stage);
    if (spin != nullptr) (*spin).set_range(0.0, 0.0);
}

void numeric_replace(Stage& stage) {
    const std::shared_ptr<NumericUpDown> spin = subject_as<NumericUpDown>(stage);
    if (spin != nullptr) (*spin).set_range(20.0, 30.0);
}

void build_numeric_up_down(Stage& stage) {
    std::shared_ptr<NumericUpDown> spin = make_control<NumericUpDown>(StableId("hostile.numeric-up-down"));
    (*spin).set_range(0.0, 10.0);
    (*spin).value = 5.0;
    (*spin).set_requested_bounds({0.0, 0.0, 160.0, 28.0});
    stage.subject = spin;
    stage.clear_items = &numeric_clear;
    stage.replace_items = &numeric_replace;
}

void drive_numeric_up_down(Stage& stage) {
    focus_subject(stage);
    press_key(stage, PhysicalKey::up);
    press_key(stage, PhysicalKey::up);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::page_up);
    // The spin halves at the right end.
    click_at(stage, Point{152.0, 7.0});
    click_at(stage, Point{152.0, 21.0});
}

void check_numeric_up_down(Stage& stage, Findings& findings) {
    const std::shared_ptr<NumericUpDown> spin = subject_as<NumericUpDown>(stage);
    if (spin == nullptr) return;
    const double minimum = (*spin).minimum.read();
    const double maximum = (*spin).maximum.read();
    const double value = (*spin).value.read();
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        findings.fail("NumericUpDown's value is outside its range");
    }
}

const HostileEvent numeric_up_down_events[] = {
    {"focus_observed", &attach_focus},
    {"value_changed", &attach_to_subject<NumericUpDown, Event<double>, &NumericUpDown::value_changed>},
};

// --- DataGridView (A-gf-controls-02-F6) --------------------------------------

std::vector<DataGridViewColumn> grid_columns() {
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "name", .header_text = "Name", .width = 140.0});
    columns.push_back({.name = "active", .header_text = "On", .kind = DataGridViewColumnKind::check_box,
                       .width = 50.0});
    return columns;
}

DataGridViewRow grid_row(std::string identity, std::string text, bool active) {
    DataGridViewRow row;
    row.stable_id = DataGridViewRowId{std::move(identity)};
    row.cells.push_back(BindingValue{std::move(text)});
    row.cells.push_back(BindingValue{active});
    return row;
}

void grid_clear(Stage& stage) {
    const std::shared_ptr<DataGridView> grid = subject_as<DataGridView>(stage);
    if (grid != nullptr) (*grid).clear_rows();
}

void grid_replace(Stage& stage) {
    const std::shared_ptr<DataGridView> grid = subject_as<DataGridView>(stage);
    if (grid == nullptr) return;
    (*grid).clear_rows();
    (*grid).rows.append(grid_row("replaced", "Replaced", false));
}

void build_data_grid_view(Stage& stage) {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("hostile.grid"));
    (*grid).set_columns(grid_columns());
    for (std::size_t index = 0U; index < 6U; ++index) {
        (*grid).rows.append(grid_row("row-" + std::to_string(index), "Row " + std::to_string(index), index % 2U == 0U));
    }
    (*grid).allow_user_to_add_rows = false;
    (*grid).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = grid;
    stage.clear_items = &grid_clear;
    stage.replace_items = &grid_replace;
}

bool grid_cell(Stage& stage, std::size_t row, std::size_t column, Point& point) {
    const std::shared_ptr<DataGridView> grid = subject_as<DataGridView>(stage);
    if (grid == nullptr) return false;
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    for (const DataGridView::Cell& cell : cells) {
        if (cell.address.row.value != row || cell.address.column != column || !cell.parts.cell.present) continue;
        point = centre_in_window(*grid, cell.parts.cell.bounds);
        return true;
    }
    return false;
}

void drive_data_grid_view(Stage& stage) {
    Point point{};
    if (grid_cell(stage, 1U, 0U, point)) click_at(stage, point);
    if (grid_cell(stage, 3U, 1U, point)) click_at(stage, point);
    if (grid_cell(stage, 2U, 0U, point)) click_at(stage, point, 2U);
    focus_subject(stage);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::tab);
    press_key(stage, PhysicalKey::f2);
    press_key(stage, PhysicalKey::escape);
    press_key(stage, PhysicalKey::down, Modifier::shift);
    press_key(stage, PhysicalKey::home, Modifier::control);
    press_key(stage, PhysicalKey::end, Modifier::control);
}

void check_data_grid_view(Stage& stage, Findings& findings) {
    const std::shared_ptr<DataGridView> grid = subject_as<DataGridView>(stage);
    if (grid == nullptr) return;
    const std::size_t rows = (*grid).row_count();
    const std::size_t columns = (*grid).columns().size();
    const stxlib::Optional<DataGridViewCellAddress> current = (*grid).current_cell();
    const DataGridViewCellAddress* const address = current.value();
    if (address != nullptr && ((*address).row.value >= rows || (*address).column >= columns)) {
        findings.fail("DataGridView's current cell is outside its rows or columns");
    }
    const std::vector<DataGridViewRowPosition> selected_rows = (*grid).selected_rows();
    for (const DataGridViewRowPosition position : selected_rows) {
        if (position.value >= rows) findings.fail("DataGridView selects a row past its rows");
    }
    const std::vector<DataGridViewCellAddress> selected_cells = (*grid).selected_cells();
    for (const DataGridViewCellAddress& cell : selected_cells) {
        if (cell.row.value >= rows || cell.column >= columns) findings.fail("DataGridView selects a cell outside its grid");
    }
}

const HostileEvent data_grid_view_events[] = {
    {"focus_observed", &attach_focus},
    {"selection_changed", &attach_to_subject<DataGridView, Event<>, &DataGridView::selection_changed>},
    {"cell_mouse_down",
     &attach_to_subject<DataGridView, Event<const DataGridViewCellMouseEvent&>, &DataGridView::cell_mouse_down>},
    {"cell_click", &attach_to_subject<DataGridView, Event<const DataGridViewCellEvent&>, &DataGridView::cell_click>},
    {"cell_enter", &attach_to_subject<DataGridView, Event<const DataGridViewCellEvent&>, &DataGridView::cell_enter>},
    {"cell_leave", &attach_to_subject<DataGridView, Event<const DataGridViewCellEvent&>, &DataGridView::cell_leave>},
    {"row_enter", &attach_to_subject<DataGridView, Event<const DataGridViewRowEvent&>, &DataGridView::row_enter>},
    {"row_leave", &attach_to_subject<DataGridView, Event<const DataGridViewRowEvent&>, &DataGridView::row_leave>},
    {"cell_value_changed",
     &attach_to_subject<DataGridView, Event<const DataGridViewCellEvent&>, &DataGridView::cell_value_changed>},
    {"cell_begin_edit",
     &attach_to_subject<DataGridView, Event<DataGridViewCellCancelEvent&>, &DataGridView::cell_begin_edit>},
};

// --- MenuStrip -----------------------------------------------------------------

std::vector<MenuStripItemSpec> menu_strip_items() {
    std::vector<MenuStripItemSpec> items;
    items.push_back({.stable_id = "hostile.file", .text = "File",
                     .items = {{"file.open", MenuItemKind::command, {}, "Open"},
                               {"file.close", MenuItemKind::command, {}, "Close"}}});
    items.push_back({.stable_id = "hostile.view", .text = "View",
                     .items = {{"view.icons", MenuItemKind::command, {}, "Icons"},
                               {"view.details", MenuItemKind::command, {}, "Details"}}});
    return items;
}

void menu_strip_clear(Stage& stage) {
    const std::shared_ptr<MenuStrip> strip = subject_as<MenuStrip>(stage);
    if (strip != nullptr) (*strip).set_items({});
}

void menu_strip_replace(Stage& stage) {
    const std::shared_ptr<MenuStrip> strip = subject_as<MenuStrip>(stage);
    if (strip == nullptr) return;
    std::vector<MenuStripItemSpec> items;
    items.push_back({.stable_id = "hostile.replaced", .text = "Replaced",
                     .items = {{"replaced.only", MenuItemKind::command, {}, "Only"}}});
    (*strip).set_items(std::move(items));
}

void build_menu_strip(Stage& stage) {
    std::shared_ptr<MenuStrip> strip = make_control<MenuStrip>(StableId("hostile.menu-strip"));
    (*strip).set_items(menu_strip_items());
    (*strip).set_requested_bounds({0.0, 0.0, window_width, 24.0});
    stage.subject = strip;
    stage.clear_items = &menu_strip_clear;
    stage.replace_items = &menu_strip_replace;
}

void open_menu_strip(Stage& stage, std::size_t index) {
    const std::shared_ptr<MenuStrip> strip = subject_as<MenuStrip>(stage);
    if (strip == nullptr || stage.window == nullptr) return;
    static_cast<void>((*strip).open(index, MenuOpenMode::keyboard));
}

void drive_menu_strip(Stage& stage) {
    // Not a suffix: a menu's semantic ID is its item's own (hostile.view).
    Window* const window = stage.window.get();
    if (window != nullptr) static_cast<void>((*window).perform_semantic_action("hostile.view", SemanticAction::press));
    click_at(stage, Point{12.0, 12.0});
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    open_menu_strip(stage, 1U);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::right);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    open_menu_strip(stage, 0U);
    press_key(stage, PhysicalKey::escape);
}

void check_menu_strip(Stage& stage, Findings& findings) {
    const std::shared_ptr<MenuStrip> strip = subject_as<MenuStrip>(stage);
    if (strip == nullptr) return;
    const MenuStrip& view = *strip;
    const stxlib::Optional<std::size_t> active = view.active_index();
    const std::size_t* const active_index = active.value();
    if (active_index != nullptr && *active_index >= view.items().size()) {
        findings.fail("MenuStrip's active index is past its items");
    }
    if (view.items().empty() && view.is_open()) findings.fail("MenuStrip is open with no items");
}

const HostileEvent menu_strip_events[] = {
    {"focus_observed", &attach_focus},
    {"item_invoked", &attach_to_subject<MenuStrip, Event<const MenuStripInvocation&>, &MenuStrip::item_invoked>},
    {"open_changed",
     &attach_to_subject<MenuStrip, Event<stxlib::Optional<std::size_t>>, &MenuStrip::open_changed>},
};

// --- ContextMenu (A-gf-controls-06-F1) -----------------------------------------
// The subject is the owner control; the menu is the stage's. release
// destroys the menu from inside its own event.

std::vector<MenuItemSpec> context_items() {
    std::vector<MenuItemSpec> items;
    items.push_back({"open", MenuItemKind::command, {}, "Open"});
    items.push_back({"separator", MenuItemKind::separator});
    items.push_back({"copy", MenuItemKind::command, {}, "Copy"});
    items.push_back({"details", MenuItemKind::check, {}, "Details"});
    return items;
}

void context_clear(Stage& stage) {
    if (stage.menu != nullptr) (*stage.menu).set_items({});
}

void context_replace(Stage& stage) {
    if (stage.menu == nullptr) return;
    std::vector<MenuItemSpec> items;
    items.push_back({"replaced", MenuItemKind::command, {}, "Replaced"});
    (*stage.menu).set_items(std::move(items));
}

void build_context_menu(Stage& stage) {
    std::shared_ptr<Panel> owner = make_control<Panel>(StableId("hostile.context-owner"));
    (*owner).focusable = true;
    (*owner).set_requested_bounds({0.0, 0.0, window_width, window_height});
    stage.subject = owner;
    stage.menu = std::make_shared<ContextMenu>("hostile.context");
    (*stage.menu).set_items(context_items());
    stage.clear_items = &context_clear;
    stage.replace_items = &context_replace;
}

void attach_context_invoked(Stage& stage) {
    on((*stage.menu).item_invoked(), stage.handler, &HostileHandler::fire);
}

void attach_context_open(Stage& stage) {
    on((*stage.menu).open_changed(), stage.handler, &HostileHandler::fire);
}

// Opens the menu over its owner; the strong references end with the call,
// so the keys that follow reach the menu only through the window.
void show_context(Stage& stage, Point position, MenuOpenMode mode) {
    const std::shared_ptr<ContextMenu> menu = stage.menu;
    const std::shared_ptr<Control> owner = stage.watched.lock();
    if (menu == nullptr || owner == nullptr || stage.window == nullptr || !(*owner).attached()) return;
    (*menu).show(owner, position, mode);
}

void drive_context_menu(Stage& stage) {
    focus_subject(stage);
    show_context(stage, Point{20.0, 20.0}, MenuOpenMode::keyboard);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    show_context(stage, Point{40.0, 40.0}, MenuOpenMode::pointer);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::down);
    press_key(stage, PhysicalKey::enter);
    show_context(stage, Point{60.0, 60.0}, MenuOpenMode::pointer);
    press_key(stage, PhysicalKey::escape);
    show_context(stage, Point{60.0, 60.0}, MenuOpenMode::pointer);
    click_at(stage, Point{390.0, 290.0});
}

void check_context_menu(Stage& stage, Findings& findings) {
    if (stage.menu == nullptr) return;
    const ContextMenu& menu = *stage.menu;
    if (menu.is_open() && stage.window == nullptr) findings.fail("ContextMenu is open after its window was destroyed");
}

const HostileEvent context_menu_events[] = {
    {"item_invoked", &attach_context_invoked},
    {"open_changed", &attach_context_open},
};

// --- The subjects -----------------------------------------------------------------

const Subject subjects[] = {
    {"list_view", &build_list_view, list_view_events, &drive_list_view, &check_list_view},
    {"list_box", &build_list_box, list_box_events, &drive_list_box, &check_list_box},
    {"checked_list_box", &build_checked_list_box, checked_list_box_events, &drive_list_box,
     &check_checked_list_box},
    {"combo_box", &build_combo_box, combo_box_events, &drive_combo_box, &check_combo_box},
    {"tree_view", &build_tree_view, tree_view_events, &drive_tree_view, &check_tree_view},
    {"tab_control", &build_tab_control, tab_control_events, &drive_tab_control, &check_tab_control},
    {"split_container", &build_split_container, split_container_events, &drive_split_container,
     &check_split_container},
    {"track_bar", &build_track_bar, range_events, &drive_range, &check_range},
    {"h_scroll_bar", &build_h_scroll_bar, range_events, &drive_range, &check_range},
    {"numeric_up_down", &build_numeric_up_down, numeric_up_down_events, &drive_numeric_up_down,
     &check_numeric_up_down},
    {"data_grid_view", &build_data_grid_view, data_grid_view_events, &drive_data_grid_view,
     &check_data_grid_view},
    {"menu_strip", &build_menu_strip, menu_strip_events, &drive_menu_strip, &check_menu_strip},
    {"context_menu", &build_context_menu, context_menu_events, &drive_context_menu, &check_context_menu},
};

// --- One case ---------------------------------------------------------------------

// Runs one (subject, event, action) case on a fresh window. Returns whether
// the script raised the event. A throw out of dispatch is a finding (the
// audit's throw partway through applying a value): the host would end the
// process there.
bool run_case(const Subject& subject, const HostileEvent& event, HostileAction action, Findings& findings) {
    findings.context = std::string(subject.name) + "/" + event.name + "/" + action_name(action);
    Stage stage;
    stage.handler.stage = &stage;
    stage.action = action;
    stage.root = make_control<Panel>(StableId("hostile.root"));
    (*stage.root).set_requested_bounds({0.0, 0.0, window_width, window_height});
    (*subject.build)(stage);
    stage.watched = stage.subject;
    (*stage.root).add_child(stage.subject);
    stage.window = std::make_unique<Window>(stage.root, Size{window_width, window_height});
    (*stage.window).perform_layout();
    (*event.attach)(stage);
    try {
        (*subject.drive)(stage);
    } catch (const std::exception& error) {
        std::string message = "the script threw out of dispatch: ";
        message += error.what();
        findings.fail(message);
    }
    try {
        check_detached(stage, findings);
        check_window(stage, findings);
        (*subject.check)(stage, findings);
        drive_afterwards(stage);
        check_window(stage, findings);
        (*subject.check)(stage, findings);
    } catch (const std::exception& error) {
        std::string message = "the control threw after the handler acted: ";
        message += error.what();
        findings.fail(message);
    }
    const bool raised = stage.fired != 0U;
    return raised;
}

// Which actions a run takes, from --actions.
enum class ActionSet : std::uint8_t {
    mutate,
    release,
    all,
};

void run_subject(const Subject& subject, ActionSet actions, Findings& findings) {
    for (const HostileEvent& event : subject.events) {
        bool raised = false;
        if (actions != ActionSet::release) {
            for (const HostileAction action : mutating_actions) {
                if (run_case(subject, event, action, findings)) raised = true;
            }
        }
        if (actions != ActionSet::mutate) {
            for (const HostileAction action : releasing_actions) {
                if (run_case(subject, event, action, findings)) raised = true;
            }
        }
        if (!raised) std::cout << "  not raised by the script: " << subject.name << "/" << event.name << '\n';
    }
}

bool parse_actions(std::string_view text, ActionSet& actions) {
    if (text == "mutate") {
        actions = ActionSet::mutate;
        return true;
    }
    if (text == "release") {
        actions = ActionSet::release;
        return true;
    }
    if (text == "all") {
        actions = ActionSet::all;
        return true;
    }
    return false;
}

} // namespace

int main(int argument_count, char** arguments) {
    std::string_view chosen{};
    ActionSet actions = ActionSet::all;
    for (int index = 1; index < argument_count; ++index) {
        const std::string_view argument(arguments[index]);
        const bool has_value = index + 1 < argument_count;
        if (argument == "--subject" && has_value) {
            ++index;
            chosen = arguments[index];
            continue;
        }
        if (argument == "--actions" && has_value) {
            ++index;
            if (parse_actions(arguments[index], actions)) continue;
        }
        std::cerr << "usage: gui_forms_hostile_handlers_tests [--subject NAME] [--actions mutate|release|all]\n";
        return 2;
    }
    Findings findings;
    bool matched = false;
    for (const Subject& subject : subjects) {
        if (!chosen.empty() && chosen != subject.name) continue;
        matched = true;
        std::cout << "hostile handlers: " << subject.name << '\n';
        run_subject(subject, actions, findings);
    }
    if (!matched) {
        std::cerr << "gui_forms_hostile_handlers_tests: no subject named " << chosen << '\n';
        return 2;
    }
    if (!findings.failures.empty()) {
        for (const std::string& failure : findings.failures) {
            std::cerr << "gui_forms_hostile_handlers_tests: " << failure << '\n';
        }
        std::cerr << findings.failures.size() << " hostile-handler findings\n";
        return EXIT_FAILURE;
    }
    std::cout << "hostile handlers: every case kept the control and the window consistent\n";
    return EXIT_SUCCESS;
}
