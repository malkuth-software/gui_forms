// The public drag client (DragTracker, cp29 phase 2b): the state machine's
// transitions (armed, dragging, the release, a click before the threshold)
// and every cancelling end (Escape armed and dragging, capture loss,
// deactivation, cancel(), refused plans); the press threshold on either
// axis and its bad values; axis locks (fixed and dominant), the grid and
// its toggle, and the bounds; drop-target hit-testing (containment, the
// earlier of overlapping targets, the magnet radius, suppression, release
// off every target with and without free_drop); reorder and value plans;
// auto-scroll's velocity bounds and its frame timer stopping at the
// content's end, with the plan redone against the scrolled content;
// the magnet's hysteresis (capture, hold within the release radius,
// release); reentrancy: a target withdrawn during the drag (outside a
// callback, inside one, and in a keyboard session, where it is announced,
// stepped past, and a release on it is rewritten to rejected), cancel and
// press from inside a callback, the owner detached mid-drag and the tracker
// destroyed mid-drag. Only public headers: the
// window's drag engine is reached through the tracker.
// Headless: a window that is never shown.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/drag/drag_tracker/drag_tracker.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/routed_input_probe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-6;
    return result;
}

// The id no test target has: the canvas withdraws nothing for it.
constexpr std::uint32_t no_target = 0xFFFFFFFFU;

// A canvas that drags one 10 by 10 subject whose top-left is `subject`, in
// content coordinates scrolled by `offset` (the window shows content point
// p at p - offset). Its plan copies `terms` and writes the targets and
// slots, moved into the window's coordinates. It records every plan,
// preview, end and announcement, and can misbehave on cue from inside a
// preview (withdraw, cancel, press).
class Canvas final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    explicit Canvas(StableId id) : gui_forms::tests::RoutedInputProbe<Control>(std::move(id)), tracker(*this) {
        tracker.callbacks.plan = Delegate<DragPlan&>::bind<Canvas, &Canvas::plan_drag>(*this);
        tracker.callbacks.preview = Delegate<const DragTrackerFrame&>::bind<Canvas, &Canvas::preview_drag>(*this);
        tracker.callbacks.finished = Delegate<const DragTrackerFrame&>::bind<Canvas, &Canvas::finish_drag>(*this);
        tracker.callbacks.announce = Delegate<DragAnnouncementRequest&>::bind<Canvas, &Canvas::announce_drag>(*this);
        tracker.callbacks.scroll = Delegate<DragAutoScrollStep&>::bind<Canvas, &Canvas::scroll_by>(*this);
        previews.reserve(4096U);
        plans.reserve(256U);
        steps.reserve(1024U);
    }

    void routed_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down && event.button == PointerButton::primary) {
            DragStart start{};
            start.threshold = threshold;
            start.source = subject_rect();
            last_start = tracker.press(event, start);
            event.handled = last_start == DragStartResult::armed || last_start == DragStartResult::started;
            return;
        }
        last_taken = tracker.pointer(event);
        if (last_taken) event.handled = true;
    }

    // The subject's rectangle in the window's coordinates.
    [[nodiscard]] Rect subject_rect() const {
        const Rect rect{subject.x - offset.x, subject.y - offset.y, 10.0, 10.0};
        return rect;
    }

    // -- configuration --
    double threshold{default_drag_threshold};
    DragPlan terms{};
    std::vector<DragTarget> targets{};
    std::vector<Rect> slots{};
    Point subject{95.0, 95.0};
    Point offset{};
    Point offset_limit{};
    std::uint32_t withdraw_in_preview{no_target};
    bool cancel_in_preview{};
    bool press_in_preview{};
    bool detach_hook{true};

    // -- records --
    DragStartResult last_start{DragStartResult::bad_input};
    DragStartResult reentrant_start{DragStartResult::started};
    bool last_taken{};
    std::vector<DragPlan> plans{};
    std::vector<DragTrackerFrame> previews{};
    std::vector<DragTrackerFrame> ends{};
    std::vector<DragTrackerFrame> announced{};
    std::vector<DragAnnouncementReason> reasons{};
    std::vector<std::uint32_t> withdrawn_ids{};
    std::vector<DragAutoScrollStep> steps{};

    DragTracker tracker;

protected:
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override {
        if (detach_hook) tracker.owner_detaching(former_window);
        Control::on_detaching_from_window(former_window);
    }

private:
    void plan_drag(DragPlan& plan) {
        const DragPlan inputs = plan;
        plan = terms;
        plan.key = inputs.key;
        plan.press = inputs.press;
        plan.source = inputs.source;
        plan.mode = inputs.mode;
        plan.epoch = inputs.epoch;
        plan.travel = inputs.travel;
        shown_targets_.clear();
        for (const DragTarget& target : targets) {
            DragTarget shown = target;
            shown.bounds.x -= offset.x;
            shown.bounds.y -= offset.y;
            shown_targets_.push_back(shown);
        }
        plan.placement.targets = std::span<const DragTarget>(shown_targets_.data(), shown_targets_.size());
        plan.reorder.slots = std::span<const Rect>(slots.data(), slots.size());
        plans.push_back(plan);
    }

    void preview_drag(const DragTrackerFrame& frame) {
        previews.push_back(frame);
        if (withdraw_in_preview != no_target && frame.hit == DragHit::target && frame.target == withdraw_in_preview) {
            static_cast<void>(tracker.withdraw_target(withdraw_in_preview));
        }
        if (cancel_in_preview) tracker.cancel();
        if (press_in_preview) {
            PointerEvent again{};
            again.action = PointerAction::down;
            again.button = PointerButton::primary;
            again.position = frame.pointer;
            const DragStart start{};
            reentrant_start = tracker.press(again, start);
        }
    }

    void finish_drag(const DragTrackerFrame& frame) { ends.push_back(frame); }

    void announce_drag(DragAnnouncementRequest& request) {
        announced.push_back(request.frame);
        reasons.push_back(request.reason);
        withdrawn_ids.push_back(request.withdrawn);
        const bool gone = request.reason == DragAnnouncementReason::target_withdrawn;
        request.text = gone ? std::string_view("target no longer available") : std::string_view("moving");
    }

    void scroll_by(DragAutoScrollStep& step) {
        const double x = std::clamp(offset.x + step.requested.x, 0.0, offset_limit.x);
        const double y = std::clamp(offset.y + step.requested.y, 0.0, offset_limit.y);
        step.applied = Point{x - offset.x, y - offset.y};
        offset = Point{x, y};
        steps.push_back(step);
    }

    std::vector<DragTarget> shown_targets_{};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Canvas> canvas;
    std::unique_ptr<Window> window;
};

// A 400 by 400 window filled by the canvas.
Fixture make_fixture() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("tracker.root"));
    fixture.canvas = make_control<Canvas>(StableId("tracker.canvas"));
    (*fixture.canvas).set_requested_bounds({0.0, 0.0, 400.0, 400.0});
    (*fixture.root).add_child(fixture.canvas);
    fixture.window = std::make_unique<Window>(fixture.root, Size{400.0, 400.0});
    (*fixture.window).perform_layout();
    return fixture;
}

void pointer(Window& window, PointerAction action, Point position, Modifier modifiers = Modifier::none) {
    PointerEvent event{};
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    event.position = position;
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(event));
}

void key(Window& window, std::uint32_t physical_key) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    static_cast<void>(window.dispatch_key(event));
}

// The subject's centre at the default place: a press there keeps the grab
// at the centre.
constexpr Point press_at{100.0, 100.0};

// -- the state machine ------------------------------------------------------------

void transitions_and_threshold() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    DragTracker& tracker = canvas.tracker;
    require(tracker.phase() == DragPhase::idle && !tracker.running(), "a new tracker is idle");

    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::armed && tracker.phase() == DragPhase::armed,
            "a press arms the session");
    require(canvas.plans.empty(), "an armed session has not planned");
    pointer(window, PointerAction::move, {103.9, 103.9});
    require(tracker.phase() == DragPhase::armed && canvas.plans.empty(),
            "travel under the threshold on both axes stays armed");
    pointer(window, PointerAction::move, {104.0, 100.0});
    require(tracker.phase() == DragPhase::dragging && canvas.plans.size() == 1U,
            "the threshold on one axis plans once");
    require(canvas.plans[0].epoch == 1U && near(canvas.plans[0].travel.x, 4.0) && near(canvas.plans[0].travel.y, 0.0),
            "the plan sees its epoch and the travel");
    require(!canvas.previews.empty() && canvas.previews.back().hit == DragHit::point &&
                near(canvas.previews.back().point.x, 99.0) && near(canvas.previews.back().point.y, 95.0),
            "the first dragging frame previews the placement");
    pointer(window, PointerAction::up, {110.0, 100.0});
    require(canvas.ends.size() == 1U && canvas.ends[0].end == DragEnd::free && canvas.ends[0].hit == DragHit::point &&
                near(canvas.ends[0].point.x, 105.0),
            "the release ends free at the placement");
    require(drag_end_commits(canvas.ends[0].end) && tracker.phase() == DragPhase::idle &&
                tracker.statistics().commits == 1U,
            "a free end commits and leaves the tracker idle");

    // A release before the threshold is the press's click: no plan, no end.
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::up, {101.0, 100.0});
    require(!canvas.last_taken && canvas.ends.size() == 1U && canvas.plans.size() == 1U &&
                tracker.phase() == DragPhase::idle,
            "a click before the threshold never plans and is not taken");

    // A zero threshold plans at the press.
    canvas.threshold = 0.0;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::started && tracker.phase() == DragPhase::dragging &&
                canvas.plans.size() == 2U,
            "a zero threshold plans at once");
    key(window, PhysicalKey::escape);

    // Bad thresholds change nothing.
    canvas.threshold = std::numeric_limits<double>::quiet_NaN();
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::bad_input && tracker.phase() == DragPhase::idle,
            "a non-finite threshold is bad input");
    canvas.threshold = -1.0;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::bad_input && tracker.phase() == DragPhase::idle,
            "a negative threshold is bad input");

    // A press that is not a down is bad input; a second press while running is busy.
    PointerEvent moved{};
    moved.action = PointerAction::move;
    moved.position = press_at;
    require(tracker.press(moved, DragStart{}) == DragStartResult::bad_input, "only a down starts a drag");
    canvas.threshold = 0.0;
    pointer(window, PointerAction::down, press_at);
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = press_at;
    require(tracker.press(down, DragStart{}) == DragStartResult::busy, "a press while a session runs is busy");
    key(window, PhysicalKey::escape);
    require(tracker.statistics().refused_starts >= 4U, "refused starts are counted");
}

void cancels() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    DragTracker& tracker = canvas.tracker;

    // Escape while armed ends without a plan or an end.
    pointer(window, PointerAction::down, press_at);
    key(window, PhysicalKey::escape);
    require(tracker.phase() == DragPhase::idle && canvas.ends.empty(), "Escape while armed disarms");

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    key(window, PhysicalKey::escape);
    require(canvas.ends.size() == 1U && canvas.ends[0].end == DragEnd::escaped && canvas.ends[0].hit == DragHit::none,
            "Escape while dragging ends escaped, with nothing selected");
    require(!drag_end_commits(canvas.ends[0].end) && tracker.statistics().restores == 1U, "an escaped end restores");

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    canvas.set_pointer_capture(false);
    require(canvas.ends.size() == 2U && canvas.ends[1].end == DragEnd::capture_lost && !tracker.running(),
            "a lost capture ends capture_lost");

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    tracker.cancel(DragEnd::rejected);
    require(tracker.running(), "cancel ignores an end other than model_changed or escaped");
    tracker.cancel();
    require(canvas.ends.size() == 3U && canvas.ends[2].end == DragEnd::model_changed && !tracker.running(),
            "cancel ends model_changed");
    tracker.cancel();
    require(canvas.ends.size() == 3U, "cancel outside a session does nothing");

    window.set_active(true);
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    window.set_active(false);
    require(canvas.ends.size() == 4U && canvas.ends[3].end == DragEnd::deactivated, "deactivation ends deactivated");

    // Refused plans: the session ends rejected and finished is called.
    canvas.threshold = 0.0;
    canvas.terms.refused = true;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && tracker.last_fault() == DragPlanFault::declined,
            "a declined plan refuses the start");
    require(canvas.ends.size() == 5U && canvas.ends[4].end == DragEnd::rejected && !tracker.running(),
            "a refused plan ends rejected");
    canvas.terms.refused = false;
    canvas.terms.placement.free_drop = false;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && tracker.last_fault() == DragPlanFault::bad_value,
            "a placement with no targets and no free drop is a bad value");
    canvas.terms.placement.free_drop = true;
    canvas.targets.assign(maximum_drag_targets + 1U, DragTarget{Rect{0.0, 0.0, 1.0, 1.0}, 1U, 0U});
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && tracker.last_fault() == DragPlanFault::too_many_targets,
            "more targets than the cap are refused, never truncated");
    canvas.targets.assign(1U, DragTarget{Rect{0.0, 0.0, 1.0, 1.0}, 1U, maximum_drag_target_groups});
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && tracker.last_fault() == DragPlanFault::bad_value,
            "a target group out of range is a bad value");
    require(tracker.statistics().refused_plans == 4U, "refused plans are counted");
}

// -- constraints ------------------------------------------------------------------

void constraints() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    canvas.threshold = 0.0;

    canvas.terms.placement.axis_lock = DragAxisLock::horizontal;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 170.0});
    require(near(canvas.previews.back().point.x, 145.0) && near(canvas.previews.back().point.y, 95.0),
            "a horizontal lock keeps y");
    key(window, PhysicalKey::escape);

    canvas.terms.placement.axis_lock = DragAxisLock::vertical;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 170.0});
    require(near(canvas.previews.back().point.x, 95.0) && near(canvas.previews.back().point.y, 165.0),
            "a vertical lock keeps x");
    key(window, PhysicalKey::escape);

    // dominant: the axis of the larger travel at the threshold, kept after.
    canvas.threshold = 4.0;
    canvas.terms.placement.axis_lock = DragAxisLock::dominant;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {100.0, 110.0});
    pointer(window, PointerAction::move, {180.0, 130.0});
    require(near(canvas.previews.back().point.x, 95.0) && near(canvas.previews.back().point.y, 125.0),
            "a dominant lock takes the axis of the first travel and keeps it");
    key(window, PhysicalKey::escape);
    canvas.threshold = 0.0;
    canvas.terms.placement.axis_lock = DragAxisLock::none;

    // The grid from the bounds' origin, and its toggle.
    canvas.terms.placement.grid = Size{10.0, 10.0};
    canvas.terms.placement.grid_toggle = Modifier::control;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {113.0, 100.0});
    require(near(canvas.previews.back().point.x, 110.0) && near(canvas.previews.back().point.y, 100.0),
            "the grid rounds the placement");
    pointer(window, PointerAction::move, {113.0, 100.0}, Modifier::control);
    require(near(canvas.previews.back().point.x, 108.0) && near(canvas.previews.back().point.y, 95.0),
            "the toggle held turns the grid off");
    key(window, PhysicalKey::escape);
    canvas.terms.placement.grid_by_default = false;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {113.0, 100.0});
    require(near(canvas.previews.back().point.x, 108.0), "with the grid off by default the placement is free");
    pointer(window, PointerAction::move, {113.0, 100.0}, Modifier::control);
    require(near(canvas.previews.back().point.x, 110.0), "and the toggle turns it on");
    key(window, PhysicalKey::escape);
    canvas.terms.placement.grid = Size{};
    canvas.terms.placement.grid_toggle = Modifier::none;
    canvas.terms.placement.grid_by_default = true;

    // The bounds: explicit, and the window's client area when empty.
    canvas.terms.placement.bounds = Rect{50.0, 50.0, 100.0, 100.0};
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {500.0, -500.0});
    require(near(canvas.previews.back().point.x, 140.0) && near(canvas.previews.back().point.y, 50.0),
            "the placement stays inside the bounds, with the subject's size");
    key(window, PhysicalKey::escape);
    canvas.terms.placement.bounds = Rect{};
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {900.0, 900.0});
    require(near(canvas.previews.back().point.x, 390.0) && near(canvas.previews.back().point.y, 390.0),
            "empty bounds are the window's client area");
    key(window, PhysicalKey::escape);
}

// -- drop targets -------------------------------------------------------------------

void drop_targets() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    canvas.threshold = 0.0;
    canvas.targets = {
        DragTarget{Rect{50.0, 150.0, 40.0, 40.0}, 7U, 0U},
        DragTarget{Rect{70.0, 150.0, 40.0, 40.0}, 9U, 0U},
        DragTarget{Rect{200.0, 200.0, 20.0, 20.0}, 11U, 1U},
    };

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {60.0, 160.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 7U,
            "a target containing the pointer is hit");
    require(near(canvas.previews.back().landing.x, 50.0) && near(canvas.previews.back().landing.width, 40.0),
            "the frame carries the target's bounds");
    pointer(window, PointerAction::move, {80.0, 160.0});
    require(canvas.previews.back().target == 7U, "of overlapping targets the earlier one wins");
    pointer(window, PointerAction::move, {100.0, 160.0});
    require(canvas.previews.back().target == 9U, "past the overlap the later one is hit");
    pointer(window, PointerAction::move, {300.0, 300.0});
    require(canvas.previews.back().hit == DragHit::point, "off every target the placement shows");
    pointer(window, PointerAction::up, {210.0, 210.0});
    require(canvas.ends.back().end == DragEnd::dropped && canvas.ends.back().hit == DragHit::target &&
                canvas.ends.back().target == 11U,
            "a release on a target drops on it");

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::up, {300.0, 300.0});
    require(canvas.ends.back().end == DragEnd::free, "with free_drop a release off the targets places");
    canvas.terms.placement.free_drop = false;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::up, {300.0, 300.0});
    require(canvas.ends.back().end == DragEnd::rejected && canvas.ends.back().hit == DragHit::none,
            "without free_drop a release off the targets is rejected");
    canvas.terms.placement.free_drop = true;

    // The magnet: the nearest target within the radius of the subject's centre.
    canvas.terms.placement.snap_radius = 16.0;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {230.0, 210.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 11U,
            "a target within the snap radius attracts");
    pointer(window, PointerAction::move, {240.0, 210.0});
    require(canvas.previews.back().hit == DragHit::point, "past the radius the placement shows");
    key(window, PhysicalKey::escape);

    // Hysteresis: captured within 16, held within 24, released past it, and
    // not captured again until within 16.
    canvas.terms.placement.release_radius = 24.0;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {240.0, 210.0});
    require(canvas.previews.back().hit == DragHit::point, "20 from the target is outside the capture radius");
    pointer(window, PointerAction::move, {230.0, 210.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 11U,
            "10 from the target captures it");
    pointer(window, PointerAction::move, {240.0, 210.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 11U,
            "20 from it, within the release radius, holds it");
    pointer(window, PointerAction::up, {243.0, 210.0});
    require(canvas.ends.back().end == DragEnd::dropped && canvas.ends.back().target == 11U,
            "a release in the release band drops on the held target");
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {230.0, 210.0});
    pointer(window, PointerAction::move, {250.0, 210.0});
    require(canvas.previews.back().hit == DragHit::point, "30 from it, past the release radius, releases it");
    pointer(window, PointerAction::move, {240.0, 210.0});
    require(canvas.previews.back().hit == DragHit::point, "back at 20 it is not captured again");
    key(window, PhysicalKey::escape);
    canvas.terms.placement.release_radius = 10.0;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && canvas.tracker.last_fault() == DragPlanFault::bad_value,
            "a release radius below the snap radius is refused");
    canvas.terms.placement.release_radius = 0.0;
    canvas.terms.placement.snap_radius = 0.0;

    canvas.terms.placement.targets_suppressed_by = Modifier::control;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {210.0, 210.0}, Modifier::control);
    require(canvas.previews.back().hit == DragHit::point, "the suppressing modifier hides the targets");
    pointer(window, PointerAction::move, {210.0, 210.0});
    require(canvas.previews.back().hit == DragHit::target, "released, the targets return");
    key(window, PhysicalKey::escape);
}

void reorder_and_value() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    canvas.threshold = 0.0;

    // Five 20-unit rows from y 90; the subject is row 1. Pressed at the
    // source's centre, the grab adds nothing: the pointer at y 160 is
    // nearest the landing of slot 3 (150 to 170).
    for (std::size_t row = 0U; row < 5U; ++row) {
        const double y = 90.0 + static_cast<double>(row) * 20.0;
        canvas.slots.push_back(Rect{90.0, y, 100.0, 20.0});
    }
    canvas.terms.kind = DragKind::reorder;
    canvas.terms.reorder.axis = DragAxis::y;
    canvas.terms.reorder.subject = 1U;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {100.0, 160.0});
    require(canvas.previews.back().hit == DragHit::slot && canvas.previews.back().target == 3U,
            "the reorder shows the slot the subject would take");
    pointer(window, PointerAction::up, {100.0, 160.0});
    require(canvas.ends.back().end == DragEnd::dropped && canvas.ends.back().target == 3U,
            "the release drops into the slot");
    canvas.terms.reorder.subject = 5U;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused, "a reorder subject past its slots is refused");

    canvas.terms.kind = DragKind::value;
    canvas.terms.value.axis = DragAxis::x;
    canvas.terms.value.origin_value = 50.0;
    canvas.terms.value.minimum = 0.0;
    canvas.terms.value.maximum = 100.0;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {130.0, 300.0});
    require(canvas.previews.back().hit == DragHit::value && near(canvas.previews.back().value, 80.0),
            "the value follows the travel along its axis");
    pointer(window, PointerAction::move, {300.0, 100.0});
    require(near(canvas.previews.back().value, 100.0), "the value is clamped");
    pointer(window, PointerAction::up, {110.0, 100.0});
    require(canvas.ends.back().end == DragEnd::free && near(canvas.ends.back().value, 60.0), "the release commits it");
    canvas.terms.value.maximum = -1.0;
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::refused && canvas.tracker.last_fault() == DragPlanFault::bad_value,
            "an inverted value range is refused");
}

// -- auto-scroll --------------------------------------------------------------------

void auto_scroll_velocity_bounds() {
    DragAutoScroll scroll{};
    scroll.viewport = Rect{0.0, 0.0, 400.0, 400.0};
    scroll.band = 24.0;
    scroll.speed = 1200.0;
    const Point centre = drag_auto_scroll_velocity(scroll, {200.0, 200.0});
    require(near(centre.x, 0.0) && near(centre.y, 0.0), "the interior asks for nothing");
    const Point inner = drag_auto_scroll_velocity(scroll, {376.0, 200.0});
    require(near(inner.x, 0.0), "the band's inner edge asks for nothing");
    const Point partway = drag_auto_scroll_velocity(scroll, {390.0, 200.0});
    require(near(partway.x, 1200.0 * 14.0 / 24.0) && near(partway.y, 0.0), "the band scales with the depth");
    const Point edge = drag_auto_scroll_velocity(scroll, {400.0, 200.0});
    const Point beyond = drag_auto_scroll_velocity(scroll, {900.0, 200.0});
    require(near(edge.x, 1200.0) && near(beyond.x, 1200.0), "at and past the edge the speed is the maximum");
    const Point low = drag_auto_scroll_velocity(scroll, {5.0, -50.0});
    require(near(low.x, -1200.0 * 19.0 / 24.0) && near(low.y, -1200.0), "the near edges scroll back, each axis apart");
    DragAutoScroll wide = scroll;
    wide.band = 300.0;
    const Point narrowed = drag_auto_scroll_velocity(wide, {100.0, 200.0});
    require(near(narrowed.x, -600.0) && near(narrowed.y, 0.0), "a band wider than half the viewport is narrowed");
    DragAutoScroll off = scroll;
    off.viewport = Rect{};
    const Point none = drag_auto_scroll_velocity(off, {0.0, 0.0});
    require(near(none.x, 0.0) && near(none.y, 0.0), "an empty viewport is off");
    off = scroll;
    off.speed = 0.0;
    const Point stopped = drag_auto_scroll_velocity(off, {0.0, 0.0});
    require(near(stopped.x, 0.0), "a zero speed is off");
    const Point unknown = drag_auto_scroll_velocity(scroll, {std::numeric_limits<double>::quiet_NaN(), 0.0});
    require(near(unknown.x, 0.0) && near(unknown.y, 0.0), "a non-finite pointer asks for nothing");
}

void auto_scroll_frames() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    DragTracker& tracker = canvas.tracker;
    canvas.threshold = 0.0;
    canvas.offset_limit = Point{0.0, 300.0};
    // Below the window until the content scrolls 300 down.
    canvas.targets = {DragTarget{Rect{300.0, 600.0, 50.0, 50.0}, 5U, 0U}};
    canvas.terms.auto_scroll.viewport = Rect{0.0, 0.0, 400.0, 400.0};

    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {100.0, 395.0});
    require(tracker.scroll_velocity().y > 0.0 && near(tracker.scroll_velocity().x, 0.0),
            "the bottom band asks to scroll down");
    require(window.next_wake().has_value(), "auto-scroll schedules a frame");
    const double frame_seconds = std::chrono::duration<double>(std::chrono::milliseconds(16)).count();
    const double longest_step = default_drag_scroll_speed * frame_seconds * 4.0 + 1e-9;
    for (int frame = 0; frame < 1000 && tracker.statistics().scroll_stops == 0U; ++frame) {
        const std::optional<FrameTime> wake = window.next_wake();
        if (!wake) break;
        static_cast<void>(window.poll_frame_schedule(*wake));
    }
    require(tracker.statistics().scroll_stops == 1U && near(canvas.offset.y, 300.0),
            "auto-scroll runs to the content's end and stops there");
    const DragTrackerStatistics& counted = tracker.statistics();
    require(counted.scroll_steps >= 2U && counted.replans >= counted.scroll_steps, "each applied step plans again");
    for (const DragAutoScrollStep& step : canvas.steps) {
        require(step.requested.y > 0.0 && step.requested.y <= longest_step && near(step.requested.x, 0.0),
                "each step is down and bounded by four frames at the maximum speed");
    }
    require(near(tracker.scroll_velocity().y, 0.0), "a stopped auto-scroll asks for nothing");
    const std::uint64_t ticks = tracker.statistics().scroll_ticks;
    const std::optional<FrameTime> later = window.next_wake();
    if (later) static_cast<void>(window.poll_frame_schedule(*later));
    require(tracker.statistics().scroll_ticks == ticks, "no frame ticks after the stop");

    // The plan was redone against the scrolled content: the target now
    // shows at y 300.
    pointer(window, PointerAction::move, {320.0, 320.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 5U,
            "a target scrolled into view is hit");
    require(canvas.plans.back().epoch > 1U, "the redone plans carry later epochs");
    pointer(window, PointerAction::up, {320.0, 320.0});
    require(canvas.ends.back().end == DragEnd::dropped && canvas.ends.back().target == 5U,
            "the release drops on the scrolled-in target");

    // Leaving the band stops auto-scroll before the content's end.
    canvas.offset = Point{};
    canvas.steps.clear();
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {100.0, 395.0});
    const std::optional<FrameTime> first = window.next_wake();
    if (first) static_cast<void>(window.poll_frame_schedule(*first));
    pointer(window, PointerAction::move, {100.0, 200.0});
    require(near(tracker.scroll_velocity().y, 0.0) && canvas.offset.y < 300.0,
            "leaving the band stops auto-scroll");
    key(window, PhysicalKey::escape);
}

// -- reentrancy ---------------------------------------------------------------------

void withdrawn_targets() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    DragTracker& tracker = canvas.tracker;
    canvas.threshold = 0.0;
    canvas.targets = {
        DragTarget{Rect{50.0, 150.0, 40.0, 40.0}, 7U, 0U},
        DragTarget{Rect{200.0, 200.0, 20.0, 20.0}, 11U, 0U},
    };

    // Outside a callback: the plan is redone at once.
    require(!tracker.withdraw_target(7U), "nothing is withdrawn outside a session");
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {60.0, 160.0});
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 7U, "on the target");
    require(tracker.withdraw_target(7U), "a target is withdrawn while dragging");
    require(canvas.previews.back().hit == DragHit::point && canvas.plans.size() == 2U,
            "the plan is redone without it and the change previewed");
    require(!tracker.withdraw_target(7U), "a target is withdrawn once");
    pointer(window, PointerAction::up, {60.0, 160.0});
    require(canvas.ends.back().end == DragEnd::free, "a release where it was places freely");

    // Inside a preview: hidden at once, the plan redone at the next event.
    canvas.withdraw_in_preview = 7U;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {60.0, 160.0});
    require(tracker.statistics().deferred_replans == 1U, "a withdrawal inside a callback waits");
    pointer(window, PointerAction::up, {60.0, 160.0});
    require(canvas.ends.back().end == DragEnd::free && canvas.ends.back().hit == DragHit::point,
            "the next event redoes the plan before the release is evaluated");
    canvas.withdraw_in_preview = no_target;

    // A keyboard session keeps its stops: a withdrawn target is announced
    // and stepped past; Enter on a withdrawn selection is rejected.
    canvas.terms.placement.free_drop = false;
    canvas.targets.push_back(DragTarget{Rect{300.0, 150.0, 40.0, 40.0}, 13U, 0U});
    const DragStart keyboard{};
    require(tracker.begin_keyboard(keyboard) == DragStartResult::started, "a keyboard drag starts");
    require(!canvas.announced.empty() && canvas.announced.back().hit == DragHit::target &&
                canvas.announced.back().target == 7U,
            "the keyboard starts on the first reachable target");
    const std::size_t plans = canvas.plans.size();
    require(tracker.withdraw_target(11U) && canvas.plans.size() == plans, "a keyboard session is not planned again");
    require(canvas.reasons.back() == DragAnnouncementReason::target_withdrawn && canvas.withdrawn_ids.back() == 11U,
            "the owner is asked to announce the withdrawn target");
    key(window, PhysicalKey::right);
    require(canvas.previews.back().hit == DragHit::target && canvas.previews.back().target == 13U,
            "Right steps past the withdrawn target to the next stop");
    key(window, PhysicalKey::left);
    require(canvas.previews.back().target == 7U, "and Left steps back past it");
    require(tracker.withdraw_target(7U), "the selected target is withdrawn");
    key(window, PhysicalKey::enter);
    require(canvas.ends.back().end == DragEnd::rejected && canvas.ends.back().hit == DragHit::none,
            "Enter on a withdrawn target is rejected, with nothing selected");
    canvas.targets.pop_back();

    // Without free_drop, withdrawing the last target refuses the redone plan.
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {60.0, 160.0});
    require(tracker.withdraw_target(7U) && tracker.running(), "one target left");
    const std::size_t ends = canvas.ends.size();
    require(tracker.withdraw_target(11U) && !tracker.running(), "the last withdrawal ends the session");
    require(canvas.ends.size() == ends + 1U && canvas.ends.back().end == DragEnd::rejected, "it ends rejected");
    canvas.terms.placement.free_drop = true;
}

void reentrant_calls() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Canvas& canvas = *fixture.canvas;
    DragTracker& tracker = canvas.tracker;
    canvas.threshold = 0.0;

    // cancel from inside a preview: finished once, nothing after.
    canvas.cancel_in_preview = true;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    canvas.cancel_in_preview = false;
    const std::size_t previews = canvas.previews.size();
    require(canvas.ends.size() == 1U && canvas.ends[0].end == DragEnd::model_changed && !tracker.running(),
            "a cancel inside a preview ends the session once");
    pointer(window, PointerAction::move, {160.0, 160.0});
    require(canvas.previews.size() == previews && canvas.ends.size() == 1U, "nothing is called after it");

    // press from inside a preview is busy.
    canvas.press_in_preview = true;
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    canvas.press_in_preview = false;
    require(canvas.reentrant_start == DragStartResult::busy && tracker.running(),
            "a press inside a callback is refused and the drag goes on");
    key(window, PhysicalKey::escape);

    // The owner leaving the window mid-drag: a pointer drag loses the
    // capture first and ends capture_lost; a keyboard drag, which holds no
    // capture, ends through owner_detaching without a call.
    pointer(window, PointerAction::down, press_at);
    pointer(window, PointerAction::move, {150.0, 150.0});
    std::size_t ends = canvas.ends.size();
    Control::Ptr removed = (*fixture.root).remove_child(canvas.runtime_id());
    require(removed && canvas.ends.size() == ends + 1U && canvas.ends.back().end == DragEnd::capture_lost &&
                !tracker.running(),
            "a detached owner's pointer drag ends capture_lost");
    (*fixture.root).add_child(fixture.canvas);
    window.perform_layout();
    require(tracker.begin_keyboard(DragStart{}) == DragStartResult::started, "a keyboard drag starts");
    ends = canvas.ends.size();
    removed = (*fixture.root).remove_child(canvas.runtime_id());
    require(removed && canvas.ends.size() == ends && !tracker.running(),
            "a detached owner's keyboard drag ends without a call");
    (*fixture.root).add_child(fixture.canvas);
    window.perform_layout();
    pointer(window, PointerAction::down, press_at);
    require(canvas.last_start == DragStartResult::started, "the window's engine is free again");
    key(window, PhysicalKey::escape);
}

// A recorder for a tracker the test owns.
struct Recorder final {
    std::size_t plans{};
    std::size_t ends{};
    void plan(DragPlan& plan) {
        ++plans;
        plan.kind = DragKind::placement;
    }
    void finished(const DragTrackerFrame&) { ++ends; }
};

void destroyed_mid_drag() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    Recorder recorder;
    std::unique_ptr<DragTracker> tracker = std::make_unique<DragTracker>(*fixture.root);
    (*tracker).callbacks.plan = Delegate<DragPlan&>::bind<Recorder, &Recorder::plan>(recorder);
    (*tracker).callbacks.finished = Delegate<const DragTrackerFrame&>::bind<Recorder, &Recorder::finished>(recorder);
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = press_at;
    DragStart start{};
    start.threshold = 0.0;
    require((*tracker).press(down, start) == DragStartResult::started && recorder.plans == 1U,
            "a tracker the test owns drags the root");
    tracker.reset();
    require(recorder.ends == 0U, "a destroyed tracker is not called back");
    DragTracker& other = (*fixture.canvas).tracker;
    (*fixture.canvas).threshold = 0.0;
    pointer(window, PointerAction::down, press_at);
    require((*fixture.canvas).last_start == DragStartResult::started && other.running(),
            "its session ended, so the window's engine is free");
    key(window, PhysicalKey::escape);
}

} // namespace

int main() {
    transitions_and_threshold();
    cancels();
    constraints();
    drop_targets();
    reorder_and_value();
    auto_scroll_velocity_bounds();
    auto_scroll_frames();
    withdrawn_targets();
    reentrant_calls();
    destroyed_mid_drag();
    std::cout << "drag tracker: transitions, cancels, thresholds, constraints, targets, auto-scroll, reentrancy\n";
    return 0;
}
