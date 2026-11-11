// The fail-fast boundary (owner, 2026-11-11): an exception reaching
// call_failing_fast prints the site, the exception's text and a stack, then
// ends the process. Each mode triggers one boundary and prints "survived" only
// if the process was not ended; CTest passes a mode when its report line
// appears and "survived" does not.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"
#include "../src/core/failure/fail_fast.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

// A death test must not open the system's crash dialog, which would hold
// the test until someone dismisses it.
void quiet_crash_reporting() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
}

int throw_injected(int value) {
    if (value != 0) throw std::runtime_error("injected direct failure");
    return value;
}

// Throws under one shadow frame, which unwinding closes before the boundary
// catches; the report must still print it (the failure snapshot).
int throw_in_frame(int value) {
    const ShadowFrameScope frame(ShadowFrameKind::event_callback, "fail-fast.frame", "click", 0U);
    if (value != 0) throw std::runtime_error("injected framed failure");
    return value;
}

class ThrowingTick final {
public:
    void operator()(FrameTime) const { throw std::runtime_error("injected timer failure"); }
};

class ThrowingMover final : public Control {
public:
    explicit ThrowingMover(StableId stable_id)
        : Control(std::move(stable_id)), value(*this, MotionCurve::timed(100ms, MotionEasing::linear)) {
        value.set_listener(MotionListener::bind<ThrowingMover, &ThrowingMover::on_step>(*this));
    }
    void on_step(const MotionStep&) { throw std::runtime_error("injected listener failure"); }
    MotionValue value;
};

class ThrowingProbe final : public Control {
public:
    explicit ThrowingProbe(StableId stable_id) : Control(std::move(stable_id)) {}

protected:
    void on_pointer(PointerEvent&) override { throw std::runtime_error("injected event failure"); }
};

void direct_mode() {
    const int result = gui_forms::detail::call_failing_fast("test.direct", &throw_injected, 1);
    std::printf("returned %d\n", result);
}

void shadow_frames_mode() {
    const int result = gui_forms::detail::call_failing_fast("test.frames", &throw_in_frame, 1);
    std::printf("returned %d\n", result);
}

void ui_timer_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("fail-fast.root"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 60.0});
    Window window(root, {100.0, 60.0});
    const FrameTime due = FrameTime{} + 16ms;
    FrameRequestToken timer = window.schedule_ui_timer(*root, 16ms, due, ThrowingTick{});
    static_cast<void>(window.poll_frame_schedule(due));
    static_cast<void>(timer.connected());
}

void motion_listener_mode() {
    std::shared_ptr<ThrowingMover> root = make_control<ThrowingMover>(StableId("fail-fast.mover"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 60.0});
    Window window(root, {100.0, 60.0});
    static_cast<void>((*root).value.animate_to(1.0));
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + 1s));
}

void host_dispatch_mode() {
    std::shared_ptr<ThrowingProbe> probe = make_control<ThrowingProbe>(StableId("fail-fast.probe"));
    (*probe).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    Window window(probe, {100.0, 80.0});
    host::HeadlessHost host(window);
    static_cast<void>(host.dispatch(HostAttachEvent{{100.0, 80.0}, 1.0}, 1));
    PointerEvent pointer;
    pointer.action = PointerAction::down;
    pointer.button = PointerButton::primary;
    pointer.position = {10.0, 10.0};
    pointer.pointer_id = 7U;
    static_cast<void>(host.dispatch(pointer, 2));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gui_forms_fail_fast_tests direct|ui_timer|motion_listener|host_dispatch|shadow_frames\n");
        return EXIT_FAILURE;
    }
    quiet_crash_reporting();
    const std::string_view mode = argv[1];
    if (mode == "direct") direct_mode();
    else if (mode == "ui_timer") ui_timer_mode();
    else if (mode == "motion_listener") motion_listener_mode();
    else if (mode == "host_dispatch") host_dispatch_mode();
    else if (mode == "shadow_frames") shadow_frames_mode();
    else {
        std::fprintf(stderr, "unknown mode\n");
        return EXIT_FAILURE;
    }
    std::printf("survived\n");
    return EXIT_SUCCESS;
}
