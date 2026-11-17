// The first-frame show gate (gui_forms/host/windowing/first_frame_show.hpp):
// a first frame before the deadline, the deadline firing, a close or hide
// before the show, a frame presented before the request, a zero ceiling,
// shows after the first, reentrancy and the clamped ceiling. Headless and
// clockless: every time is passed in.

#include "gui_forms/host/windowing/first_frame_show.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using gui_forms::FirstFrameShowCause;
using gui_forms::FirstFrameShowGate;
using gui_forms::FirstFrameShowState;
using gui_forms::FirstFrameShowStep;
using gui_forms::FrameInterval;
using gui_forms::FrameTime;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

FrameTime at(std::chrono::milliseconds offset) {
    const FrameTime origin{};
    const FrameTime time = origin + std::chrono::duration_cast<FrameInterval>(offset);
    return time;
}

FrameInterval interval(std::chrono::milliseconds value) {
    const FrameInterval converted = std::chrono::duration_cast<FrameInterval>(value);
    return converted;
}

void test_default_ceiling_is_the_owners() {
    const FirstFrameShowGate gate{};
    require(gate.snapshot().ceiling == interval(std::chrono::milliseconds(300)),
            "the default ceiling is 300 ms");
    require(gate.state() == FirstFrameShowState::idle, "a new gate is idle");
    require(gate.deadline().absent(), "a new gate has no deadline");
}

void test_first_frame_before_the_deadline_shows() {
    FirstFrameShowGate gate{};
    const FirstFrameShowStep requested = gate.request_show(at(std::chrono::milliseconds(1000)));
    require(!requested.show, "a request without a frame waits");
    require(gate.waiting(), "the gate waits after the request");
    require(gate.deadline().present() && *gate.deadline().value() == at(std::chrono::milliseconds(1300)),
            "the deadline is the request plus the ceiling");
    const FirstFrameShowStep early = gate.poll(at(std::chrono::milliseconds(1299)));
    require(!early.show, "a wake before the deadline shows nothing");
    const FirstFrameShowStep presented = gate.report_frame_presented(at(std::chrono::milliseconds(1040)));
    require(presented.show && presented.cause == FirstFrameShowCause::frame_presented,
            "the first presented frame shows the window");
    require(gate.state() == FirstFrameShowState::shown, "the gate is shown");
    require(gate.deadline().absent(), "a shown gate has no deadline");
    require(gate.snapshot().last_wait == interval(std::chrono::milliseconds(40)),
            "the wait is measured from the request");
    // The deadline passing afterwards shows nothing more.
    const FirstFrameShowStep late = gate.poll(at(std::chrono::milliseconds(1400)));
    require(!late.show, "the deadline after the show shows nothing");
    require(gate.snapshot().frame_shows == 1U && gate.snapshot().deadline_shows == 0U,
            "one show, by the frame");
}

void test_deadline_shows_without_a_frame() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    const FirstFrameShowStep fired = gate.poll(at(std::chrono::milliseconds(300)));
    require(fired.show && fired.cause == FirstFrameShowCause::deadline,
            "the deadline shows the window at exactly the ceiling");
    require(gate.snapshot().deadline_shows == 1U, "one deadline show");
    require(gate.snapshot().last_wait == interval(std::chrono::milliseconds(300)),
            "the deadline show waited the ceiling");
    // A frame arriving after the fallback is recorded and shows nothing.
    const FirstFrameShowStep presented = gate.report_frame_presented(at(std::chrono::milliseconds(320)));
    require(!presented.show, "a frame after the deadline show shows nothing");
    require(gate.snapshot().presented_frames == 1U && gate.snapshot().frame_presented,
            "the late frame is recorded");
}

void test_late_wake_shows_once() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    const FirstFrameShowStep fired = gate.poll(at(std::chrono::milliseconds(900)));
    require(fired.show && fired.cause == FirstFrameShowCause::deadline, "a late wake shows");
    const FirstFrameShowStep again = gate.poll(at(std::chrono::milliseconds(901)));
    require(!again.show, "a second wake shows nothing");
}

void test_close_before_show_cancels() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    require(gate.cancel(), "a close during the wait cancels it");
    require(gate.state() == FirstFrameShowState::idle, "a cancelled gate is idle");
    require(gate.deadline().absent(), "a cancelled wait forgets its deadline");
    const FirstFrameShowStep fired = gate.poll(at(std::chrono::milliseconds(500)));
    require(!fired.show, "the forgotten deadline shows nothing");
    const FirstFrameShowStep presented = gate.report_frame_presented(at(std::chrono::milliseconds(510)));
    require(!presented.show, "a frame after the cancel shows nothing");
    require(gate.snapshot().cancelled_waits == 1U, "one cancelled wait");
    require(!gate.cancel(), "a second cancel changes nothing");
    require(gate.snapshot().frame_shows == 0U && gate.snapshot().deadline_shows == 0U &&
                gate.snapshot().immediate_shows == 0U,
            "a cancelled window was never shown");
}

void test_hide_then_show_again_waits_anew() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    require(gate.cancel(), "a hide during the wait cancels it");
    const FirstFrameShowStep again = gate.request_show(at(std::chrono::milliseconds(1000)));
    require(!again.show, "a show after the hide waits again");
    require(*gate.deadline().value() == at(std::chrono::milliseconds(1300)),
            "the new wait has its own deadline");
    const FirstFrameShowStep fired = gate.poll(at(std::chrono::milliseconds(1300)));
    require(fired.show && fired.cause == FirstFrameShowCause::deadline, "the new deadline shows");
}

void test_frame_before_request_shows_at_once() {
    FirstFrameShowGate gate{};
    const FirstFrameShowStep presented = gate.report_frame_presented(at(std::chrono::milliseconds(5)));
    require(!presented.show, "a frame with no request shows nothing");
    const FirstFrameShowStep requested = gate.request_show(at(std::chrono::milliseconds(10)));
    require(requested.show && requested.cause == FirstFrameShowCause::frame_presented,
            "a request after a presented frame shows at once");
    require(gate.snapshot().last_wait == FrameInterval::zero(), "it waited nothing");
}

void test_zero_ceiling_turns_the_gate_off() {
    FirstFrameShowGate gate{FrameInterval::zero()};
    const FirstFrameShowStep requested = gate.request_show(at(std::chrono::milliseconds(0)));
    require(requested.show && requested.cause == FirstFrameShowCause::immediate,
            "a zero ceiling shows at once");
    require(gate.snapshot().immediate_shows == 1U, "one immediate show");
}

void test_later_shows_are_at_once() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    static_cast<void>(gate.report_frame_presented(at(std::chrono::milliseconds(20))));
    require(!gate.cancel(), "a hide after the show cancels no wait");
    const FirstFrameShowStep again = gate.request_show(at(std::chrono::milliseconds(2000)));
    require(again.show && again.cause == FirstFrameShowCause::immediate,
            "a window shown before is shown again at once");
}

void test_repeated_request_keeps_the_first_deadline() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    const FirstFrameShowStep repeated = gate.request_show(at(std::chrono::milliseconds(200)));
    require(!repeated.show, "a repeated request while waiting shows nothing");
    require(*gate.deadline().value() == at(std::chrono::milliseconds(300)),
            "the first deadline stands");
}

void test_reentrant_report_shows_once() {
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    const FirstFrameShowStep first = gate.report_frame_presented(at(std::chrono::milliseconds(10)));
    // The host's show presents again (a key window repaints): reentrant.
    const FirstFrameShowStep reentrant = gate.report_frame_presented(at(std::chrono::milliseconds(10)));
    require(first.show && !reentrant.show, "the show is returned exactly once");
}

void test_ceiling_is_clamped() {
    const FirstFrameShowGate long_gate{interval(std::chrono::milliseconds(60000))};
    require(long_gate.snapshot().ceiling == interval(gui_forms::maximum_first_frame_show_ceiling),
            "a long ceiling is clamped to the maximum");
    const FirstFrameShowGate negative_gate{interval(std::chrono::milliseconds(-5))};
    require(negative_gate.snapshot().ceiling == FrameInterval::zero(),
            "a negative ceiling is zero");
    FirstFrameShowGate gate{};
    static_cast<void>(gate.request_show(at(std::chrono::milliseconds(0))));
    gate.set_ceiling(interval(std::chrono::milliseconds(50)));
    require(*gate.deadline().value() == at(std::chrono::milliseconds(300)),
            "a new ceiling leaves the wait under way");
}

void test_names() {
    require(std::string_view(gui_forms::first_frame_show_cause_name(FirstFrameShowCause::deadline)) == "deadline",
            "the deadline cause's name");
    require(std::string_view(gui_forms::first_frame_show_state_name(FirstFrameShowState::waiting)) == "waiting",
            "the waiting state's name");
}

} // namespace

int main() {
    try {
        test_default_ceiling_is_the_owners();
        test_first_frame_before_the_deadline_shows();
        test_deadline_shows_without_a_frame();
        test_late_wake_shows_once();
        test_close_before_show_cancels();
        test_hide_then_show_again_waits_anew();
        test_frame_before_request_shows_at_once();
        test_zero_ceiling_turns_the_gate_off();
        test_later_shows_are_at_once();
        test_repeated_request_keeps_the_first_deadline();
        test_reentrant_report_shows_once();
        test_ceiling_is_clamped();
        test_names();
    } catch (const std::exception& error) {
        std::cerr << "first-frame show tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "first-frame show gate: frame, deadline, cancel and clamping hold\n";
    return EXIT_SUCCESS;
}
