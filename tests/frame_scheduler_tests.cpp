#include "gui_forms/gui_forms.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

// A UI timer target that counts its calls and throws on each.
class ThrowingTick final {
public:
    explicit ThrowingTick(std::uint64_t& calls) : calls_(&calls) {}

    void operator()(FrameTime) const {
        ++(*calls_);
        throw std::runtime_error("intentional frame callback fault");
    }

private:
    std::uint64_t* calls_;
};

// A UI timer target that counts its calls.
class CountingTick final {
public:
    explicit CountingTick(std::uint64_t& calls) : calls_(&calls) {}

    void operator()(FrameTime) const { ++(*calls_); }

private:
    std::uint64_t* calls_;
};

// A UI timer target that repaints its control, as an animated control's
// own timer does (ADR-016).
class InvalidatingTick final {
public:
    explicit InvalidatingTick(const Control::Ptr& target) : target_(target) {}

    void operator()(FrameTime) const {
        const Control::Ptr live = target_.lock();
        if (live) (*live).invalidate(Dirty::paint);
    }

private:
    Control::WeakPtr target_;
};

struct Fixture {
    Fixture() {
        (*root).set_requested_bounds({0.0, 0.0, 160.0, 90.0});
        window = std::make_unique<Window>(root, Size{160.0, 90.0});
        (*window).perform_layout();
        DamageRegion initial = (*window).take_damage();
        (*window).paint(painter, initial.bounds());
        (*window).reset_activity_metrics();
    }

    void consume_frame() {
        DamageRegion damage = (*window).take_damage();
        if (!damage.empty()) {
            (*window).paint(painter, damage.bounds());
        }
    }

    Control::Ptr root = make_control<Control>(StableId("scheduler.root"));
    std::unique_ptr<Window> window;
    NullPainter painter;
};

// The controller timer's target: cancels a peer due in the same poll,
// schedules a new timer due now and polls again from inside the callback.
class ScheduleDuringFrameCallback final {
public:
    ScheduleDuringFrameCallback(FrameRequestToken& cancelled_request,
                                FrameRequestToken& next_request,
                                Window& window,
                                Control& owner,
                                std::uint64_t& next_calls,
                                std::uint64_t& controller_calls,
                                FramePollResult& nested)
        : cancelled_request_(&cancelled_request), next_request_(&next_request),
          window_(&window), owner_(&owner), next_calls_(&next_calls),
          controller_calls_(&controller_calls), nested_(&nested) {}

    void operator()(FrameTime now) const {
        ++(*controller_calls_);
        (*cancelled_request_).disconnect();
        *next_request_ = (*window_).schedule_ui_timer(
            *owner_, 33ms, now, CountingTick{*next_calls_});
        *nested_ = (*window_).poll_frame_schedule(now);
    }

private:
    FrameRequestToken* cancelled_request_;
    FrameRequestToken* next_request_;
    Window* window_;
    Control* owner_;
    std::uint64_t* next_calls_;
    std::uint64_t* controller_calls_;
    FramePollResult* nested_;
};

class SchedulePaintOffThread final {
public:
    SchedulePaintOffThread(Window& window, Control::Ptr root,
                           std::atomic<bool>& rejected)
        : window_(window), root_(std::move(root)), rejected_(rejected) {}

    void operator()() const {
        try {
            static_cast<void>(window_.schedule_paint(root_, FrameTime{}));
        } catch (const std::logic_error&) {
            rejected_.store(true, std::memory_order_relaxed);
        }
    }

private:
    Window& window_;
    Control::Ptr root_;
    std::atomic<bool>& rejected_;
};

void test_deadline_is_exact_and_one_shot() {
    Fixture fixture;
    const FrameTime base{};
    FrameRequestToken deadline =
        (*fixture.window).schedule_paint(fixture.root, base + 50ms);
    require(deadline.connected() && (*fixture.window).next_wake() == base + 50ms &&
                !(*fixture.window).needs_frame(),
            "a future deadline must request a wake without immediate paint");

    FramePollResult early = (*fixture.window).poll_frame_schedule(base + 49ms);
    require(early.deadlines_fired == 0 && !early.damage_pending &&
                early.next_wake == base + 50ms && deadline.connected(),
            "polling before a deadline must preserve it exactly");

    FramePollResult due = (*fixture.window).poll_frame_schedule(base + 50ms);
    require(due.deadlines_fired == 1 && due.damage_pending &&
                !due.next_wake.has_value() && !deadline.connected(),
            "a due one-shot deadline must invalidate once and disconnect");
    fixture.consume_frame();
    require(!(*fixture.window).needs_frame(),
            "painting deadline damage must return the scheduler to idle");
}

void test_same_target_deadlines_coalesce() {
    Fixture fixture;
    const FrameTime due = FrameTime{} + 10ms;
    FrameRequestToken first = (*fixture.window).schedule_paint(fixture.root, due);
    FrameRequestToken second = (*fixture.window).schedule_paint(fixture.root, due);
    const FramePollResult poll = (*fixture.window).poll_frame_schedule(due);
    require(poll.deadlines_fired == 2 && poll.coalesced_requests == 1,
            "same-target deadlines must produce one retained invalidation");
    require(!first.connected() && !second.connected(),
            "all fired one-shot deadline tokens must disconnect");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.scheduled_frame_requests == 2 && metrics.scheduler_wakes == 1 &&
                metrics.frame_deadlines_fired == 2 &&
                metrics.frame_requests_coalesced == 1,
            "deadline coalescing metrics must remain truthful");
}

void test_ui_timer_skips_catch_up_bursts() {
    Fixture fixture;
    const FrameTime first = FrameTime{} + 100ms;
    constexpr FrameInterval interval = 33ms;
    std::uint64_t calls = 0U;
    FrameRequestToken timer = (*fixture.window).schedule_ui_timer(
        *fixture.root, interval, first, CountingTick{calls});
    require(timer.connected() && (*fixture.window).next_wake() == first,
            "a UI timer must expose one bounded wake");

    FramePollResult initial = (*fixture.window).poll_frame_schedule(first);
    require(initial.ui_timer_ticks == 1 && initial.coalesced_requests == 0 &&
                initial.next_wake == first + interval && calls == 1U,
            "the first timer tick must advance by one interval");

    const FrameTime late = first + interval * 10;
    FramePollResult delayed = (*fixture.window).poll_frame_schedule(late);
    require(delayed.ui_timer_ticks == 1 && delayed.coalesced_requests == 9 &&
                delayed.next_wake == late + interval && calls == 2U,
            "late timer polling must emit one tick and skip missed intervals");
    timer.disconnect();
    static_cast<void>((*fixture.window).poll_frame_schedule(late));
    require(!(*fixture.window).next_wake().has_value() &&
                !(*fixture.window).needs_frame(),
            "disconnecting the timer must restore quiescence");
}

void test_throwing_frame_callback_isolated_and_disconnected() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("scheduler.fault.root"));
    std::shared_ptr<gui_forms::Control> throwing = make_control<Control>(StableId("scheduler.fault.throwing"));
    std::shared_ptr<gui_forms::Control> healthy = make_control<Control>(StableId("scheduler.fault.healthy"));
    (*root).set_requested_bounds({0.0, 0.0, 160.0, 90.0});
    (*throwing).set_requested_bounds({0.0, 0.0, 70.0, 40.0});
    (*healthy).set_requested_bounds({80.0, 0.0, 70.0, 40.0});
    (*root).add_child(throwing);
    (*root).add_child(healthy);
    Window window(root, {160.0, 90.0});
    NullPainter painter;
    window.paint(painter, window.take_damage().bounds());
    window.reset_activity_metrics();
    const FrameTime due = FrameTime{} + 33ms;
    std::uint64_t bad_calls = 0U;
    std::uint64_t good_calls = 0U;
    FrameRequestToken bad = window.schedule_ui_timer(*throwing, 33ms, due, ThrowingTick{bad_calls});
    FrameRequestToken good = window.schedule_ui_timer(*healthy, 33ms, due, CountingTick{good_calls});

    const FramePollResult result = window.poll_frame_schedule(due);
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(result.ui_timer_ticks == 2U &&
                result.callback_faults == 1U &&
                bad_calls == 1U && good_calls == 1U && !bad.connected() &&
                good.connected() && metrics.frame_callback_faults == 1U,
            "one throwing timer callback must disconnect itself, report the fault, and preserve a healthy peer");
    const FramePollResult later = window.poll_frame_schedule(due + 33ms);
    require(later.callback_faults == 0U && later.ui_timer_ticks == 1U &&
                bad_calls == 1U && good_calls == 2U && good.connected(),
            "a faulted timer must not crash or retry on later native timer turns");
}

void test_frame_callbacks_use_fixed_due_set_and_defer_nested_poll() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("scheduler.reentry.root"));
    Window window(root, {160.0, 90.0});
    const FrameTime due = FrameTime{} + 20ms;
    FrameRequestToken cancelled_request;
    FrameRequestToken next_request;
    FramePollResult nested;
    std::uint64_t controller_calls = 0U;
    std::uint64_t cancelled_calls = 0U;
    std::uint64_t next_calls = 0U;
    FrameRequestToken controller_request = window.schedule_ui_timer(
        *root, 33ms, due,
        ScheduleDuringFrameCallback(cancelled_request, next_request, window, *root,
                                    next_calls, controller_calls, nested));
    cancelled_request = window.schedule_ui_timer(*root, 33ms, due, CountingTick{cancelled_calls});

    const FramePollResult first = window.poll_frame_schedule(due);
    require(first.ui_timer_ticks == 1U && controller_calls == 1U &&
                cancelled_calls == 0U && next_calls == 0U &&
                nested.reentrant_poll_deferred && next_request.connected() &&
                first.next_wake == due,
            "timer callbacks must cancel later peers and admit new due work only to the next poll");
    const FramePollResult second = window.poll_frame_schedule(due);
    require(second.ui_timer_ticks == 1U && next_calls == 1U && controller_calls == 1U &&
                second.next_wake == due + 33ms &&
                window.metrics_snapshot().reentrant_frame_polls_deferred == 1U,
            "a deferred nested poll must preserve one next-turn delivery and diagnostic");
    controller_request.disconnect();
    next_request.disconnect();
}

void test_owner_disposal_revokes_ui_timer() {
    Fixture fixture;
    std::uint64_t calls = 0U;
    FrameRequestToken timer = (*fixture.window).schedule_ui_timer(
        *fixture.root, 33ms, FrameTime{} + 33ms, CountingTick{calls});
    (*fixture.root).dispose();
    require(!timer.connected(),
            "component disposal must synchronously revoke its UI timer");
    static_cast<void>((*fixture.window).poll_frame_schedule(FrameTime{} + 33ms));
    require(!(*fixture.window).next_wake().has_value() && calls == 0U,
            "a disposed owner's timer must leave no latent wake");
}

void test_occlusion_holds_paint_deadline() {
    Fixture fixture;
    const FrameTime base{};
    FrameRequestToken deadline = (*fixture.window).schedule_paint(
        fixture.root, base + 100ms);

    (*fixture.window).set_occluded(true, base + 90ms);
    require((*fixture.window).occluded() && !(*fixture.window).next_wake().has_value() &&
                deadline.connected(),
            "occlusion must suppress wakes without revoking a paint deadline");
    const FramePollResult hidden =
        (*fixture.window).poll_frame_schedule(base + 500ms);
    require(hidden.suppressed_by_occlusion && hidden.deadlines_fired == 0 &&
                !hidden.next_wake.has_value() && deadline.connected(),
            "polling while occluded must not fire or disconnect retained work");

    (*fixture.window).set_occluded(false, base + 500ms);
    require(!(*fixture.window).occluded() &&
                (*fixture.window).next_wake() == base + 100ms,
            "resume must expose the overdue deadline once");
    const FramePollResult resumed =
        (*fixture.window).poll_frame_schedule(base + 500ms);
    require(resumed.deadlines_fired == 1 && resumed.damage_pending &&
                !resumed.next_wake.has_value() && !deadline.connected(),
            "resume must deliver the held deadline as one paint");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.occlusion_suspensions == 1 && metrics.occlusion_resumes == 1 &&
                metrics.occluded_frame_polls == 1 &&
                metrics.frame_deadlines_fired == 1,
            "occlusion and resumed work must remain explicitly accounted");
}

void test_thirty_tick_band_stays_localized() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("band.root"));
    std::shared_ptr<gui_forms::Control> band = make_control<Control>(StableId("band.active"));
    std::shared_ptr<gui_forms::Control> sibling = make_control<Control>(StableId("band.sibling"));
    (*root).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    (*band).set_requested_bounds({10.0, 20.0, 60.0, 10.0});
    (*sibling).set_requested_bounds({120.0, 20.0, 60.0, 10.0});
    (*root).add_child(band);
    (*root).add_child(sibling);
    Window window(root, {200.0, 100.0});
    NullPainter painter;
    window.perform_layout();
    DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());
    window.reset_activity_metrics();

    constexpr FrameInterval interval = std::chrono::nanoseconds(33'333'333);
    const FrameTime base{};
    FrameRequestToken timer =
        window.schedule_ui_timer(*band, interval, base + interval, InvalidatingTick{band});
    for (int tick = 1; tick <= 30; ++tick) {
        const FramePollResult poll =
            window.poll_frame_schedule(base + interval * tick);
        require(poll.ui_timer_ticks == 1 && poll.coalesced_requests == 0,
                "an on-cadence band timer must emit exactly one tick");
        DamageRegion damage = window.take_damage();
        require(damage.bounds() == Rect{10.0, 20.0, 60.0, 10.0},
                "band damage must remain at its exact arranged bounds");
        window.paint(painter, damage.bounds());
    }

    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.display_chunks_rebuilt == 30 &&
                metrics.paint_invalidations_consumed == 30,
            "30 band ticks must account for 30 localized chunk rebuilds");
    require(metrics.partial_paints == 30 && metrics.full_window_paints == 0 &&
                metrics.painted_damage_area == 18'000.0,
            "30 band ticks must never expand the 600-pixel band damage");
    timer.disconnect();
    static_cast<void>(window.poll_frame_schedule(base + interval * 30));
    require(!window.next_wake().has_value() && !window.needs_frame(),
            "completed 30-tick workload must return to idle");
}

void test_ui_timer_bounds_and_thread_affinity() {
    Fixture fixture;
    bool cadence_rejected = false;
    std::uint64_t calls = 0U;
    try {
        static_cast<void>((*fixture.window).schedule_ui_timer(
            *fixture.root, minimum_ui_timer_interval - 1ns, FrameTime{}, CountingTick{calls}));
    } catch (const std::invalid_argument&) {
        cadence_rejected = true;
    }
    require(cadence_rejected, "a sub-bound timer cadence must be rejected");

    std::atomic<bool> rejected{false};
    std::thread worker(SchedulePaintOffThread(
        *fixture.window, fixture.root, rejected));
    worker.join();
    require(rejected.load(std::memory_order_relaxed) &&
                (*fixture.window).metrics_snapshot().rejected_wrong_thread_operations == 1,
            "wrong-thread scheduling must reject without adding a request");
}

} // namespace

int main() {
    try {
        test_deadline_is_exact_and_one_shot();
        test_same_target_deadlines_coalesce();
        test_ui_timer_skips_catch_up_bursts();
        test_throwing_frame_callback_isolated_and_disconnected();
        test_frame_callbacks_use_fixed_due_set_and_defer_nested_poll();
        test_owner_disposal_revokes_ui_timer();
        test_occlusion_holds_paint_deadline();
        test_thirty_tick_band_stays_localized();
        test_ui_timer_bounds_and_thread_affinity();
        std::cout << "gui_forms_frame_scheduler_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_frame_scheduler_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
