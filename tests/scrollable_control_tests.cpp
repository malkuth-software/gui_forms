#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/range_control_values.hpp"
#include "gui_forms/controls/scroll_bar.hpp"
#include "gui_forms/controls/scroll_bar_metrics.hpp"
#include "gui_forms/controls/scroll_properties.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/clip_tracking_painter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class ObserveScrollEvent final {
public:
    ObserveScrollEvent(std::size_t& notifications, ScrollEvent& last)
        : notifications_(notifications), last_(last) {}

    void operator()(ScrollEvent& event) const {
        ++notifications_;
        last_ = event;
    }

private:
    std::size_t& notifications_;
    ScrollEvent& last_;
};

bool near(double left, double right) {
    return std::abs(left - right) < 0.0001;
}

// The motion clock's time, set by the test, so that no smooth-scroll test
// depends on the host's clock. Declared before the window whose clock
// borrows it, so it outlives the installation.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// Advances the test's time by `step` and polls the window there.
void advance(Window& window, ManualTime& time, std::chrono::milliseconds step) {
    time.time += step;
    static_cast<void>(window.poll_frame_schedule(time.time));
}

// Advances frame by frame (at the clock's 16 ms cadence) for `frames`.
void advance_frames(Window& window, ManualTime& time, int frames) {
    for (int frame = 0; frame < frames; ++frame) advance(window, time, std::chrono::milliseconds(16));
}

void reduce_motion(Window& window) {
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
}

void test_control_family_and_manual_axis_contract() {
    static_assert(std::is_base_of_v<ScrollableControl, Panel>);
    static_assert(std::is_base_of_v<ScrollableControl, ContainerControl>);

    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("scroll.manual"));
    require(!(*panel).auto_scroll.read() && !(*panel).hscroll() && !(*panel).vscroll(),
            "scrollable controls must default to an idle unscrolled viewport");
    require((*panel).horizontal_scroll().enabled() &&
                (*panel).horizontal_scroll().minimum() == 0.0 &&
                (*panel).horizontal_scroll().maximum() == 100.0 &&
                (*panel).horizontal_scroll().large_change() == 10.0 &&
                (*panel).horizontal_scroll().small_change() == 1.0,
            "manual ScrollProperties defaults must match the Forms contract");

    (*panel).set_hscroll(true);
    (*panel).horizontal_scroll().set_maximum(250.0);
    (*panel).horizontal_scroll().set_large_change(50.0);
    (*panel).horizontal_scroll().set_value(80.0);
    Window window(panel, {100.0, 80.0});
    window.perform_layout();
    require((*panel).hscroll() && !(*panel).vscroll() &&
                (*panel).viewport_rectangle() == Rect{0.0, 0.0, 100.0, 64.0} &&
                (*panel).scroll_position() == Point{80.0, 0.0},
            "manual scroll visibility and value must survive retained layout");

    (*panel).horizontal_scroll().set_large_change(500.0);
    (*panel).horizontal_scroll().set_small_change(600.0);
    require((*panel).horizontal_scroll().large_change() == 251.0 &&
                (*panel).horizontal_scroll().small_change() == 251.0,
            "effective scroll changes must clamp without losing authored values");

    bool bad_minimum{};
    bool bad_value{};
    try {
        (*panel).horizontal_scroll().set_minimum(-1.0);
    } catch (const std::invalid_argument&) {
        bad_minimum = true;
    }
    try {
        (*panel).horizontal_scroll().set_value(300.0);
    } catch (const std::out_of_range&) {
        bad_value = true;
    }
    require(bad_minimum && bad_value,
            "ScrollProperties must reject invalid ranges and out-of-range values");
}

void test_auto_scroll_projection_viewport_and_resize() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("scroll.auto"));
    std::shared_ptr<gui_forms::Button> child = make_control<Button>(StableId("scroll.auto.child"), "Content");
    (*child).set_requested_bounds({20.0, 10.0, 220.0, 160.0});
    (*panel).add_child(child);
    (*panel).auto_scroll_margin = {5.0, 7.0};
    (*panel).auto_scroll = true;
    Window window(panel, {100.0, 80.0});
    window.perform_layout();

    ScrollSnapshot initial = (*panel).scroll_snapshot();
    require(initial.horizontal.visible && initial.vertical.visible &&
                initial.viewport_rectangle == Rect{0.0, 0.0, 84.0, 64.0} &&
                initial.display_rectangle == Rect{0.0, 0.0, 248.0, 180.0},
            "automatic scroll layout must solve two-axis scrollbar interdependence");

    const Rect authored = (*child).requested_bounds();
    require((*panel).scroll_to({50.0, 40.0}),
            "programmatic scroll must move a visible automatic viewport");
    window.perform_layout();
    require((*panel).auto_scroll_position.read() == Point{-50.0, -40.0} &&
                (*child).requested_bounds() == authored &&
                (*child).committed_arranged_bounds() ==
                    Rect{-30.0, -30.0, 220.0, 160.0} &&
                (*panel).display_rectangle() == Rect{-50.0, -40.0, 248.0, 180.0},
            "scrolling must project retained child geometry without destructive drift");
    window.perform_layout();
    require((*child).committed_arranged_bounds() ==
                Rect{-30.0, -30.0, 220.0, 160.0},
            "repeated layout must not accumulate the scroll translation");
    // A second scroll to a different position re-arranges from the authored
    // slot, not from the bounds the first scroll already offset.
    require((*panel).scroll_to({10.0, 5.0}), "second programmatic scroll accepted");
    window.perform_layout();
    require((*child).committed_arranged_bounds() == Rect{10.0, 5.0, 220.0, 160.0},
            "a second scroll must not accumulate the previous scroll translation");
    require((*panel).scroll_to({50.0, 40.0}), "scroll back accepted");
    window.perform_layout();
    require(!(*panel).get_child_at_point({10.0, 70.0}) &&
                window.hit_test({10.0, 70.0}) == panel,
            "children must not receive hits through scrollbar chrome");

    window.resize({300.0, 220.0});
    window.perform_layout();
    require(!(*panel).hscroll() && !(*panel).vscroll() &&
                (*panel).scroll_position() == Point{} &&
                (*child).committed_arranged_bounds() == authored,
            "viewport growth must hide unnecessary bars, clamp position, and restore projection");
}

void test_scroll_gestures_events_semantics_and_state() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("scroll.input"));
    std::shared_ptr<gui_forms::Control> content = make_control<Control>(StableId("scroll.input.content"));
    (*content).set_requested_bounds({0.0, 0.0, 40.0, 300.0});
    (*panel).add_child(content);
    (*panel).auto_scroll = true;
    Window window(panel, {100.0, 100.0});
    // Reduced motion: wheel and bar input move the position at once, so the
    // positions below are read right after each input (the smooth scroll
    // is tested by test_wheel_glides_on_the_motion_clock).
    reduce_motion(window);
    window.perform_layout();

    std::size_t notifications{};
    ScrollEvent last;
    SubscriptionToken token = (*panel).scroll().subscribe(
        ObserveScrollEvent(notifications, last));
    require((*panel).scroll_to({0.0, 25.0}) && notifications == 0,
            "programmatic viewport changes must not masquerade as user Scroll events");
    window.perform_layout();

    PointerEvent wheel;
    wheel.action = PointerAction::wheel;
    wheel.position = {20.0, 20.0};
    wheel.wheel_delta = {0.0, -1.0};
    require(window.dispatch_pointer(wheel) &&
                (*panel).scroll_position().y > 25.0 && notifications == 0,
            "wheel input must update retained position while preserving WinForms Scroll suppression");

    // A precise device's pixels scroll exactly, fractions included; a
    // detented wheel's line scrolls one 48-pixel step.
    const double before_precise = (*panel).scroll_position().y;
    PointerEvent precise = wheel;
    precise.wheel_delta = {0.0, -2.5};
    precise.wheel_unit = WheelDeltaUnit::pixels;
    require(window.dispatch_pointer(precise) &&
                std::abs((*panel).scroll_position().y - (before_precise + 2.5)) < 1.0e-9,
            "a precise wheel delta must scroll by exactly its logical pixels");
    window.perform_layout();
    const double before_line = (*panel).scroll_position().y;
    PointerEvent line = wheel;
    line.wheel_delta = {0.0, 1.0};
    require(window.dispatch_pointer(line) &&
                std::abs((*panel).scroll_position().y - std::max(0.0, before_line - 48.0)) < 1.0e-9,
            "a detented wheel line must scroll one 48-pixel step");
    window.perform_layout();

    const std::vector<SemanticNode> nodes = (*panel).semantic_virtual_children();
    require(nodes.size() == 1U &&
                nodes.front().role == SemanticRole::scroll_bar &&
                nodes.front().stable_id == "scroll.input.vertical-scroll",
            "visible axes must publish stable virtual accessibility scrollbars");
    const double before = (*panel).scroll_position().y;
    require((*panel).on_semantic_child_action(
                "scroll.input.vertical-scroll", SemanticAction::increment, "") &&
                (*panel).scroll_position().y > before && notifications == 1U &&
                last.type == ScrollEventType::small_increment &&
                last.orientation == ScrollOrientation::vertical,
            "semantic scrollbar actions must use the same eventful interaction path");
    require((*panel).get_scroll_state(
                ScrollableControl::scroll_state_user_has_scrolled),
            "eventful scrolling must retain the user-scrolled state");

    bool bad_state{};
    try {
        (*panel).set_scroll_state(0x80000000U, true);
    } catch (const std::invalid_argument&) {
        bad_state = true;
    }
    require(bad_state, "unknown scroll state bits must be rejected explicitly");
}

void test_control_into_view_offset_and_minimum_extent() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("scroll.into-view"));
    std::shared_ptr<gui_forms::Button> target = make_control<Button>(StableId("scroll.into-view.target"), "Target");
    (*target).set_requested_bounds({180.0, 120.0, 20.0, 20.0});
    (*target).set_auto_scroll_offset({3.0, 5.0});
    (*panel).add_child(target);
    (*panel).auto_scroll_min_size = {220.0, 180.0};
    Window window(panel, {100.0, 80.0});
    window.perform_layout();
    require((*panel).auto_scroll.read() && (*panel).hscroll() && (*panel).vscroll(),
            "AutoScrollMinSize must enable automatic scrolling and contribute extent");

    (*panel).scroll_control_into_view(target);
    window.perform_layout();
    require(near((*panel).scroll_position().x, 113.0) &&
                near((*panel).scroll_position().y, 71.0),
            "ScrollControlIntoView must honor viewport geometry and AutoScrollOffset");

    bool invalid_margin{};
    try {
        (*panel).auto_scroll_margin = Size{-1.0, 0.0};
    } catch (const std::invalid_argument&) {
        invalid_margin = true;
    }
    (*panel).auto_scroll_margin = Size{0.0, 9.0};
    require(invalid_margin && (*panel).auto_scroll_margin.read() == Size{0.0, 9.0},
            "property assignment must reject negative margins and store a valid one");
}

} // namespace

// Press-and-hold repeat (ADR-016): a UI timer the bar stops on release,
// first after the initial delay, then every repeat interval; no active
// surface.
void test_scroll_bar_press_and_hold_repeats_on_a_ui_timer() {
    std::shared_ptr<ScrollBar> bar =
        make_control<ScrollBar>(StableId("scroll.repeat"), Orientation::horizontal);
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 17.0});
    (*bar).set_range(0.0, 100.0);
    (*bar).small_change = 1.0;
    Window window(bar, {200.0, 17.0});
    window.perform_layout();
    PointerEvent press;
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = {195.0, 8.0};
    require(window.dispatch_pointer(press) && near((*bar).value.read(), 1.0),
            "pressing the increment button must step once");
    const FrameTime pressed = FrameClock::now();
    require(window.next_wake().has_value() &&
                *window.next_wake() >= pressed + (*bar).initial_repeat_delay.read() - std::chrono::milliseconds(5),
            "holding must schedule the first repeat after the initial delay, on a UI timer");
    const FrameTime first = *window.next_wake();
    const FramePollResult repeat = window.poll_frame_schedule(first);
    require(repeat.ui_timer_ticks == 1U && near((*bar).value.read(), 2.0) &&
                *window.next_wake() == first + (*bar).repeat_interval.read(),
            "each repeat must step once and schedule the next one an interval later");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require(near((*bar).value.read(), 3.0), "a held press must keep repeating");
    PointerEvent release = press;
    release.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(release));
    require(!window.next_wake().has_value() && near((*bar).value.read(), 3.0),
            "release must stop the repeat timer");
}

// A host that keeps its pixels (scroll copies on): a scroll that only moves
// the viewport's children by whole device pixels records one copy and
// damages the band it exposed and the bar, not the viewport; a control
// over the viewport is repainted where it is and where the copy moved it.
void test_scroll_copies_damage_only_the_exposed_band() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("copy.root"));
    // An opaque solid ground whose paint does not depend on its size: the
    // copy's precondition beneath the viewport (the theme's panel material
    // has a border band and no opaque cover).
    (*root).set_background(Color::rgba(240, 240, 240));
    std::shared_ptr<gui_forms::ScrollableControl> viewport =
        make_control<ScrollableControl>(StableId("copy.viewport"));
    (*viewport).auto_scroll = true;
    (*viewport).set_requested_bounds({10.0, 10.0, 100.0, 80.0});
    std::shared_ptr<gui_forms::Button> child = make_control<Button>(StableId("copy.child"), "Tall");
    (*child).set_requested_bounds({0.0, 0.0, 80.0, 400.0});
    (*viewport).add_child(child);
    (*root).add_child(viewport);
    Window window(root, {200.0, 120.0});
    window.set_scroll_copies_supported(true);
    window.perform_layout();
    // The host has painted and presented a frame: the controls have
    // recorded what they paint, which is what the copy keeps.
    test_support::ClipTrackingPainter painter;
    static_cast<void>(window.paint(painter));
    static_cast<void>(window.take_damage());
    std::vector<ScrollCopy> copies;
    window.take_scroll_copies(copies);
    copies.clear();

    require((*viewport).scroll_by({0.0, 10.0}), "the viewport scrolls");
    const DamageRegion damage = window.take_damage();
    window.take_scroll_copies(copies);
    const Rect clip{10.0, 10.0, 84.0, 80.0};
    require(copies.size() == 1U && copies[0].clip == clip && copies[0].offset == Point{0.0, -10.0},
            "a scroll by whole pixels must record one copy of the viewport");
    // The band, the bar, and the bar's ink over the viewport: one bar
    // thickness inside it (invalidate_scroll_bar_ink).
    const double band_and_bar = 84.0 * 10.0 + 16.0 * 80.0 + 16.0 * 80.0;
    require(damage.area() <= band_and_bar + 1.0e-6 && damage.area() >= 84.0 * 10.0,
            "a copied scroll must damage only the exposed band, the bar and its ink");
    require((*child).committed_arranged_bounds() == Rect{0.0, -10.0, 80.0, 400.0},
            "the children move by the scroll");

    // Under a pixel: the children stay on the pixel grid, nothing is copied.
    copies.clear();
    require((*viewport).scroll_by({0.0, 0.25}), "a fractional scroll is kept");
    const DamageRegion fractional = window.take_damage();
    window.take_scroll_copies(copies);
    require(copies.empty() && fractional.area() <= 16.0 * 80.0 + 1.0e-6 &&
                (*child).committed_arranged_bounds() == Rect{0.0, -10.0, 80.0, 400.0} &&
                near((*viewport).scroll_position().y, 10.25),
            "a scroll under a device pixel must keep the exact position and move no pixels");

    // A later sibling over the viewport is repainted where it is and where
    // the copy moved its pixels; the rest of the viewport is still copied.
    std::shared_ptr<gui_forms::Label> cover = make_control<Label>(StableId("copy.cover"), "Over");
    (*cover).set_requested_bounds({40.0, 40.0, 30.0, 20.0});
    (*root).add_child(cover);
    window.perform_layout();
    static_cast<void>(window.take_damage());
    copies.clear();
    require((*viewport).scroll_by({0.0, 12.0}), "the covered viewport scrolls");
    const DamageRegion covered = window.take_damage();
    window.take_scroll_copies(copies);
    const double repainted_cover = 2.0 * 30.0 * 20.0;
    require(copies.size() == 1U && copies[0].offset == Point{0.0, -12.0} &&
                covered.area() <= 84.0 * 12.0 + 2.0 * 16.0 * 80.0 + repainted_cover + 1.0e-6,
            "a control over the viewport must be repainted, and the viewport still copied");
    bool cover_damaged = false;
    for (const Rect rect : covered.rectangles()) {
        if (rect.contains(Rect{40.0, 40.0, 30.0, 20.0})) cover_damaged = true;
    }
    require(cover_damaged, "the control over the viewport must be damaged where it is");
}

// damage_after_scroll_copy: a rectangle's share inside the clip moves and is
// cut to it; its share outside stays.
void test_damage_moves_with_a_scroll_copy() {
    DamageRegion damage;
    damage.add({0.0, 20.0, 10.0, 10.0});
    damage.add({50.0, 50.0, 10.0, 10.0});
    const DamageRegion moved = damage_after_scroll_copy(damage, {0.0, 0.0, 40.0, 100.0}, {0.0, -15.0});
    require(near(moved.area(), 200.0) && moved.bounds() == Rect{0.0, 5.0, 60.0, 55.0},
            "damage inside the clip must move with the copy and damage outside stay");
    const DamageRegion clipped = damage_after_scroll_copy(damage, {0.0, 22.0, 40.0, 100.0}, {0.0, -15.0});
    // The first rectangle keeps its two rows above the clip; its share
    // inside moves above the clip and is cut away.
    require(near(clipped.area(), 120.0) && clipped.bounds() == Rect{0.0, 20.0, 60.0, 40.0},
            "a rectangle crossing the clip keeps its outside share");
}

// A panel 100 high over content 1000 high (and its 3-unit margin): the
// vertical axis scrolls 0..903.
struct GlideFixture final {
    ManualTime time{};
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("glide.panel"));
    std::shared_ptr<gui_forms::Control> content = make_control<Control>(StableId("glide.content"));
    Window window{panel, {100.0, 100.0}};
    GlideFixture() {
        (*content).set_requested_bounds({0.0, 0.0, 40.0, 1000.0});
        (*panel).add_child(content);
        (*panel).auto_scroll = true;
        window.motion_clock().set_time_source(&time);
        window.perform_layout();
    }
    bool wheel(double lines, WheelDeltaUnit unit = WheelDeltaUnit::lines) {
        PointerEvent event;
        event.action = PointerAction::wheel;
        event.position = {20.0, 20.0};
        event.wheel_delta = {0.0, lines};
        event.wheel_unit = unit;
        const bool handled = window.dispatch_pointer(event);
        return handled;
    }
};

// wheel_scrolls off: a wheel turn over the content (bubbling from a child)
// or over the panel itself passes through unhandled and leaves the position;
// turned back on, the same turns scroll as before.
void test_wheel_scrolls_off_leaves_the_wheel_to_the_consumer() {
    GlideFixture f{};
    require((*f.panel).wheel_scrolls.read(), "wheel_scrolls must default to true");
    (*f.panel).wheel_scrolls = false;
    PointerEvent over_child;
    over_child.action = PointerAction::wheel;
    over_child.position = {20.0, 20.0};
    over_child.wheel_delta = {0.0, -10.0};
    over_child.wheel_unit = WheelDeltaUnit::pixels;
    PointerEvent over_panel = over_child;
    over_panel.position = {60.0, 20.0};
    const bool child_handled = f.window.dispatch_pointer(over_child);
    const bool panel_handled = f.window.dispatch_pointer(over_panel);
    require(!child_handled && !panel_handled &&
                (*f.panel).scroll_position().y == 0.0 && !(*f.panel).scrolling_smoothly(),
            "with wheel_scrolls off a wheel turn must pass through and leave the position");
    (*f.panel).wheel_scrolls = true;
    require(f.window.dispatch_pointer(over_child) && near((*f.panel).scroll_position().y, 10.0),
            "with wheel_scrolls on a wheel turn over a child must scroll the panel");
    require(f.window.dispatch_pointer(over_panel) && near((*f.panel).scroll_position().y, 20.0),
            "with wheel_scrolls on a wheel turn over the panel must scroll it");
}

// Smooth scrolling: a wheel line moves the target at once and the position
// over the next frames, a fixed fraction of the gap per frame; a second
// line before it settles adds to the target; the glide settles exactly on
// the target and the clock then holds no frame request.
void test_wheel_glides_on_the_motion_clock() {
    GlideFixture f{};
    require(f.wheel(-1.0) && near((*f.panel).scroll_position().y, 0.0) &&
                (*f.panel).scrolling_smoothly() && near((*f.panel).scroll_target().y, 48.0) &&
                f.window.motion_clock().armed(),
            "a wheel line must set the target and leave the position to the motion clock");
    advance(f.window, f.time, std::chrono::milliseconds(32));
    const double first = (*f.panel).scroll_position().y;
    // follow: the gap shrinks by e each 75 ms, so after 32 ms at most
    // 1 - e^(-32/75), about 35 %, of 48 has been covered.
    require(first > 0.0 && first < 48.0 * 0.4,
            "a glide must move part of the way per frame, bounded by its curve");
    require(f.wheel(-1.0) && near((*f.panel).scroll_target().y, 96.0),
            "a second line while gliding must add to the target, not to the position");
    advance_frames(f.window, f.time, 60);
    require((*f.panel).scroll_position().y == 96.0 && !(*f.panel).scrolling_smoothly() &&
                !f.window.motion_clock().armed(),
            "a glide must settle exactly on its target and release the frame request");

    // A precise device's pixels (a trackpad, which the system already
    // smooths) move the position at once and start no glide; one arriving
    // during a glide stops it where it is and moves on from there.
    require(f.wheel(-2.5, WheelDeltaUnit::pixels) && near((*f.panel).scroll_position().y, 98.5) &&
                !(*f.panel).scrolling_smoothly() && !f.window.motion_clock().armed(),
            "a precise delta must move the position at once by its exact pixels");
    require(f.wheel(-1.0) && (*f.panel).scrolling_smoothly(), "a line glides again");
    advance(f.window, f.time, std::chrono::milliseconds(16));
    const double interrupted = (*f.panel).scroll_position().y;
    require(f.wheel(-3.0, WheelDeltaUnit::pixels) && near((*f.panel).scroll_position().y, interrupted + 3.0) &&
                !(*f.panel).scrolling_smoothly(),
            "a precise delta during a glide must stop it and move on from the position");

    // At the end of the range the target clamps; a line past it is not
    // handled, so an enclosing scroller may take it.
    // The range ends at the content's bottom plus its margin (Control's
    // default margin, 3 on each side) less the viewport's height.
    const double range_end = 1000.0 + (*f.content).margin.read().bottom - 100.0;
    require(f.wheel(-2000.0, WheelDeltaUnit::pixels) && near((*f.panel).scroll_target().y, range_end),
            "the target clamps to the range");
    advance_frames(f.window, f.time, 60);
    require(!f.wheel(-1.0) && !(*f.panel).scrolling_smoothly(),
            "a line past the end must not be handled or start a glide");
}

// The bars' arrows glide and raise one Scroll event at the press, carrying
// the target; programmatic scrolls and the thumb are immediate and stop
// the glide; reduced motion makes every input immediate.
void test_bar_glides_and_immediate_input_stops_it() {
    GlideFixture f{};
    std::size_t notifications{};
    ScrollEvent last;
    SubscriptionToken token = (*f.panel).scroll().subscribe(ObserveScrollEvent(notifications, last));
    const double small = (*f.panel).vertical_scroll().small_change();
    PointerEvent press;
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = {92.0, 95.0};
    require(f.window.dispatch_pointer(press) && notifications == 1U &&
                last.type == ScrollEventType::small_increment && near(last.new_value, small) &&
                near((*f.panel).scroll_position().y, 0.0) && (*f.panel).scrolling_smoothly(),
            "an arrow press must raise one Scroll event with the target and glide there");
    PointerEvent release = press;
    release.action = PointerAction::up;
    static_cast<void>(f.window.dispatch_pointer(release));
    advance_frames(f.window, f.time, 60);
    require(near((*f.panel).scroll_position().y, small) && notifications == 1U,
            "the glide's frames must raise no further Scroll events");

    require(f.wheel(-1.0) && (*f.panel).scrolling_smoothly(), "a glide is under way");
    require((*f.panel).scroll_to({0.0, 300.0}) && !(*f.panel).scrolling_smoothly() &&
                near((*f.panel).scroll_position().y, 300.0),
            "a programmatic scroll must land at once and stop the glide");
    advance_frames(f.window, f.time, 10);
    require(near((*f.panel).scroll_position().y, 300.0), "a stopped glide must not move the position again");

    require(f.wheel(-1.0) && (*f.panel).scrolling_smoothly(), "a second glide is under way");
    static_cast<void>((*f.panel).scroll_to((*f.panel).scroll_position()));
    require((*f.panel).scrolling_smoothly(),
            "restating the current position (a text box on paint) must leave the glide moving");

    reduce_motion(f.window);
    require(!(*f.panel).scrolling_smoothly() && !f.window.motion_clock().armed(),
            "turning reduced motion on must finish the glide");
    const double settled = (*f.panel).scroll_position().y;
    require(f.wheel(-1.0) && near((*f.panel).scroll_position().y, settled + 48.0) &&
                !(*f.panel).scrolling_smoothly() && !f.window.motion_clock().armed(),
            "under reduced motion a wheel line must move the position at once");
    static_cast<void>(token);
}

// A pointer event of `action` at `position`, with the primary button for a
// press or release.
PointerEvent pointer_at(PointerAction action, Point position) {
    PointerEvent event;
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    event.position = position;
    return event;
}

// The built-in bars show hover on the part under the pointer alone, and
// pressed on the held part; a held arrow repeats on a UI timer until
// release (ADR-016, as ScrollBar). In a GlideFixture panel the vertical bar
// spans x 84 to 100: arrows 16 high at either end, the track from 16 to 84
// with an 18-high thumb at its start while the position is 0.
void test_built_in_bars_hover_press_and_repeat() {
    GlideFixture f{};
    reduce_motion(f.window);
    PointerEvent move = pointer_at(PointerAction::move, {92.0, 8.0});
    static_cast<void>(f.window.dispatch_pointer(move));
    ScrollableControl::Parts parts = (*f.panel).parts().scrollable;
    require(parts.vertical_decrement_arrow.states.contains(AnatomyState::hot) &&
                !parts.vertical_thumb.states.contains(AnatomyState::hot),
            "the arrow under the pointer must show hot");
    move.position = {92.0, 25.0};
    static_cast<void>(f.window.dispatch_pointer(move));
    parts = (*f.panel).parts().scrollable;
    require(parts.vertical_thumb.states.contains(AnatomyState::hot) &&
                !parts.vertical_decrement_arrow.states.contains(AnatomyState::hot),
            "the hover must follow the pointer onto the thumb and leave the arrow");
    move.position = {92.0, 60.0};
    static_cast<void>(f.window.dispatch_pointer(move));
    parts = (*f.panel).parts().scrollable;
    require(parts.vertical_track.states.contains(AnatomyState::hot) &&
                !parts.vertical_thumb.states.contains(AnatomyState::hot),
            "the track must show hot under the pointer");
    move.position = {60.0, 20.0};
    static_cast<void>(f.window.dispatch_pointer(move));
    parts = (*f.panel).parts().scrollable;
    require(!parts.vertical_track.states.contains(AnatomyState::hot) &&
                !parts.vertical_thumb.states.contains(AnatomyState::hot) &&
                !parts.vertical_decrement_arrow.states.contains(AnatomyState::hot) &&
                !parts.vertical_increment_arrow.states.contains(AnatomyState::hot),
            "off the bar no part may show hot");

    const double small = (*f.panel).vertical_scroll().small_change();
    const PointerEvent press = pointer_at(PointerAction::down, {92.0, 95.0});
    require(f.window.dispatch_pointer(press) && near((*f.panel).scroll_position().y, small) &&
                (*f.panel).parts().scrollable.vertical_increment_arrow.states.contains(AnatomyState::pressed) &&
                f.window.captured_control() == f.panel,
            "an arrow press must step once, capture and show the arrow pressed");
    require(f.window.next_wake().has_value(), "holding must schedule the repeat on a UI timer");
    const FrameTime first = *f.window.next_wake();
    const FramePollResult repeat = f.window.poll_frame_schedule(first);
    require(repeat.ui_timer_ticks == 1U && near((*f.panel).scroll_position().y, small * 2.0) &&
                f.window.next_wake().has_value() &&
                *f.window.next_wake() == first + scroll_repeat_interval_default,
            "each repeat must step once and schedule the next an interval later");
    const PointerEvent release = pointer_at(PointerAction::up, {92.0, 95.0});
    static_cast<void>(f.window.dispatch_pointer(release));
    require(!f.window.next_wake().has_value() && !f.window.captured_control() &&
                !(*f.panel).parts().scrollable.vertical_increment_arrow.states.contains(AnatomyState::pressed) &&
                near((*f.panel).scroll_position().y, small * 2.0),
            "release must stop the repeat, release capture and clear the press");
}

// A held track pages toward the pointer and stops once the thumb reaches
// it, as the standalone bar's track does.
void test_built_in_track_repeat_stops_at_the_pointer() {
    GlideFixture f{};
    reduce_motion(f.window);
    const PointerEvent press = pointer_at(PointerAction::down, {92.0, 70.0});
    require(f.window.dispatch_pointer(press) && (*f.panel).scroll_position().y > 0.0 &&
                (*f.panel).parts().scrollable.vertical_track.states.contains(AnatomyState::pressed),
            "a track press must page once and show the track pressed");
    int polls = 0;
    while (f.window.next_wake().has_value() && polls < 40) {
        static_cast<void>(f.window.poll_frame_schedule(*f.window.next_wake()));
        ++polls;
    }
    const Rect thumb = (*f.panel).parts().scrollable.vertical_thumb.bounds;
    require(!f.window.next_wake().has_value() && thumb.bottom() >= 70.0,
            "the track's repeat must stop once the thumb reaches the pointer");
    static_cast<void>(f.window.dispatch_pointer(pointer_at(PointerAction::up, {92.0, 70.0})));
}

// At a fractional display scale the built-in thumb's ends lie on whole
// device pixels, and the thumb is hit where it is painted.
void test_built_in_thumb_snaps_to_device_pixels() {
    GlideFixture f{};
    reduce_motion(f.window);
    f.window.set_scale(1.5);
    f.window.perform_layout();
    require((*f.panel).scroll_to({0.0, 333.0}), "the panel scrolls");
    f.window.perform_layout();
    const Rect thumb = (*f.panel).parts().scrollable.vertical_thumb.bounds;
    const double top = thumb.y * 1.5;
    const double bottom = thumb.bottom() * 1.5;
    require(!thumb.empty() && std::abs(top - std::round(top)) < 1.0e-9 &&
                std::abs(bottom - std::round(bottom)) < 1.0e-9,
            "the thumb's ends must lie on whole device pixels");
    const PointerEvent press = pointer_at(PointerAction::down, {92.0, thumb.y + 0.2});
    require(f.window.dispatch_pointer(press) &&
                (*f.panel).parts().scrollable.vertical_thumb.states.contains(AnatomyState::captured) &&
                near((*f.panel).scroll_position().y, 333.0),
            "a press just inside the painted thumb must take the thumb, not the track");
    static_cast<void>(f.window.dispatch_pointer(pointer_at(PointerAction::up, {92.0, thumb.y + 0.2})));
}

// Bars shorter than two arrows and a thumb: the arrows share the bar, the
// track and thumb are gone, and the arrows still step. The standalone bar
// keeps the same rule.
void test_short_bars_show_arrows_and_no_thumb() {
    ManualTime time{};
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("short.panel"));
    std::shared_ptr<gui_forms::Control> content = make_control<Control>(StableId("short.content"));
    (*content).set_requested_bounds({0.0, 0.0, 40.0, 1000.0});
    (*panel).add_child(content);
    (*panel).auto_scroll = true;
    Window window(panel, {100.0, 30.0});
    window.motion_clock().set_time_source(&time);
    reduce_motion(window);
    window.perform_layout();
    const ScrollableControl::Parts parts = (*panel).parts().scrollable;
    require(parts.vertical_thumb.bounds.empty() && parts.vertical_track.bounds.empty() &&
                near(parts.vertical_decrement_arrow.bounds.height, 15.0) &&
                near(parts.vertical_increment_arrow.bounds.height, 15.0),
            "a 30-high bar must split between its arrows and show no thumb");
    require(window.dispatch_pointer(pointer_at(PointerAction::down, {92.0, 25.0})) &&
                (*panel).scroll_position().y > 0.0,
            "the arrows of a short bar must still step");
    static_cast<void>(window.dispatch_pointer(pointer_at(PointerAction::up, {92.0, 25.0})));

    std::shared_ptr<ScrollBar> bar =
        make_control<ScrollBar>(StableId("short.bar"), Orientation::horizontal);
    (*bar).set_requested_bounds({0.0, 0.0, 40.0, 17.0});
    (*bar).set_range(0.0, 100.0);
    Window bar_window(bar, {40.0, 17.0});
    bar_window.perform_layout();
    require((*bar).thumb_bounds().empty() && (*bar).track_bounds().width < scroll_thumb_extent_least,
            "a standalone bar too short for the least thumb must show none");
}

// The standalone bar tracks hover per part and drops it when the pointer
// leaves.
void test_scroll_bar_hover_follows_the_part() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("hover.root"));
    std::shared_ptr<ScrollBar> bar =
        make_control<ScrollBar>(StableId("hover.bar"), Orientation::horizontal);
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 17.0});
    (*bar).set_range(0.0, 100.0);
    (*root).add_child(bar);
    Window window(root, {300.0, 60.0});
    window.perform_layout();
    static_cast<void>(window.dispatch_pointer(pointer_at(PointerAction::move, {195.0, 8.0})));
    require((*bar).parts().increment_arrow.states.contains(AnatomyState::hot) &&
                !(*bar).parts().thumb.states.contains(AnatomyState::hot),
            "the arrow under the pointer must show hot");
    const Rect thumb = (*bar).thumb_bounds();
    static_cast<void>(window.dispatch_pointer(
        pointer_at(PointerAction::move, {thumb.x + thumb.width * 0.5, 8.0})));
    require((*bar).parts().thumb.states.contains(AnatomyState::hot) &&
                !(*bar).parts().increment_arrow.states.contains(AnatomyState::hot),
            "the hover must move to the thumb and leave the arrow");
    static_cast<void>(window.dispatch_pointer(pointer_at(PointerAction::move, {250.0, 40.0})));
    require(!(*bar).parts().thumb.states.contains(AnatomyState::hot) &&
                !(*bar).parts().track.states.contains(AnatomyState::hot),
            "leaving the bar must drop the hover");
}

// Focus moving to a control inside an auto-scrolling panel scrolls it into
// view (Windows Forms' ScrollControlIntoView on activation): a glide on the
// motion clock, at once under reduced motion; enclosing scrollers then
// reveal the inner one. A panel without auto scroll does not move.
void test_focus_scrolls_the_focused_control_into_view() {
    ManualTime time{};
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("reveal.panel"));
    std::shared_ptr<gui_forms::Button> near_button = make_control<Button>(StableId("reveal.near"), "Near");
    (*near_button).set_requested_bounds({0.0, 0.0, 60.0, 20.0});
    std::shared_ptr<gui_forms::Button> far_button = make_control<Button>(StableId("reveal.far"), "Far");
    (*far_button).set_requested_bounds({0.0, 400.0, 60.0, 20.0});
    (*panel).add_child(near_button);
    (*panel).add_child(far_button);
    (*panel).auto_scroll = true;
    Window window(panel, {100.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    require(window.request_focus(far_button) && (*panel).scrolling_smoothly() &&
                near((*panel).scroll_position().y, 0.0) &&
                (*panel).scroll_target().y >= 320.0 && (*panel).scroll_target().y <= 400.0,
            "focusing a control below the viewport must glide toward revealing it");
    advance_frames(window, time, 60);
    window.perform_layout();
    const Rect far_bounds = (*far_button).committed_arranged_bounds();
    require(!(*panel).scrolling_smoothly() && far_bounds.y >= 0.0 && far_bounds.bottom() <= 100.0,
            "the glide must settle with the focused control in view");
    reduce_motion(window);
    require(window.request_focus(near_button) && near((*panel).scroll_position().y, 0.0) &&
                !(*panel).scrolling_smoothly(),
            "under reduced motion focus must reveal the control at once");

    // Nested: the inner panel reveals the button, the outer the inner panel.
    std::shared_ptr<gui_forms::Panel> outer = make_control<Panel>(StableId("reveal.outer"));
    std::shared_ptr<gui_forms::Panel> inner = make_control<Panel>(StableId("reveal.inner"));
    std::shared_ptr<gui_forms::Button> deep = make_control<Button>(StableId("reveal.deep"), "Deep");
    (*deep).set_requested_bounds({0.0, 300.0, 40.0, 20.0});
    (*inner).add_child(deep);
    (*inner).auto_scroll = true;
    (*inner).set_requested_bounds({0.0, 300.0, 80.0, 80.0});
    (*outer).add_child(inner);
    (*outer).auto_scroll = true;
    Window nested(outer, {100.0, 100.0});
    reduce_motion(nested);
    nested.perform_layout();
    require(nested.request_focus(deep) && (*inner).scroll_position().y > 0.0 &&
                (*outer).scroll_position().y > 0.0,
            "focus must scroll every enclosing auto-scroll panel");

    // Without auto scroll a panel keeps its position.
    std::shared_ptr<gui_forms::Panel> plain = make_control<Panel>(StableId("reveal.plain"));
    std::shared_ptr<gui_forms::Button> hidden = make_control<Button>(StableId("reveal.hidden"), "Hidden");
    (*hidden).set_requested_bounds({0.0, 300.0, 40.0, 20.0});
    (*plain).add_child(hidden);
    Window plain_window(plain, {100.0, 100.0});
    plain_window.perform_layout();
    require(plain_window.request_focus(hidden) && near((*plain).scroll_position().y, 0.0),
            "a panel without auto scroll must not scroll on focus");
}

int main() {
    try {
        test_control_family_and_manual_axis_contract();
        test_auto_scroll_projection_viewport_and_resize();
        test_scroll_gestures_events_semantics_and_state();
        test_control_into_view_offset_and_minimum_extent();
        test_scroll_bar_press_and_hold_repeats_on_a_ui_timer();
        test_scroll_copies_damage_only_the_exposed_band();
        test_damage_moves_with_a_scroll_copy();
        test_wheel_glides_on_the_motion_clock();
        test_wheel_scrolls_off_leaves_the_wheel_to_the_consumer();
        test_bar_glides_and_immediate_input_stops_it();
        test_built_in_bars_hover_press_and_repeat();
        test_built_in_track_repeat_stops_at_the_pointer();
        test_built_in_thumb_snaps_to_device_pixels();
        test_short_bars_show_arrows_and_no_thumb();
        test_scroll_bar_hover_follows_the_part();
        test_focus_scrolls_the_focused_control_into_view();
        std::cout << "gui_forms_scrollable_control_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_scrollable_control_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
