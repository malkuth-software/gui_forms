// The window's interaction state and gesture recognizer
// (APP_WINDOW_RULES_DESIGN C.2, FD-6 section 5): the interactions and their
// order, double-click interval and slop, hold, drag on controls that take
// drags, the secondary and middle buttons, capture and activation loss,
// held keys and the snapshot, and the threshold validation.

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

// Records what the recognizer raises on it and how often it is activated.
class Recorder final : public Control {
public:
    Recorder(StableId stable_id, bool drags) : Control(std::move(stable_id)), drags_(drags) {}

    void on_interaction(const InteractionEvent& event) override {
        kinds.push_back(event.kind);
        click_counts.push_back(event.click_count);
    }
    void on_activate() override { ++activations; }
    [[nodiscard]] bool accepts_drag_gestures() const noexcept override { return drags_; }

    void clear() {
        kinds.clear();
        click_counts.clear();
        activations = 0;
    }

    std::vector<InteractionKind> kinds{};
    std::vector<std::uint32_t> click_counts{};
    int activations{};

private:
    bool drags_{};
};

// A window holding one Recorder at (10, 10, 100, 40), its clock installed.
struct Fixture {
    explicit Fixture(bool drags)
        : root(make_control<Panel>(StableId("interaction.root"))),
          recorder(make_control<Recorder>(StableId("interaction.recorder"), drags)) {
        (*recorder).set_requested_bounds({10.0, 10.0, 100.0, 40.0});
        (*root).add_child(recorder);
        window = std::make_unique<Window>(root, Size{200.0, 100.0});
        (*window).set_input_time_source(&clock);
    }
    ~Fixture() {
        if (window) (*window).set_input_time_source(nullptr);
    }

    bool pointer(PointerAction action, PointerButton button, Point position) {
        PointerEvent event{};
        event.action = action;
        event.button = button;
        event.position = position;
        event.pointer_id = 1U;
        return (*window).dispatch_pointer(event);
    }

    ManualTime clock{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Recorder> recorder;
    std::unique_ptr<Window> window;
};

using Kinds = std::vector<InteractionKind>;

void test_click_order_and_snapshot() {
    Fixture fixture(false);
    Window& window = *fixture.window;
    const InteractionSnapshot before = window.interaction_snapshot();
    require(before.held_keys.empty() && before.gesture == GesturePhase::none && before.interactions_raised == 0U,
            "a window that has seen no input reports an empty state");

    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    const InteractionSnapshot pressed = window.interaction_snapshot();
    const RuntimeId id = (*fixture.recorder).runtime_id();
    require(pressed.gesture == GesturePhase::pressed && pressed.gesture_button == PointerButton::primary &&
                pressed.gesture_control == id && pressed.pressed == id && pressed.captured == id &&
                pressed.primary_down && pressed.gesture_click_count == 1U && pressed.pointer_inside,
            "a primary press starts a gesture on the pressed, captured control");
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0, 20.0});
    require((*fixture.recorder).kinds == Kinds{InteractionKind::press, InteractionKind::release, InteractionKind::click},
            "press, release, click in that order");
    require((*fixture.recorder).click_counts[2] == 1U && (*fixture.recorder).activations == 1,
            "the click counts one and activates the control once");
    const InteractionSnapshot after = window.interaction_snapshot();
    require(after.gesture == GesturePhase::none && !after.primary_down && after.captured == RuntimeId{} &&
                after.interactions_raised == 3U,
            "the release ends the gesture and the capture");
}

void test_double_click_interval_and_slop() {
    Fixture fixture(false);
    Recorder& recorder = *fixture.recorder;
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    fixture.clock.advance(std::chrono::milliseconds(100));
    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {21.0, 22.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0, 22.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release, InteractionKind::click,
                                    InteractionKind::double_click},
            "a second click within the interval and slop raises double_click after its click");
    require(recorder.click_counts[2] == 2U && recorder.click_counts[3] == 2U && recorder.activations == 1,
            "the second click counts two and still activates once");

    fixture.clock.advance(std::chrono::milliseconds(100));
    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {21.0, 22.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0, 22.0});
    require(recorder.kinds.size() == 3U && recorder.click_counts[2] == 1U,
            "a third click starts a new count");

    fixture.clock.advance(std::chrono::milliseconds(default_double_click_milliseconds + 1U));
    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {21.0, 22.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0, 22.0});
    require(recorder.kinds.size() == 3U && recorder.click_counts[2] == 1U,
            "a second press after the interval is a single click");

    fixture.clock.advance(std::chrono::milliseconds(100));
    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {21.0 + default_gesture_slop + 1.0, 22.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {21.0 + default_gesture_slop + 1.0, 22.0});
    require(recorder.kinds.size() == 3U && recorder.click_counts[2] == 1U,
            "a second press outside the slop is a single click");
}

void test_release_elsewhere_and_wandering() {
    Fixture fixture(false);
    Recorder& recorder = *fixture.recorder;
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {60.0, 30.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {60.0, 30.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release, InteractionKind::click} &&
                recorder.activations == 1,
            "a press that wanders within a control that takes no drags still clicks");

    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {150.0, 80.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {150.0, 80.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release} && recorder.activations == 0,
            "a release away from the control raises release and no click");
}

void test_drag() {
    Fixture fixture(true);
    Recorder& recorder = *fixture.recorder;
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0 + default_drag_threshold, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press}, "a move up to the drag threshold starts no drag");
    fixture.pointer(PointerAction::move, PointerButton::none, {30.0, 20.0});
    require((*fixture.window).interaction_snapshot().gesture == GesturePhase::dragging,
            "a move past the drag threshold starts a drag");
    fixture.pointer(PointerAction::move, PointerButton::none, {40.0, 25.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {40.0, 25.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::drag_start, InteractionKind::drag,
                                    InteractionKind::release, InteractionKind::drag_end} &&
                recorder.activations == 0,
            "press, drag_start, drag, release, drag_end, and no click");
}

void test_hold() {
    Fixture fixture(false);
    Recorder& recorder = *fixture.recorder;
    Window& window = *fixture.window;
    const FrameTime pressed_at = fixture.clock.time;
    const FrameInterval hold = std::chrono::milliseconds(default_hold_milliseconds);
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    static_cast<void>(window.poll_frame_schedule(pressed_at + hold - std::chrono::milliseconds(1)));
    require(recorder.kinds == Kinds{InteractionKind::press}, "no hold before the hold time");
    static_cast<void>(window.poll_frame_schedule(pressed_at + hold));
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::hold} &&
                window.interaction_snapshot().gesture == GesturePhase::held,
            "a press resting for the hold time raises hold");
    static_cast<void>(window.poll_frame_schedule(pressed_at + hold + hold));
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::hold, InteractionKind::release,
                                    InteractionKind::click},
            "hold is raised once and does not cancel the click");

    recorder.clear();
    fixture.clock.advance(std::chrono::seconds(2));
    const FrameTime second_press = fixture.clock.time;
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::move, PointerButton::none, {20.0 + default_gesture_slop + 1.0, 20.0});
    static_cast<void>(window.poll_frame_schedule(second_press + hold + hold));
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0 + default_gesture_slop + 1.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release, InteractionKind::click},
            "a press that leaves the slop raises no hold");
}

void test_capture_and_activation_loss() {
    Fixture fixture(false);
    Recorder& recorder = *fixture.recorder;
    Window& window = *fixture.window;
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    window.release_pointer();
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press} && recorder.activations == 0 &&
                window.interaction_snapshot().gestures_cancelled == 1U,
            "a capture released under the gesture cancels it silently");

    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    window.set_active(false);
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    const InteractionSnapshot inactive = window.interaction_snapshot();
    require(recorder.kinds == Kinds{InteractionKind::press} && inactive.gestures_cancelled == 2U,
            "deactivation cancels the gesture silently");
    window.set_active(true);

    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    const Control::Ptr removed = (*fixture.root).remove_child((*fixture.recorder).runtime_id());
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press} && recorder.activations == 0 &&
                window.interaction_snapshot().gesture == GesturePhase::none,
            "removing the pressed control cancels its gesture");
}

void test_secondary_and_middle() {
    Fixture fixture(false);
    Recorder& recorder = *fixture.recorder;
    Window& window = *fixture.window;
    fixture.pointer(PointerAction::down, PointerButton::secondary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::secondary, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release,
                                    InteractionKind::context_request} &&
                recorder.activations == 0,
            "the secondary button raises context_request after its release, and no click");

    PresentationSettings settings = window.presentation_settings();
    settings.gestures.context_request = ContextRequestEdge::press;
    settings.gestures.context_request_origin = SettingOrigin::host;
    window.set_presentation_settings(settings);
    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::secondary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::secondary, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::context_request,
                                    InteractionKind::release},
            "where the host says press, context_request follows the press");

    recorder.clear();
    fixture.pointer(PointerAction::down, PointerButton::middle, {20.0, 20.0});
    fixture.pointer(PointerAction::down, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::primary, {20.0, 20.0});
    fixture.pointer(PointerAction::up, PointerButton::middle, {20.0, 20.0});
    require(recorder.kinds == Kinds{InteractionKind::press, InteractionKind::release},
            "the middle button raises press and release, and a press during its gesture raises nothing");
}

void test_held_keys() {
    Fixture fixture(false);
    Window& window = *fixture.window;
    const FrameTime start = fixture.clock.time;
    window.dispatch_key({KeyAction::down, PhysicalKey::a, Modifier::none});
    fixture.clock.advance(std::chrono::milliseconds(10));
    window.dispatch_key({KeyAction::down, PhysicalKey::b, Modifier::shift});
    KeyEvent repeat{KeyAction::down, PhysicalKey::a, Modifier::shift};
    repeat.repeat = true;
    window.dispatch_key(repeat);
    InteractionSnapshot snapshot = window.interaction_snapshot();
    require(snapshot.held_keys.size() == 2U && snapshot.held_keys[0] == HeldKey{PhysicalKey::a, start} &&
                snapshot.held_keys[1].physical_key == PhysicalKey::b && snapshot.modifiers == Modifier::shift,
            "held keys are kept in press order with their press times; a repeat adds nothing");
    window.dispatch_key({KeyAction::up, PhysicalKey::a, Modifier::shift});
    snapshot = window.interaction_snapshot();
    require(snapshot.held_keys.size() == 1U && snapshot.held_keys[0].physical_key == PhysicalKey::b,
            "a release forgets its key");

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

void test_threshold_validation() {
    Fixture fixture(false);
    Window& window = *fixture.window;
    const PresentationSettings defaults = window.presentation_settings();
    require(defaults.gestures.slop == default_gesture_slop &&
                defaults.gestures.slop_origin == SettingOrigin::default_value &&
                defaults.gestures.hold_milliseconds == default_hold_milliseconds,
            "the library defaults hold where no host reported");
    bool refused = false;
    PresentationSettings negative = defaults;
    negative.gestures.slop = -1.0;
    try {
        window.set_presentation_settings(negative);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && window.presentation_settings() == defaults, "a negative slop is refused, changing nothing");
    refused = false;
    PresentationSettings no_hold = defaults;
    no_hold.gestures.hold_milliseconds = 0U;
    try {
        window.set_presentation_settings(no_hold);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a zero hold time is refused");
}

} // namespace

int main() {
    try {
        test_click_order_and_snapshot();
        test_double_click_interval_and_slop();
        test_release_elsewhere_and_wandering();
        test_drag();
        test_hold();
        test_capture_and_activation_loss();
        test_secondary_and_middle();
        test_held_keys();
        test_threshold_validation();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "interaction state tests passed\n";
    return EXIT_SUCCESS;
}
