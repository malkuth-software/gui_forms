// The WinForms mouse events' order, beside mouse_events_tests (Part C docs
// and tests, docs/user/mouse-events.md): DoubleClick on the second release
// and not on the press, a second press released elsewhere, a press counted
// beyond two, MouseEnter before MouseMove and MouseLeave before the next
// control's MouseEnter, a button's activation between MouseClick and
// MouseUp, and the events the click styles leave alone.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using Log = std::vector<std::string>;

// Appends `name` (and the click count where the event has one) to `log`.
struct Note final {
    Log* log{};
    const char* name{};
    void operator()() const { (*log).push_back(name); }
};
struct NoteArgs final {
    Log* log{};
    const char* name{};
    void operator()(const MouseEventArgs& args) const {
        (*log).push_back(std::string(name) + ":" + std::to_string(args.clicks));
    }
};
struct NoteActivation final {
    Log* log{};
    void operator()(ButtonBase&) const { (*log).push_back("clicked"); }
};

// One subscriber to each mouse event of `control`, MouseMove included.
struct Subscriptions final {
    Subscriptions(Control& control, Log& log) {
        tokens.push_back(control.mouse_down().subscribe(NoteArgs{&log, "down"}));
        tokens.push_back(control.mouse_up().subscribe(NoteArgs{&log, "up"}));
        tokens.push_back(control.mouse_move().subscribe(NoteArgs{&log, "move"}));
        tokens.push_back(control.click().subscribe(Note{&log, "click"}));
        tokens.push_back(control.mouse_click().subscribe(NoteArgs{&log, "mouse_click"}));
        tokens.push_back(control.double_click().subscribe(Note{&log, "double_click"}));
        tokens.push_back(control.mouse_double_click().subscribe(NoteArgs{&log, "mouse_double_click"}));
        tokens.push_back(control.mouse_enter().subscribe(Note{&log, "enter"}));
        tokens.push_back(control.mouse_leave().subscribe(Note{&log, "leave"}));
        tokens.push_back(control.mouse_capture_changed().subscribe(Note{&log, "capture_changed"}));
    }
    std::vector<SubscriptionToken> tokens{};
};

// A window holding two panels side by side: `left` at (10, 10, 80, 40) and
// `right` at (110, 10, 80, 40). Panels keep both click styles.
struct Fixture final {
    Fixture()
        : root(make_control<Panel>(StableId("order.root"))), left(make_control<Panel>(StableId("order.left"))),
          right(make_control<Panel>(StableId("order.right"))) {
        (*left).set_requested_bounds({10.0, 10.0, 80.0, 40.0});
        (*right).set_requested_bounds({110.0, 10.0, 80.0, 40.0});
        (*root).add_child(left);
        (*root).add_child(right);
        window = std::make_unique<Window>(root, Size{220.0, 80.0});
        (*window).perform_layout();
    }

    void pointer(PointerAction action, PointerButton button, Point position, std::uint32_t clicks = 1U) {
        PointerEvent event{};
        event.action = action;
        event.button = button;
        event.position = position;
        event.pointer_id = 1U;
        event.click_count = action == PointerAction::move ? 0U : clicks;
        static_cast<void>((*window).dispatch_pointer(event));
    }

    std::shared_ptr<Panel> root;
    std::shared_ptr<Panel> left;
    std::shared_ptr<Panel> right;
    std::unique_ptr<Window> window;
};

void test_double_click_on_the_second_release() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.left, log);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    require(log == Log{"down:2"}, "the second press raises MouseDown only; DoubleClick waits for the release");
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 2U);
    require(log == Log{"down:2", "up:2", "double_click", "mouse_double_click:2", "capture_changed"},
            "the second release raises MouseUp (the target phase), then DoubleClick and MouseDoubleClick");
}

void test_second_press_released_elsewhere() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.left, log);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    fixture.pointer(PointerAction::up, PointerButton::primary, {150.0, 30.0}, 2U);
    require(log == Log{"down:2", "up:2", "capture_changed"},
            "a second press released over another control is neither a double-click nor a click");
}

void test_press_counted_beyond_two() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.left, log);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 3U);
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 3U);
    require(log == Log{"down:3", "up:3", "double_click", "mouse_double_click:3", "capture_changed"},
            "a third press (macOS and Linux count on) is a double-click's press, carrying its count");
}

void test_enter_move_and_leave_order() {
    Fixture fixture;
    Log left_log;
    Log right_log;
    const Subscriptions left_subscriptions(*fixture.left, left_log);
    const Subscriptions right_subscriptions(*fixture.right, right_log);
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {30.0, 20.0});
    require(left_log == Log{"enter", "move:0", "move:0"}, "MouseEnter comes before the first MouseMove");
    fixture.pointer(PointerAction::move, PointerButton::none, {120.0, 20.0});
    require(left_log == Log{"enter", "move:0", "move:0", "leave"} && right_log == Log{"enter", "move:0"},
            "the control left gets MouseLeave and no MouseMove; the control entered gets MouseEnter, then MouseMove");
}

void test_button_activation_between_mouse_click_and_up() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("activation.root"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("activation.ok"), "OK");
    (*button).set_requested_bounds({10.0, 10.0, 100.0, 30.0});
    (*root).add_child(button);
    Window window(root, {200.0, 60.0});
    window.perform_layout();
    Log log;
    const Subscriptions subscriptions(*button, log);
    const SubscriptionToken activation = (*button).clicked().subscribe(NoteActivation{&log});
    PointerEvent move{PointerAction::move, PointerButton::none, {20.0, 20.0}};
    move.click_count = 0U;
    PointerEvent down{PointerAction::down, PointerButton::primary, {20.0, 20.0}};
    PointerEvent up{PointerAction::up, PointerButton::primary, {20.0, 20.0}};
    static_cast<void>(window.dispatch_pointer(move));
    log.clear();
    static_cast<void>(window.dispatch_pointer(down));
    static_cast<void>(window.dispatch_pointer(up));
    require(log == Log{"down:1", "up:1", "click", "mouse_click:1", "clicked", "capture_changed"},
            "a button's activation (clicked) follows MouseUp (the target phase), Click and MouseClick");
}

void test_styles_leave_the_other_events() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.left, log);
    (*fixture.left).Control::set_style(ControlStyles::standard_click, false);
    (*fixture.left).Control::set_style(ControlStyles::standard_double_click, false);
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 2U);
    require(log == Log{"enter", "move:0", "down:1", "up:1", "capture_changed", "down:2", "up:2", "capture_changed"},
            "without the click styles MouseEnter, MouseMove, MouseDown, MouseUp and the capture's end remain");
}

} // namespace

int main() {
    try {
        test_double_click_on_the_second_release();
        test_second_press_released_elsewhere();
        test_press_counted_beyond_two();
        test_enter_move_and_leave_order();
        test_button_activation_between_mouse_click_and_up();
        test_styles_leave_the_other_events();
    } catch (const std::exception& failure) {
        std::cerr << "mouse event order tests failed: " << failure.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "mouse event order tests passed\n";
    return EXIT_SUCCESS;
}
