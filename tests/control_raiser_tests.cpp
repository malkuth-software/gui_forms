// Control's interaction raisers (Windows Forms' OnClick, OnMouseDown,
// OnKeyDown and the rest; projects:desks/coordinator/ON_HOOKS_PLAN.md, step
// 4). For each family (mouse, keys, focus, validation), an override whose
// own code runs before the subscribers, one whose code runs after them, one
// that suppresses them, and a subscriber that re-enters the window during
// the raise; then the raisers without an event yet (layout, the background,
// drags, the focus cue), placed against the dispatch around them. The
// subscribers' own order is the one mouse_event_order_tests and
// key_events_tests pin: a raiser adds its entry and moves nothing else.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/clip_tracking_painter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using Log = std::vector<std::string>;

std::string joined(const Log& log) {
    std::string text;
    for (const std::string& entry : log) {
        if (!text.empty()) text += ", ";
        text += entry;
    }
    return text;
}

void require_log(const Log& log, const Log& expected, const std::string& message) {
    require(log == expected, message + " (got: " + joined(log) + ")");
}

// Where a Probe's raiser overrides run their own code (a log entry)
// relative to the base call, which raises the event to the subscribers.
enum class Placement : std::uint8_t {
    before,
    after,
    suppress,
};

// The raisers a Probe logs; the other families pass to the base silently.
enum class Family : std::uint8_t {
    mouse,
    keys,
    focus,
    validation,
    frame,
};

class Probe final : public Panel {
public:
    Probe(StableId stable_id, Log& log, Family family) : Panel(std::move(stable_id)), log_(log), family_(family) {}

    Placement placement{Placement::after};
    // on_key_down sets handled before its base runs (with Placement::before).
    bool handle_key_before_base{};

    void on_key(KeyEvent& event) override {
        if (family_ == Family::keys) {
            log_.push_back(std::string(event.action == KeyAction::down ? "on_key:down:" : "on_key:up:") +
                           std::to_string(event.physical_key));
        }
        Panel::on_key(event);
    }

    void arrange(Rect final_bounds) override {
        if (family_ == Family::frame) log_.push_back("arrange");
        Panel::arrange(final_bounds);
    }

    void on_paint(Painter& painter, Rect local_damage) override {
        if (family_ == Family::frame) log_.push_back("on_paint");
        Panel::on_paint(painter, local_damage);
    }

    void on_drag(DragEvent& event) override {
        if (family_ == Family::frame) log_.push_back("on_drag");
        Panel::on_drag(event);
    }

protected:
    void on_mouse_down(const MouseEventArgs& arguments) override {
        const bool run = enter(Family::mouse, "mouse_down");
        if (run) Panel::on_mouse_down(arguments);
        leave(Family::mouse, "mouse_down");
    }

    void on_mouse_up(const MouseEventArgs& arguments) override {
        const bool run = enter(Family::mouse, "mouse_up");
        if (run) Panel::on_mouse_up(arguments);
        leave(Family::mouse, "mouse_up");
    }

    void on_mouse_move(const MouseEventArgs& arguments) override {
        const bool run = enter(Family::mouse, "mouse_move");
        if (run) Panel::on_mouse_move(arguments);
        leave(Family::mouse, "mouse_move");
    }

    void on_mouse_enter() override {
        const bool run = enter(Family::mouse, "mouse_enter");
        if (run) Panel::on_mouse_enter();
        leave(Family::mouse, "mouse_enter");
    }

    void on_mouse_capture_changed() override {
        const bool run = enter(Family::mouse, "mouse_capture_changed");
        if (run) Panel::on_mouse_capture_changed();
        leave(Family::mouse, "mouse_capture_changed");
    }

    void on_click() override {
        const bool run = enter(Family::mouse, "click");
        if (run) Panel::on_click();
        leave(Family::mouse, "click");
    }

    void on_mouse_click(const MouseEventArgs& arguments) override {
        const bool run = enter(Family::mouse, "mouse_click");
        if (run) Panel::on_mouse_click(arguments);
        leave(Family::mouse, "mouse_click");
    }

    void on_preview_key_down(PreviewKeyDownEventArgs& arguments) override {
        const std::string name = "preview_key_down:" + std::to_string(arguments.physical_key);
        const bool run = enter(Family::keys, name);
        if (run) Panel::on_preview_key_down(arguments);
        leave(Family::keys, name);
    }

    void on_key_down(KeyEventArgs& arguments) override {
        const std::string name = "key_down:" + std::to_string(arguments.physical_key);
        const bool run = enter(Family::keys, name);
        if (handle_key_before_base) arguments.handled = true;
        if (run) Panel::on_key_down(arguments);
        leave(Family::keys, name);
    }

    void on_key_up(KeyEventArgs& arguments) override {
        const std::string name = "key_up:" + std::to_string(arguments.physical_key);
        const bool run = enter(Family::keys, name);
        if (run) Panel::on_key_up(arguments);
        leave(Family::keys, name);
    }

    void on_enter() override {
        const bool run = enter(Family::focus, "enter");
        if (run) Panel::on_enter();
        leave(Family::focus, "enter");
    }

    void on_got_focus() override {
        const bool run = enter(Family::focus, "got_focus");
        if (run) Panel::on_got_focus();
        leave(Family::focus, "got_focus");
    }

    void on_lost_focus() override {
        const bool run = enter(Family::focus, "lost_focus");
        if (run) Panel::on_lost_focus();
        leave(Family::focus, "lost_focus");
    }

    void on_leave() override {
        const bool run = enter(Family::focus, "leave");
        if (run) Panel::on_leave();
        leave(Family::focus, "leave");
    }

    void on_validating(ControlValidationEvent& event) override {
        const bool run = enter(Family::validation, "validating");
        if (run) Panel::on_validating(event);
        leave(Family::validation, "validating");
    }

    void on_validated() override {
        const bool run = enter(Family::validation, "validated");
        if (run) Panel::on_validated();
        leave(Family::validation, "validated");
    }

    void on_layout() override {
        const bool run = enter(Family::frame, "layout");
        if (run) Panel::on_layout();
        leave(Family::frame, "layout");
    }

    void on_paint_background(PaintEventArgs& arguments) override {
        const bool run = enter(Family::frame, "paint_background");
        if (run) Panel::on_paint_background(arguments);
        leave(Family::frame, "paint_background");
    }

    void on_drag_enter(DragEvent& event) override {
        const bool run = enter(Family::frame, "drag_enter");
        if (run) Panel::on_drag_enter(event);
        leave(Family::frame, "drag_enter");
    }

    void on_drag_drop(DragEvent& event) override {
        const bool run = enter(Family::frame, "drag_drop");
        if (run) Panel::on_drag_drop(event);
        leave(Family::frame, "drag_drop");
    }

    void on_change_ui_cues(bool visible) override {
        const std::string name = std::string("change_ui_cues:") + (visible ? "true" : "false");
        const bool run = enter(Family::frame, name);
        if (run) Panel::on_change_ui_cues(visible);
        leave(Family::frame, name);
    }

private:
    // Logs the raiser before the base, or instead of it; true when the base
    // runs.
    bool enter(Family family, const std::string& name) {
        if (family != family_) return true;
        if (placement != Placement::after) log_.push_back("raiser:" + name);
        const bool run = placement != Placement::suppress;
        return run;
    }

    void leave(Family family, const std::string& name) {
        if (family == family_ && placement == Placement::after) log_.push_back("raiser:" + name);
    }

    Log& log_;
    Family family_;
};

// Subscribers: each appends "event:<name>" to its log.
struct NoteEvent final {
    Log* log{};
    const char* name{};
    void operator()() const { (*log).push_back(std::string("event:") + name); }
};

struct NoteMouse final {
    Log* log{};
    const char* name{};
    void operator()(const MouseEventArgs&) const { (*log).push_back(std::string("event:") + name); }
};

struct NoteKey final {
    Log* log{};
    const char* name{};
    void operator()(KeyEventArgs& arguments) const {
        (*log).push_back(std::string("event:") + name + ":" + std::to_string(arguments.physical_key));
    }
};

struct NoteFocus final {
    Log* log{};
    void operator()(bool focused) const {
        (*log).push_back(std::string("event:focus_observed:") + (focused ? "true" : "false"));
    }
};

struct NoteValidating final {
    Log* log{};
    void operator()(ControlValidationEvent&) const { (*log).push_back("event:validating"); }
};

// Re-entering subscribers.

// A mouse_down subscriber that moves the pointer over the pressed control
// from inside the raise.
struct MoveOnDown final {
    Log* log{};
    Window* window{};
    Point position{};
    void operator()(const MouseEventArgs&) const {
        (*log).push_back("event:mouse_down");
        PointerEvent move{};
        move.action = PointerAction::move;
        move.position = position;
        move.pointer_id = 1U;
        move.click_count = 0U;
        static_cast<void>((*window).dispatch_pointer(move));
    }
};

// A key_down subscriber that, on the first A, presses B from inside the
// raise.
struct PressOnKey final {
    Log* log{};
    Window* window{};
    bool* sent{};
    void operator()(KeyEventArgs& arguments) const {
        (*log).push_back("event:key_down:" + std::to_string(arguments.physical_key));
        if (*sent || arguments.physical_key != PhysicalKey::a) return;
        *sent = true;
        KeyEvent nested{};
        nested.action = KeyAction::down;
        nested.physical_key = PhysicalKey::b;
        static_cast<void>((*window).dispatch_key(nested));
    }
};

// A focus_observed subscriber that asks for the focus its control already
// has: the window refuses nothing and raises nothing again.
struct FocusAgain final {
    Log* log{};
    Window* window{};
    // Weak: the subscription lives in the control it names.
    std::weak_ptr<Control> control{};
    void operator()(bool focused) const {
        (*log).push_back(std::string("event:focus_observed:") + (focused ? "true" : "false"));
        if (!focused) return;
        const bool kept = (*window).request_focus(control.lock());
        (*log).push_back(std::string("nested:") + (kept ? "true" : "false"));
    }
};

// A validating subscriber that validates its control again: the window
// refuses a validation during another.
struct ValidateAgain final {
    Log* log{};
    Window* window{};
    // Weak: the subscription lives in the control it names.
    std::weak_ptr<Control> control{};
    void operator()(ControlValidationEvent&) const {
        (*log).push_back("event:validating");
        const bool accepted = (*window).validate_control(control.lock());
        (*log).push_back(std::string("nested:") + (accepted ? "true" : "false"));
    }
};

// A window over a root panel holding a probe at (10, 10, 80, 40) and a
// focusable panel beside it at (110, 10, 80, 40).
struct Fixture final {
    explicit Fixture(Family family, bool focusable_probe = false)
        : root(make_control<Panel>(StableId("raisers.root"))),
          probe(make_control<Probe>(StableId("raisers.probe"), log, family)),
          other(make_control<Panel>(StableId("raisers.other"))) {
        (*probe).set_requested_bounds({10.0, 10.0, 80.0, 40.0});
        (*other).set_requested_bounds({110.0, 10.0, 80.0, 40.0});
        (*probe).focusable = focusable_probe;
        (*other).focusable = true;
        (*root).add_child(probe);
        (*root).add_child(other);
        window = std::make_unique<Window>(root, Size{220.0, 80.0});
        (*window).perform_layout();
        (*window).set_active(true);
    }

    void pointer(PointerAction action, Point position) {
        PointerEvent event{};
        event.action = action;
        event.button = PointerButton::primary;
        event.position = position;
        event.pointer_id = 1U;
        event.click_count = action == PointerAction::move ? 0U : 1U;
        static_cast<void>((*window).dispatch_pointer(event));
    }

    void key(KeyAction action, std::uint32_t physical) {
        KeyEvent event{};
        event.action = action;
        event.physical_key = physical;
        static_cast<void>((*window).dispatch_key(event));
    }

    Log log;
    std::shared_ptr<Panel> root;
    std::shared_ptr<Probe> probe;
    std::shared_ptr<Panel> other;
    std::unique_ptr<Window> window;
};

// The mouse.

struct MouseSubscriptions final {
    MouseSubscriptions(Control& control, Log& log) {
        tokens.push_back(control.mouse_down().subscribe(NoteMouse{&log, "mouse_down"}));
        tokens.push_back(control.mouse_up().subscribe(NoteMouse{&log, "mouse_up"}));
        tokens.push_back(control.click().subscribe(NoteEvent{&log, "click"}));
        tokens.push_back(control.mouse_click().subscribe(NoteMouse{&log, "mouse_click"}));
        tokens.push_back(control.mouse_capture_changed().subscribe(NoteEvent{&log, "mouse_capture_changed"}));
    }
    std::vector<SubscriptionToken> tokens{};
};

Log click_log(Placement placement) {
    Fixture fixture(Family::mouse);
    (*fixture.probe).placement = placement;
    const MouseSubscriptions subscriptions(*fixture.probe, fixture.log);
    fixture.log.clear();
    fixture.pointer(PointerAction::down, {20.0, 20.0});
    fixture.pointer(PointerAction::up, {20.0, 20.0});
    return fixture.log;
}

void test_mouse_raisers_after_the_subscribers() {
    require_log(click_log(Placement::after),
                Log{"event:mouse_down", "raiser:mouse_down", "event:click", "raiser:click", "event:mouse_click",
                    "raiser:mouse_click", "event:mouse_up", "raiser:mouse_up", "event:mouse_capture_changed",
                    "raiser:mouse_capture_changed"},
                "code after the base runs after each mouse event's subscribers, in the events' own order");
}

void test_mouse_raisers_before_the_subscribers() {
    require_log(click_log(Placement::before),
                Log{"raiser:mouse_down", "event:mouse_down", "raiser:click", "event:click", "raiser:mouse_click",
                    "event:mouse_click", "raiser:mouse_up", "event:mouse_up", "raiser:mouse_capture_changed",
                    "event:mouse_capture_changed"},
                "code before the base runs before each mouse event's subscribers");
}

void test_mouse_raisers_suppress_the_subscribers() {
    require_log(click_log(Placement::suppress),
                Log{"raiser:mouse_down", "raiser:click", "raiser:mouse_click", "raiser:mouse_up",
                    "raiser:mouse_capture_changed"},
                "a raiser that does not call the base keeps each mouse event from its subscribers");
}

void test_mouse_subscriber_reenters() {
    Fixture fixture(Family::mouse);
    const Point inside{20.0, 20.0};
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*fixture.probe).mouse_down().subscribe(MoveOnDown{&fixture.log, fixture.window.get(), inside}));
    tokens.push_back((*fixture.probe).mouse_enter().subscribe(NoteEvent{&fixture.log, "mouse_enter"}));
    tokens.push_back((*fixture.probe).mouse_move().subscribe(NoteMouse{&fixture.log, "mouse_move"}));
    fixture.log.clear();
    fixture.pointer(PointerAction::down, inside);
    require_log(fixture.log,
                Log{"event:mouse_down", "event:mouse_enter", "raiser:mouse_enter", "event:mouse_move",
                    "raiser:mouse_move", "raiser:mouse_down"},
                "a move dispatched by a MouseDown subscriber raises its own raisers nested inside on_mouse_down");
}

// The keys.

struct KeySubscriptions final {
    KeySubscriptions(Control& control, Log& log) {
        tokens.push_back(control.key_down().subscribe(NoteKey{&log, "key_down"}));
        tokens.push_back(control.key_up().subscribe(NoteKey{&log, "key_up"}));
    }
    std::vector<SubscriptionToken> tokens{};
};

Log key_log(Placement placement, bool handle_before_base) {
    Fixture fixture(Family::keys, true);
    require((*fixture.window).request_focus(fixture.probe), "the probe takes focus");
    (*fixture.probe).placement = placement;
    (*fixture.probe).handle_key_before_base = handle_before_base;
    const KeySubscriptions subscriptions(*fixture.probe, fixture.log);
    fixture.log.clear();
    fixture.key(KeyAction::down, PhysicalKey::a);
    fixture.key(KeyAction::up, PhysicalKey::a);
    return fixture.log;
}

void test_key_raisers_after_the_subscribers() {
    const std::string a = std::to_string(PhysicalKey::a);
    require_log(key_log(Placement::after, false),
                Log{"raiser:preview_key_down:" + a, "event:key_down:" + a, "raiser:key_down:" + a, "on_key:down:" + a,
                    "event:key_up:" + a, "raiser:key_up:" + a, "on_key:up:" + a},
                "code after the base runs after the key subscribers and before the control's own key handling");
}

void test_key_raisers_before_the_subscribers() {
    const std::string a = std::to_string(PhysicalKey::a);
    require_log(key_log(Placement::before, false),
                Log{"raiser:preview_key_down:" + a, "raiser:key_down:" + a, "event:key_down:" + a, "on_key:down:" + a,
                    "raiser:key_up:" + a, "event:key_up:" + a, "on_key:up:" + a},
                "code before the base runs before the key subscribers (WinForms' OnKeyDown order)");
}

void test_key_raisers_suppress_the_subscribers() {
    const std::string a = std::to_string(PhysicalKey::a);
    require_log(key_log(Placement::suppress, false),
                Log{"raiser:preview_key_down:" + a, "raiser:key_down:" + a, "on_key:down:" + a, "raiser:key_up:" + a,
                    "on_key:up:" + a},
                "a key raiser that does not call the base keeps the key from its subscribers, not from the control");
}

void test_key_raiser_handles_before_the_subscribers() {
    const std::string a = std::to_string(PhysicalKey::a);
    require_log(key_log(Placement::before, true),
                Log{"raiser:preview_key_down:" + a, "raiser:key_down:" + a, "event:key_down:" + a,
                    "raiser:key_up:" + a, "event:key_up:" + a, "on_key:up:" + a},
                "handled set before the base: the subscribers still see the key, the control's handling does not");
}

void test_key_subscriber_reenters() {
    Fixture fixture(Family::keys, true);
    require((*fixture.window).request_focus(fixture.probe), "the probe takes focus");
    bool sent = false;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*fixture.probe).key_down().subscribe(PressOnKey{&fixture.log, fixture.window.get(), &sent}));
    fixture.log.clear();
    fixture.key(KeyAction::down, PhysicalKey::a);
    const std::string a = std::to_string(PhysicalKey::a);
    const std::string b = std::to_string(PhysicalKey::b);
    require_log(fixture.log,
                Log{"raiser:preview_key_down:" + a, "event:key_down:" + a, "raiser:preview_key_down:" + b,
                    "event:key_down:" + b, "raiser:key_down:" + b, "on_key:down:" + b, "raiser:key_down:" + a,
                    "on_key:down:" + a},
                "a key dispatched by a KeyDown subscriber runs its whole route nested inside on_key_down");
}

// Focus.

Log focus_log(Placement placement, bool lose) {
    Fixture fixture(Family::focus, true);
    (*fixture.probe).placement = placement;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*fixture.probe).focus_observed().subscribe(NoteFocus{&fixture.log}));
    fixture.log.clear();
    require((*fixture.window).request_focus(fixture.probe), "the probe takes focus");
    if (lose) {
        fixture.log.clear();
        require((*fixture.window).request_focus(fixture.other), "the other panel takes focus");
    }
    return fixture.log;
}

void test_focus_raisers_after_the_subscribers() {
    require_log(focus_log(Placement::after, false),
                Log{"raiser:enter", "event:focus_observed:true", "raiser:got_focus"},
                "gaining focus: on_enter, then on_got_focus around focus_observed(true)");
    require_log(focus_log(Placement::after, true),
                Log{"event:focus_observed:false", "raiser:lost_focus", "raiser:leave"},
                "losing focus: on_lost_focus around focus_observed(false), then on_leave");
}

void test_focus_raisers_before_the_subscribers() {
    require_log(focus_log(Placement::before, false),
                Log{"raiser:enter", "raiser:got_focus", "event:focus_observed:true"},
                "code before the base runs before focus_observed(true)'s subscribers");
    require_log(focus_log(Placement::before, true),
                Log{"raiser:lost_focus", "event:focus_observed:false", "raiser:leave"},
                "code before the base runs before focus_observed(false)'s subscribers");
}

void test_focus_raisers_suppress_the_subscribers() {
    require_log(focus_log(Placement::suppress, false), Log{"raiser:enter", "raiser:got_focus"},
                "on_got_focus without the base keeps focus_observed(true) from its subscribers");
    require_log(focus_log(Placement::suppress, true), Log{"raiser:lost_focus", "raiser:leave"},
                "on_lost_focus without the base keeps focus_observed(false) from its subscribers");
}

void test_focus_subscriber_reenters() {
    Fixture fixture(Family::focus, true);
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(
        (*fixture.probe).focus_observed().subscribe(FocusAgain{&fixture.log, fixture.window.get(), fixture.probe}));
    fixture.log.clear();
    require((*fixture.window).request_focus(fixture.probe), "the probe takes focus");
    require_log(fixture.log, Log{"raiser:enter", "event:focus_observed:true", "nested:true", "raiser:got_focus"},
                "asking for the focus the control already has, inside on_got_focus, raises nothing again");
}

// Validation.

Log validation_log(Placement placement) {
    Fixture fixture(Family::validation, true);
    (*fixture.probe).placement = placement;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*fixture.probe).validating().subscribe(NoteValidating{&fixture.log}));
    tokens.push_back((*fixture.probe).validated().subscribe(NoteEvent{&fixture.log, "validated"}));
    fixture.log.clear();
    require((*fixture.window).validate_control(fixture.probe), "the probe validates");
    return fixture.log;
}

void test_validation_raisers() {
    require_log(validation_log(Placement::after),
                Log{"event:validating", "raiser:validating", "event:validated", "raiser:validated"},
                "code after the base runs after the validation subscribers");
    require_log(validation_log(Placement::before),
                Log{"raiser:validating", "event:validating", "raiser:validated", "event:validated"},
                "code before the base runs before the validation subscribers");
    require_log(validation_log(Placement::suppress), Log{"raiser:validating", "raiser:validated"},
                "validation raisers without the base keep both events from their subscribers");
}

void test_validation_subscriber_reenters() {
    Fixture fixture(Family::validation, true);
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(
        (*fixture.probe).validating().subscribe(ValidateAgain{&fixture.log, fixture.window.get(), fixture.probe}));
    fixture.log.clear();
    require((*fixture.window).validate_control(fixture.probe), "the probe validates");
    require_log(fixture.log, Log{"event:validating", "nested:false", "raiser:validating", "raiser:validated"},
                "a validation asked for inside on_validating is refused and raises nothing nested");
}

// The raisers with no event yet.

std::size_t position_of(const Log& log, const std::string& entry) {
    const Log::const_iterator found = std::find(log.begin(), log.end(), entry);
    const std::size_t position = static_cast<std::size_t>(found - log.begin());
    return position;
}

void test_frame_raisers() {
    Fixture fixture(Family::frame, true);
    const std::size_t arranged = position_of(fixture.log, "arrange");
    require(arranged + 1U < fixture.log.size() && fixture.log[arranged + 1U] == "raiser:layout",
            "on_layout runs right after arrange (got: " + joined(fixture.log) + ")");

    fixture.log.clear();
    test_support::ClipTrackingPainter painter{};
    static_cast<void>((*fixture.window).paint(painter, {0.0, 0.0, 220.0, 80.0}));
    const std::size_t background = position_of(fixture.log, "raiser:paint_background");
    const std::size_t content = position_of(fixture.log, "on_paint");
    require(background < content && content < fixture.log.size(),
            "on_paint_background runs before on_paint (got: " + joined(fixture.log) + ")");

    (*fixture.probe).allow_drop = true;
    fixture.log.clear();
    DragEvent drag{};
    drag.action = DragAction::enter;
    drag.session_id = 7U;
    drag.position = {20.0, 20.0};
    drag.allowed_effects = DragEffect::copy;
    drag.items = {DragTextData{"text"}};
    static_cast<void>((*fixture.window).dispatch_drag(drag));
    require_log(fixture.log, Log{"on_drag", "raiser:drag_enter"}, "on_drag_enter runs after the target's on_drag");
    fixture.log.clear();
    drag.action = DragAction::drop;
    static_cast<void>((*fixture.window).dispatch_drag(drag));
    require_log(fixture.log, Log{"on_drag", "raiser:drag_drop"}, "on_drag_drop runs after the target's on_drag");

    require((*fixture.window).request_focus(fixture.probe), "the probe takes focus");
    fixture.log.clear();
    fixture.key(KeyAction::down, PhysicalKey::a);
    require(position_of(fixture.log, "raiser:change_ui_cues:true") < fixture.log.size(),
            "a key shows the focus cue: on_change_ui_cues(true) on the focused control (got: " + joined(fixture.log) +
                ")");
}

} // namespace

int main() {
    try {
        test_mouse_raisers_after_the_subscribers();
        test_mouse_raisers_before_the_subscribers();
        test_mouse_raisers_suppress_the_subscribers();
        test_mouse_subscriber_reenters();
        test_key_raisers_after_the_subscribers();
        test_key_raisers_before_the_subscribers();
        test_key_raisers_suppress_the_subscribers();
        test_key_raiser_handles_before_the_subscribers();
        test_key_subscriber_reenters();
        test_focus_raisers_after_the_subscribers();
        test_focus_raisers_before_the_subscribers();
        test_focus_raisers_suppress_the_subscribers();
        test_focus_subscriber_reenters();
        test_validation_raisers();
        test_validation_subscriber_reenters();
        test_frame_raisers();
        std::cout << "gui_forms_control_raiser_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_control_raiser_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
