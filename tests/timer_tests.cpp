#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/named_callbacks.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    Fixture() : window(root, {160.0, 90.0}) {
        (*root).set_requested_bounds({0.0, 0.0, 160.0, 90.0});
        window.perform_layout();
        static_cast<void>(window.take_damage());
    }

    Control::Ptr root = make_control<Control>(StableId("timer.root"));
    Window window;
};

class RestartControllerAndDisposeVictim final {
public:
    RestartControllerAndDisposeVictim(Timer& controller, Timer& victim,
                                      int& controller_ticks,
                                      FrameTime restart_deadline)
        : controller_(controller), victim_(victim),
          controller_ticks_(controller_ticks),
          restart_deadline_(restart_deadline) {}

    void operator()() const {
        ++controller_ticks_;
        victim_.dispose();
        controller_.stop();
        controller_.start_at(restart_deadline_);
    }

private:
    Timer& controller_;
    Timer& victim_;
    int& controller_ticks_;
    FrameTime restart_deadline_;
};

class StopTimerOffThread final {
public:
    StopTimerOffThread(Timer& timer, std::atomic<bool>& rejected)
        : timer_(timer), rejected_(rejected) {}

    void operator()() const {
        try {
            timer_.stop();
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Timer& timer_;
    std::atomic<bool>& rejected_;
};

void test_disabled_timer_is_idle_and_exact() {
    Fixture fixture;
    Timer timer(fixture.window, 10ms);
    int ticks = 0;
    SubscriptionToken subscription = timer.tick().subscribe(
        test_support::IncrementCounter<int>(ticks));
    require(!timer.enabled() && !fixture.window.next_wake(),
            "a newly constructed Timer must not keep the UI loop awake");

    const FrameTime base{};
    timer.start_at(base + 10ms);
    require(timer.enabled() && fixture.window.next_wake() == base + 10ms,
            "deterministic Timer start must publish its exact first deadline");
    require(fixture.window.poll_frame_schedule(base + 9ms).ui_timer_ticks == 0U &&
                ticks == 0,
            "Timer must not tick early");
    const FramePollResult due = fixture.window.poll_frame_schedule(base + 10ms);
    require(due.ui_timer_ticks == 1U && ticks == 1 &&
                due.next_wake == base + 20ms,
            "Timer must tick once and retain its cadence");
    timer.stop();
    require(!timer.enabled() && !fixture.window.next_wake(),
            "stopping the last Timer must return the UI loop to idle");
}

void test_registration_order_and_late_coalescing() {
    Fixture fixture;
    Timer first(fixture.window, 10ms);
    Timer second(fixture.window, 10ms);
    std::vector<int> order;
    SubscriptionToken first_subscription = first.tick().subscribe(
        test_support::PushConstant<std::vector<int>, int>(order, 1));
    SubscriptionToken second_subscription = second.tick().subscribe(
        test_support::PushConstant<std::vector<int>, int>(order, 2));
    const FrameTime base{};
    first.start_at(base + 10ms);
    second.start_at(base + 10ms);

    const FramePollResult late = fixture.window.poll_frame_schedule(base + 35ms);
    require(order == std::vector<int>({1, 2}) && late.ui_timer_ticks == 2U &&
                late.coalesced_requests == 4U &&
                late.next_wake == base + 40ms,
            "late timers must fire once in registration order and skip catch-up bursts");
}

void test_callback_stop_restart_and_disposal_are_snapshot_safe() {
    Fixture fixture;
    Timer controller(fixture.window, 10ms);
    Timer victim(fixture.window, 10ms);
    int controller_ticks = 0;
    int victim_ticks = 0;
    const FrameTime base{};
    SubscriptionToken controller_subscription = controller.tick().subscribe(
        RestartControllerAndDisposeVictim(
            controller, victim, controller_ticks, base + 100ms));
    SubscriptionToken victim_subscription = victim.tick().subscribe(
        test_support::IncrementCounter<int>(victim_ticks));
    controller.start_at(base + 10ms);
    victim.start_at(base + 10ms);

    const FramePollResult first = fixture.window.poll_frame_schedule(base + 10ms);
    require(controller_ticks == 1 && victim_ticks == 0 && victim.is_disposed() &&
                first.ui_timer_ticks == 1U && first.next_wake == base + 100ms,
            "a callback must be able to stop/restart itself and dispose a later timer");
    static_cast<void>(fixture.window.poll_frame_schedule(base + 100ms));
    require(controller_ticks == 2 && victim_ticks == 0,
            "the restarted deadline must fire without reviving disposed work");
}

void test_occlusion_does_not_suspend_component_time() {
    Fixture fixture;
    Timer timer(fixture.window, 25ms);
    int ticks = 0;
    SubscriptionToken subscription = timer.tick().subscribe(
        test_support::IncrementCounter<int>(ticks));
    const FrameTime base{};
    timer.start_at(base + 25ms);
    fixture.window.set_occluded(true, base);
    require(fixture.window.next_wake() == base + 25ms,
            "UI component timers must remain scheduled while rendering is occluded");
    const FramePollResult result = fixture.window.poll_frame_schedule(base + 25ms);
    require(result.suppressed_by_occlusion && result.ui_timer_ticks == 1U &&
                ticks == 1 && result.next_wake == base + 50ms,
            "occlusion must suppress rendering work, not UI component callbacks");
}

void test_window_shutdown_revokes_timer_and_thread_is_enforced() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("timer.shutdown.root"));
    std::unique_ptr<gui_forms::Window> window = std::make_unique<Window>(root, Size{80.0, 40.0});
    Timer timer(*window, 10ms);
    int ticks = 0;
    SubscriptionToken subscription = timer.tick().subscribe(
        test_support::IncrementCounter<int>(ticks));
    timer.start_at(FrameTime{} + 10ms);

    std::atomic<bool> rejected{false};
    std::thread foreign(StopTimerOffThread(timer, rejected));
    foreign.join();
    require(rejected && timer.enabled(),
            "Timer mutation from a foreign thread must be rejected without state change");

    window.reset();
    require(!timer.enabled() && ticks == 0,
            "Window shutdown must revoke Timer work without a post-shutdown callback");
    bool rejected_after_shutdown = false;
    try {
        timer.start();
    } catch (const std::logic_error&) {
        rejected_after_shutdown = true;
    }
    require(rejected_after_shutdown,
            "a Timer must reject reuse after its owning Window has shut down");
    timer.dispose();
}

} // namespace

int main() {
    try {
        test_disabled_timer_is_idle_and_exact();
        test_registration_order_and_late_coalescing();
        test_callback_stop_restart_and_disposal_are_snapshot_safe();
        test_occlusion_does_not_suspend_component_time();
        test_window_shutdown_revokes_timer_and_thread_is_enforced();
        std::cout << "timer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "timer tests failed: " << error.what() << '\n';
        return 1;
    }
}
