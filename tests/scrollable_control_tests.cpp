#include "gui_forms/gui_forms.hpp"

#include <algorithm>
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

// A host that keeps its pixels (scroll copies on): a scroll that only moves
// the viewport's children by whole device pixels records one copy and
// damages the band it exposed and the bar, not the viewport; a control
// over the viewport is repainted where it is and where the copy moved it.
void test_scroll_copies_damage_only_the_exposed_band() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("copy.root"));
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
    const double band_and_bar = 84.0 * 10.0 + 16.0 * 80.0;
    require(damage.area() <= band_and_bar + 1.0e-6 && damage.area() >= 84.0 * 10.0,
            "a copied scroll must damage only the exposed band and the bar");
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
                covered.area() <= 84.0 * 12.0 + 16.0 * 80.0 + repainted_cover + 1.0e-6,
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
        std::cout << "gui_forms_scrollable_control_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_scrollable_control_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
