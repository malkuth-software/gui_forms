// The relative pointer example (examples/relative_pointer), driven headless
// on the reference host: a press on the view takes the confined, hidden,
// raw pointer; scripted motion turns the camera, sample by sample; the
// wheel moves it; a press fires; the pointer reaching the shell's capsule
// suspends the mode and the capsule is clicked; moving back resumes it;
// Escape gives the pointer back. Then the visible-cursor variant: free
// mode, every sample, the pointer shown.
#include "orbit_toy.hpp"

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "headless_host.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace gui_forms;
using relative_pointer_example::OrbitToy;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

struct Driver final {
    explicit Driver(OrbitToy& toy) : host(*toy.window) {
        require(host.dispatch(HostAttachEvent{{480.0, 320.0}, 1.0}, step()).accepted(), "attach accepted");
    }
    std::uint64_t step() {
        time = time + 1'000'000U;
        return time;
    }
    void pointer(PointerAction action, Point position, PointerButton button = PointerButton::none) {
        PointerEvent event{};
        event.action = action;
        event.position = position;
        event.button = button;
        event.pointer_id = 1;
        static_cast<void>(host.dispatch(event, step()));
    }
    void motion(Point delta, Point position) {
        HostRelativeMotionEvent event{};
        event.delta = delta;
        event.position.set(position);
        event.unaccelerated = true;
        static_cast<void>(host.dispatch(event, step()));
    }
    void wheel(double pixels, Point position) {
        PointerEvent event{};
        event.action = PointerAction::wheel;
        event.position = position;
        event.wheel_delta = {0.0, pixels};
        event.wheel_unit = WheelDeltaUnit::pixels;
        static_cast<void>(host.dispatch(event, step()));
    }
    void key(KeyAction action, std::uint32_t physical) {
        KeyEvent event{};
        event.action = action;
        event.physical_key = physical;
        static_cast<void>(host.dispatch(event, step()));
    }
    host::HeadlessHost host;
    std::uint64_t time{};
};

void confined_play() {
    OrbitToy toy;
    Driver drive(toy);
    drive.pointer(PointerAction::move, {240.0, 160.0});
    drive.pointer(PointerAction::down, {240.0, 160.0}, PointerButton::primary);
    const RelativePointerGrant grant = (*toy.view).state.last_grant;
    require(grant.result == RelativePointerResult::granted && grant.mode == RelativePointerMode::confined &&
                grant.cursor_hidden && grant.raw,
            "the press takes the confined, hidden, raw pointer");
    drive.pointer(PointerAction::up, {240.0, 160.0}, PointerButton::primary);
    // Forty samples of {2, -1}: forty quarter-degree turns of 2 and 1.
    Point at{240.0, 160.0};
    for (int index = 0; index < 40; ++index) {
        at = Point{at.x + 2.0, at.y - 1.0};
        drive.motion({2.0, -1.0}, at);
    }
    require((*toy.view).state.samples == 40U, "every scripted sample turned the camera");
    require(near((*toy.view).state.yaw, 20.0) && near((*toy.view).state.pitch, 10.0),
            "yaw and pitch are the sum of the samples");
    drive.wheel(40.0, at);
    require(near((*toy.view).state.distance, 8.0), "the wheel is a sample: forty pixels move the camera in by 2");
    drive.pointer(PointerAction::down, at, PointerButton::primary);
    drive.pointer(PointerAction::up, at, PointerButton::primary);
    require((*toy.view).state.shots == 1U, "a press while the mode holds fires");

    // The capsule: the pointer reaching it suspends the mode.
    drive.motion({-280.0, -100.0}, {36.0, 16.0});
    require((*toy.window).relative_pointer_snapshot().suspended, "over the capsule the mode is suspended");
    require(!drive.host.headless_services().relative_pointer_request().active,
            "and the ordinary pointer is back");
    drive.pointer(PointerAction::move, {36.0, 16.0});
    drive.pointer(PointerAction::down, {36.0, 16.0}, PointerButton::primary);
    drive.pointer(PointerAction::up, {36.0, 16.0}, PointerButton::primary);
    require(toy.capsule_clicks == 1U, "the capsule is clickable in the middle of play");
    require((*toy.view).state.ends == 0U, "clicking it suspends, it does not end");
    drive.pointer(PointerAction::move, {240.0, 200.0});
    require(!(*toy.window).relative_pointer_snapshot().suspended &&
                drive.host.headless_services().relative_pointer_request().active,
            "moving back over the view resumes the mode");

    drive.key(KeyAction::down, PhysicalKey::escape);
    drive.key(KeyAction::up, PhysicalKey::escape);
    require((*toy.view).state.ends == 1U && (*toy.view).state.last_end == RelativePointerEnd::escape,
            "Escape gives the pointer back");
    require(!drive.host.headless_services().relative_pointer_request().active, "the host's pointer is ordinary");
}

void visible_cursor_play() {
    OrbitToy toy;
    (*toy.view).visible = true;
    Driver drive(toy);
    drive.pointer(PointerAction::move, {240.0, 160.0});
    drive.pointer(PointerAction::down, {240.0, 160.0}, PointerButton::primary);
    const RelativePointerGrant grant = (*toy.view).state.last_grant;
    require(grant.result == RelativePointerResult::granted && grant.mode == RelativePointerMode::free &&
                !grant.cursor_hidden && grant.raw,
            "the visible-cursor variant: free, shown, raw");
    drive.pointer(PointerAction::up, {240.0, 160.0}, PointerButton::primary);
    for (int index = 0; index < 25; ++index) drive.motion({0.5, 0.0}, {240.0 + 0.5 * (index + 1), 160.0});
    require((*toy.view).state.samples == 25U && near((*toy.view).state.yaw, 3.125),
            "every sample arrives with the pointer shown");
    // The capsule needs no region in free mode: the pointer is ordinary.
    drive.pointer(PointerAction::move, {36.0, 16.0});
    drive.pointer(PointerAction::down, {36.0, 16.0}, PointerButton::primary);
    drive.pointer(PointerAction::up, {36.0, 16.0}, PointerButton::primary);
    require(toy.capsule_clicks == 1U, "the capsule is clicked as usual");
}

} // namespace

int main() {
    confined_play();
    visible_cursor_play();
    std::cout << "relative pointer example tests passed\n";
    return 0;
}
