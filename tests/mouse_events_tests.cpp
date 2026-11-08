// The WinForms mouse events (owner's mouse-event rule, 2026-11-08): their
// order, the host's click count deciding Click or DoubleClick under
// ControlStyles, Button's lack of DoubleClick, MouseHover from the window's
// timer or the host's report, MouseCaptureChanged, context-menu requests,
// and the window's input state and pointer settings.

#include "gui_forms/gui_forms.hpp"

#include <chrono>
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

// The input clock the tests set by hand.
class ManualTime final : public InputTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{FrameClock::now()};

    void advance(std::chrono::milliseconds step) { time += std::chrono::duration_cast<FrameInterval>(step); }
};

using Log = std::vector<std::string>;

// Appends `name`, and the click count where the event has one.
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
struct NoteRequest final {
    Log* log{};
    const char* name{};
    bool handle{};
    void operator()(ContextMenuRequest& request) const {
        (*log).push_back(std::string(name) + (request.origin == ContextRequestOrigin::keyboard ? ":keyboard" : ":pointer"));
        request.handled = handle;
    }
};

// Counts its activations.
class Probe final : public Control {
public:
    explicit Probe(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_activate() override { ++activations; }
    int activations{};
};

// One subscriber to every mouse event of `control`, logging into `log`.
struct Subscriptions {
    Subscriptions(Control& control, Log& log, const char* prefix_name) : prefix(prefix_name) {
        tokens.push_back(control.mouse_down().subscribe(NoteArgs{&log, "down"}));
        tokens.push_back(control.mouse_up().subscribe(NoteArgs{&log, "up"}));
        tokens.push_back(control.click().subscribe(Note{&log, "click"}));
        tokens.push_back(control.mouse_click().subscribe(NoteArgs{&log, "mouse_click"}));
        tokens.push_back(control.double_click().subscribe(Note{&log, "double_click"}));
        tokens.push_back(control.mouse_double_click().subscribe(NoteArgs{&log, "mouse_double_click"}));
        tokens.push_back(control.mouse_enter().subscribe(Note{&log, "enter"}));
        tokens.push_back(control.mouse_leave().subscribe(Note{&log, "leave"}));
        tokens.push_back(control.mouse_hover().subscribe(Note{&log, "hover"}));
        tokens.push_back(control.mouse_capture_changed().subscribe(Note{&log, "capture_changed"}));
        tokens.push_back(control.mouse_wheel().subscribe(NoteArgs{&log, "wheel"}));
    }
    const char* prefix{};
    std::vector<SubscriptionToken> tokens{};
};

// A window holding one Probe at (10, 10, 100, 40), its clock installed.
struct Fixture {
    Fixture()
        : root(make_control<Panel>(StableId("mouse.root"))),
          probe(make_control<Probe>(StableId("mouse.probe"))) {
        (*probe).set_requested_bounds({10.0, 10.0, 100.0, 40.0});
        (*root).add_child(probe);
        window = std::make_unique<Window>(root, Size{200.0, 100.0});
        (*window).set_input_time_source(&clock);
    }
    ~Fixture() {
        if (window) (*window).set_input_time_source(nullptr);
    }

    bool pointer(PointerAction action, PointerButton button, Point position, std::uint32_t clicks = 1U) {
        PointerEvent event{};
        event.action = action;
        event.button = button;
        event.position = position;
        event.pointer_id = 1U;
        event.click_count = clicks;
        return (*window).dispatch_pointer(event);
    }

    ManualTime clock{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Probe> probe;
    std::unique_ptr<Window> window;
};

void test_click_order() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.probe, log, "probe");
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0, 20.0});
    require(log == Log{"down:1", "click", "mouse_click:1", "up:1", "capture_changed"},
            "MouseDown, Click, MouseClick, MouseUp, then the capture's end");
    require((*fixture.probe).activations == 1, "the click activates the control once");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 2U);
    require(log == Log{"down:2", "double_click", "mouse_double_click:2", "up:2", "capture_changed"},
            "the host's second press: MouseDown, DoubleClick, MouseDoubleClick, MouseUp");
    require((*fixture.probe).activations == 1, "a double-click is no activation");

    log.clear();
    (*fixture.probe).set_style(ControlStyles::standard_double_click, false);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 2U);
    require(log == Log{"down:2", "click", "mouse_click:2", "up:2", "capture_changed"} &&
                (*fixture.probe).activations == 2,
            "without standard_double_click the second press is a click");

    log.clear();
    (*fixture.probe).set_style(ControlStyles::standard_click, false);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    require(log == Log{"down:1", "up:1", "capture_changed"} && (*fixture.probe).activations == 2,
            "without standard_click there is no Click and no activation");
}

void test_release_elsewhere_and_other_buttons() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.probe, log, "probe");
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {60.0, 30.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {60.0, 30.0});
    require(log == Log{"down:1", "click", "mouse_click:1", "up:1", "capture_changed"},
            "a press that wanders within the control still clicks");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {150.0, 80.0});
    require(log == Log{"down:1", "up:1", "capture_changed"} && (*fixture.probe).activations == 1,
            "a release away from the control is no click; the capture's end is reported");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::secondary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::secondary, {20.0, 20.0});
    require(log == Log{"down:1", "click", "mouse_click:1", "up:1"} && (*fixture.probe).activations == 1,
            "as in WinForms the secondary button clicks too, with no capture and no activation");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::middle, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::middle, {20.0, 20.0});
    require(log == Log{"down:1", "click", "mouse_click:1", "up:1"} && (*fixture.probe).activations == 1,
            "the middle button clicks too, with no activation");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::secondary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::secondary, {150.0, 80.0});
    fixture.pointer(PointerAction::up, PointerButton::secondary, {20.0, 20.0});
    require(log == Log{"down:1", "up:1"},
            "a secondary release away from the control is no click, and the press is spent");
}

void test_button_has_no_double_click() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("button.root"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("button.ok"), "OK");
    (*button).set_requested_bounds({10.0, 10.0, 100.0, 30.0});
    (*root).add_child(button);
    Window window(root, {200.0, 60.0});
    window.perform_layout();
    Log log;
    const Subscriptions subscriptions(*button, log, "button");
    require(!(*button).has_style(ControlStyles::standard_double_click), "a button clears standard_double_click");
    PointerEvent down{PointerAction::down, PointerButton::primary, {20.0, 20.0}};
    PointerEvent up{PointerAction::up, PointerButton::primary, {20.0, 20.0}};
    window.dispatch_pointer(down);
    window.dispatch_pointer(up);
    down.click_count = 2U;
    up.click_count = 2U;
    window.dispatch_pointer(down);
    window.dispatch_pointer(up);
    require(log == Log{"down:1", "click", "mouse_click:1", "up:1", "capture_changed", "down:2", "click",
                        "mouse_click:2", "up:2", "capture_changed"},
            "a button's double-click is two clicks");

    log.clear();
    PointerEvent secondary_down{PointerAction::down, PointerButton::secondary, {20.0, 20.0}};
    PointerEvent secondary_up{PointerAction::up, PointerButton::secondary, {20.0, 20.0}};
    window.dispatch_pointer(secondary_down);
    window.dispatch_pointer(secondary_up);
    require(log == Log{"down:1", "up:1"}, "a button clicks for the primary button only");
}

void test_enter_leave_wheel_and_capture() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.probe, log, "probe");
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {150.0, 80.0});
    require(log == Log{"enter", "leave"}, "MouseEnter and MouseLeave follow the pointer");

    log.clear();
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = {20.0, 20.0};
    wheel.wheel_delta = {0.0, 1.0};
    (*fixture.window).dispatch_pointer(wheel);
    fixture.pointer(PointerAction::leave, PointerButton::none, {20.0, 20.0});
    require(log == Log{"enter", "wheel:0", "leave"}, "MouseWheel on the control, MouseLeave as the pointer leaves");

    log.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    (*fixture.window).release_pointer();
    require(log == Log{"down:1", "capture_changed"}, "losing the capture raises MouseCaptureChanged");
}

void test_hover_timer() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.probe, log, "probe");
    Window& window = *fixture.window;
    const FrameInterval hover = std::chrono::milliseconds(default_hover_milliseconds);
    const FrameTime entered = fixture.clock.time;
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    require(window.interaction_snapshot().hover_pending, "entering arms the hover timer");
    static_cast<void>(window.poll_frame_schedule(entered + hover - std::chrono::milliseconds(1)));
    require(log == Log{"enter"}, "no hover before the hover time");
    static_cast<void>(window.poll_frame_schedule(entered + hover));
    require(log == Log{"enter", "hover"} && window.interaction_snapshot().hover_raised, "MouseHover after the rest");
    fixture.clock.advance(std::chrono::seconds(1));
    fixture.pointer(PointerAction::move, PointerButton::none, {60.0, 30.0});
    static_cast<void>(window.poll_frame_schedule(fixture.clock.time + hover + hover));
    require(log == Log{"enter", "hover"}, "MouseHover is raised once per entry");

    log.clear();
    fixture.pointer(PointerAction::move, PointerButton::none, {150.0, 80.0});
    fixture.clock.advance(std::chrono::seconds(1));
    const FrameTime again = fixture.clock.time;
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    fixture.clock.advance(std::chrono::milliseconds(300));
    // Leaving the hover rectangle starts the rest again.
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0 + default_hover_size, 20.0});
    static_cast<void>(window.poll_frame_schedule(again + hover));
    require(log == Log{"leave", "enter"}, "a move out of the hover rectangle restarts the rest");
    static_cast<void>(window.poll_frame_schedule(again + std::chrono::milliseconds(300) + hover));
    require(log == Log{"leave", "enter", "hover"}, "and MouseHover follows the new rest");
}

void test_native_hover() {
    Fixture fixture;
    Log log;
    const Subscriptions subscriptions(*fixture.probe, log, "probe");
    Window& window = *fixture.window;
    PresentationSettings settings = window.presentation_settings();
    settings.pointer.native_hover = true;
    window.set_presentation_settings(settings);
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0, 20.0});
    require(!window.interaction_snapshot().hover_pending, "with the host's own hover the window times none");
    static_cast<void>(window.poll_frame_schedule(fixture.clock.time + std::chrono::seconds(5)));
    require(log == Log{"enter"}, "and raises none by itself");
    window.dispatch_host_hover({20.0, 20.0});
    window.dispatch_host_hover({20.0, 20.0});
    require(log == Log{"enter", "hover"}, "the host's report raises MouseHover once per entry");
}

void test_context_requests() {
    Fixture fixture;
    Window& window = *fixture.window;
    Log log;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*fixture.probe).context_menu_requested().subscribe(NoteRequest{&log, "probe", false}));
    tokens.push_back((*fixture.root).context_menu_requested().subscribe(NoteRequest{&log, "root", true}));
    require(window.dispatch_context_request(ContextRequestOrigin::pointer, {20.0, 20.0}) &&
                log == Log{"probe:pointer", "root:pointer"},
            "a pointer request goes to the control under it, then its ancestors until handled");

    log.clear();
    (*fixture.probe).set_focusable(true);
    require(window.request_focus(fixture.probe), "the probe takes focus");
    require(window.dispatch_context_request(ContextRequestOrigin::keyboard, {}) &&
                log == Log{"probe:keyboard", "root:keyboard"},
            "a keyboard request goes to the focused control");

    log.clear();
    tokens.push_back((*fixture.probe).context_menu_requested().subscribe(NoteRequest{&log, "probe-handles", true}));
    require(window.dispatch_context_request(ContextRequestOrigin::pointer, {20.0, 20.0}) &&
                log == Log{"probe:pointer", "probe-handles:pointer"},
            "a handled request stops at its control");
    require(!window.dispatch_key({KeyAction::down, PhysicalKey::f10, Modifier::shift}) &&
                log.size() == 2U,
            "the core makes no context request from keys: that is the host's");
}

// Destroys the window from its control's Click.
struct CloseWindow final {
    std::unique_ptr<Window>* window{};
    void operator()() const { (*window).reset(); }
};

void test_window_destroyed_by_click() {
    Fixture fixture;
    Log log;
    SubscriptionToken up = (*fixture.probe).mouse_up().subscribe(NoteArgs{&log, "up"});
    SubscriptionToken close = (*fixture.probe).click().subscribe(CloseWindow{&fixture.window});
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    PointerEvent release{PointerAction::up, PointerButton::primary, {20.0, 20.0}};
    Window* const window = fixture.window.get();
    static_cast<void>((*window).dispatch_pointer(release));
    require(!fixture.window && log.empty(), "a window destroyed by its Click raises no MouseUp");
}

void test_input_state() {
    Fixture fixture;
    Window& window = *fixture.window;
    const InteractionSnapshot before = window.interaction_snapshot();
    require(before.held_keys.empty() && before.last_click_count == 0U && !before.pointer_inside,
            "a window that has seen no input reports an empty state");
    const FrameTime start = fixture.clock.time;
    window.dispatch_key({KeyAction::down, PhysicalKey::a, Modifier::none});
    fixture.clock.advance(std::chrono::milliseconds(10));
    window.dispatch_key({KeyAction::down, PhysicalKey::b, Modifier::shift});
    KeyEvent repeat{KeyAction::down, PhysicalKey::a, Modifier::shift};
    repeat.repeat = true;
    window.dispatch_key(repeat);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0}, 2U);
    InteractionSnapshot snapshot = window.interaction_snapshot();
    const RuntimeId id = (*fixture.probe).runtime_id();
    require(snapshot.held_keys.size() == 2U && snapshot.held_keys[0] == HeldKey{PhysicalKey::a, start} &&
                snapshot.held_keys[1].physical_key == PhysicalKey::b && snapshot.primary_down &&
                snapshot.last_click_count == 2U && snapshot.pressed == id && snapshot.captured == id &&
                snapshot.pointer_inside,
            "held keys in press order with their times, the buttons, the host's count and the registries");
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0}, 2U);
    window.dispatch_key({KeyAction::up, PhysicalKey::a, Modifier::shift});
    snapshot = window.interaction_snapshot();
    require(snapshot.held_keys.size() == 1U && !snapshot.primary_down && snapshot.captured == RuntimeId{},
            "releases forget their keys and buttons");
    for (std::uint32_t key = PhysicalKey::c; key < PhysicalKey::c + maximum_held_keys; ++key) {
        window.dispatch_key({KeyAction::down, key, Modifier::none});
    }
    snapshot = window.interaction_snapshot();
    require(snapshot.held_keys.size() == maximum_held_keys && snapshot.untracked_key_presses == 1U,
            "keys beyond the capacity are counted, not tracked");
    window.set_active(false);
    snapshot = window.interaction_snapshot();
    require(snapshot.held_keys.empty() && snapshot.modifiers == Modifier::none,
            "deactivation forgets the held keys");
}

void test_pointer_settings() {
    Fixture fixture;
    Window& window = *fixture.window;
    const PresentationSettings defaults = window.presentation_settings();
    require(defaults.pointer.drag_width == default_drag_size &&
                defaults.pointer.drag_origin == SettingOrigin::default_value &&
                defaults.pointer.hover_milliseconds == default_hover_milliseconds && !defaults.pointer.native_hover,
            "the Windows-like defaults hold where no host reported");
    bool refused = false;
    PresentationSettings negative = defaults;
    negative.pointer.drag_width = -1.0;
    try {
        window.set_presentation_settings(negative);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && window.presentation_settings() == defaults, "a negative drag size is refused");
    refused = false;
    PresentationSettings no_hover = defaults;
    no_hover.pointer.hover_milliseconds = 0U;
    try {
        window.set_presentation_settings(no_hover);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a zero hover time is refused");
}

} // namespace

int main() {
    try {
        test_click_order();
        test_release_elsewhere_and_other_buttons();
        test_button_has_no_double_click();
        test_enter_leave_wheel_and_capture();
        test_hover_timer();
        test_native_hover();
        test_context_requests();
        test_window_destroyed_by_click();
        test_input_state();
        test_pointer_settings();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "mouse events tests passed\n";
    return EXIT_SUCCESS;
}
