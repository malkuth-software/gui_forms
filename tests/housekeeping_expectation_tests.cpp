// Housekeeping expectations (owner, 2026-11-11): the scheduler wake, paint
// ticks and damage collection are checked against what they should have
// produced and how long they should have taken. A missed expectation is
// counted in the metrics snapshot and printed as one rate-limited telemetry
// line; nothing throws and nothing terminates.

#include "gui_forms/gui_forms.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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

// A UI timer target that counts its calls.
class CountingTick final {
public:
    explicit CountingTick(std::uint64_t& calls) : calls_(&calls) {}
    void operator()(FrameTime) const { ++(*calls_); }

private:
    std::uint64_t* calls_;
};

// A control whose paint takes at least `delay` (the injected slow tick).
class SlowControl final : public Control {
public:
    explicit SlowControl(StableId stable_id) : Control(std::move(stable_id)) {}
    std::chrono::milliseconds delay{0};

protected:
    void on_paint(Painter& painter, Rect damage) override {
        painter.fill_rect(damage, Color{0x20, 0x40, 0x60, 0xff});
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
    }
};

struct Fixture final {
    std::shared_ptr<Control> root;
    std::shared_ptr<SlowControl> slow;
    std::unique_ptr<Window> window;
};

std::unique_ptr<Fixture> make_fixture() {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).root = make_control<Control>(StableId("expectations.root"));
    (*fixture).slow = make_control<SlowControl>(StableId("expectations.slow"));
    (*(*fixture).root).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
    (*(*fixture).slow).set_requested_bounds({10.0, 10.0, 80.0, 40.0});
    (*(*fixture).root).add_child((*fixture).slow);
    (*fixture).window = std::make_unique<Window>((*fixture).root, Size{200.0, 120.0});
    NullPainter painter;
    static_cast<void>((*(*fixture).window).paint(painter, (*(*fixture).window).take_damage().bounds()));
    (*(*fixture).window).reset_activity_metrics();
    return fixture;
}

std::uint64_t all_misses(const MetricsSnapshot& metrics) {
    const std::uint64_t total = metrics.wake_expectation_misses + metrics.paint_budget_misses +
        metrics.live_drain_misses + metrics.damage_bound_misses;
    return total;
}

void normal_run_reports_nothing() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    const FrameTime due = FrameTime{} + 33ms;
    std::uint64_t ticks = 0U;
    FrameRequestToken timer = window.schedule_ui_timer(*(*fixture).slow, 33ms, due, CountingTick{ticks});
    static_cast<void>(window.poll_frame_schedule(due));
    static_cast<void>(window.poll_frame_schedule(due + 33ms));
    (*(*fixture).slow).invalidate(Dirty::paint);
    NullPainter painter;
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(ticks == 2U && timer.connected(), "the normal run's timer ticks twice");
    require(all_misses(metrics) == 0U && metrics.expectation_telemetry_records == 0U,
            "a normal run misses no expectation and prints no telemetry");
}

void missed_wake_reports_once() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    const FrameTime due = FrameTime{} + 16ms;
    std::uint64_t ticks = 0U;
    FrameRequestToken timer = window.schedule_ui_timer(*(*fixture).slow, 16ms, due, CountingTick{ticks});
    // The wake comes half a second after the deadline: the frame it was
    // scheduled for was missed.
    static_cast<void>(window.poll_frame_schedule(due + 500ms));
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(ticks == 1U && timer.connected(), "a late wake still ticks the timer once");
    require(metrics.wake_expectation_misses == 1U && all_misses(metrics) == 1U,
            "a late wake misses wake_on_time once");
    require(metrics.expectation_telemetry_records == 1U, "and prints one telemetry record");
    require(metrics.to_json().find("\"wake_expectation_misses\":1") != std::string::npos,
            "the miss is queryable in the snapshot's JSON");
}

void slow_tick_reports_once() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ExpectationThresholds thresholds{};
    thresholds.paint_budget_nanoseconds = 5'000'000U;
    window.metrics().set_expectation_thresholds(thresholds);
    (*(*fixture).slow).delay = 20ms;
    (*(*fixture).slow).invalidate(Dirty::paint);
    NullPainter painter;
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.paint_budget_misses == 1U && all_misses(metrics) == 1U,
            "a paint tick over its budget misses paint_within_budget once");
    require(metrics.expectation_telemetry_records == 1U, "and prints one telemetry record");
}

void telemetry_is_rate_limited() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    const FrameTime due = FrameTime{} + 16ms;
    std::uint64_t ticks = 0U;
    FrameRequestToken timer = window.schedule_ui_timer(*(*fixture).slow, 16ms, due, CountingTick{ticks});
    FrameTime now = due;
    for (int wake = 0; wake < 9; ++wake) {
        now += 500ms;
        static_cast<void>(window.poll_frame_schedule(now));
    }
    const MetricsSnapshot metrics = window.metrics_snapshot();
    // Misses 1, 2, 4 and 8 print; the other five are counted only.
    require(metrics.wake_expectation_misses == 9U && metrics.expectation_telemetry_records == 4U,
            "telemetry prints the 1st, 2nd, 4th and 8th miss");
    require(timer.connected() && ticks == 9U, "every late wake still ticks");
}

void damage_overflow_reports() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    std::vector<std::shared_ptr<Control>> cells;
    for (std::size_t index = 0U; index < 90U; ++index) {
        std::shared_ptr<Control> cell = make_control<Control>(StableId("expectations.cell." + std::to_string(index)));
        const double x = static_cast<double>(index % 15U) * 13.0;
        const double y = static_cast<double>(index / 15U) * 19.0;
        (*cell).set_requested_bounds({x, y, 4.0, 4.0});
        (*(*fixture).root).add_child(cell);
        cells.push_back(cell);
    }
    NullPainter painter;
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    window.reset_activity_metrics();
    for (const std::shared_ptr<Control>& cell : cells) (*cell).invalidate(Dirty::paint);
    static_cast<void>(window.take_damage());
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.damage_bound_misses >= 1U, "damage outgrowing its rectangle capacity misses damage_bounded");
    require(metrics.paint_budget_misses == 0U && metrics.wake_expectation_misses == 0U,
            "and no other expectation");
}

} // namespace

int main() {
    try {
        normal_run_reports_nothing();
        missed_wake_reports_once();
        slow_tick_reports_once();
        telemetry_is_rate_limited();
        damage_overflow_reports();
    } catch (const std::exception& error) {
        std::cerr << "housekeeping expectation test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "housekeeping expectations: 5 checks passed\n";
    return EXIT_SUCCESS;
}
