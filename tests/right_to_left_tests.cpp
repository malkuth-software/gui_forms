// Control::right_to_left (owner ruling of 2026-11-11): Windows Forms'
// inherited ambient RightToLeft, with No, Yes and Inherit, over the one
// stored layout_direction. Resolution through the parent chain to the
// top-level window, the registry's `right_to_left` spelling, the
// right_to_left_changed event's scope and order (a write, a removal and a
// reparenting), the typed dirtiness a change marks, and directional
// controls that follow the resolved value: the track bar, the horizontal
// scroll bar, the combo box's drop button and the check box's well. The
// barber pole's direction is tested with the other progress effects
// (tests/effect_program_tests.cpp, progress_bar_follows_right_to_left).

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap, as tests/layout_direction_tests.cpp
// does, to keep main's inlined frame small under the sanitizer.

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Appends "<name>:rtl" or "<name>:ltr" to `log` for each
// right_to_left_changed it receives. Both pointers borrow the test's
// locals, which outlive the subscription.
struct DirectionRecorder final {
    std::vector<std::string>* log{};
    const char* name{};

    void operator()(bool right_to_left) const {
        std::string entry{name};
        entry += right_to_left ? ":rtl" : ":ltr";
        (*log).push_back(std::move(entry));
    }
};

std::shared_ptr<Panel> make_panel(const char* id) {
    std::shared_ptr<Panel> made = make_control<Panel>(StableId(std::string(id)));
    return made;
}

bool resolves_right_to_left(const Control& control) {
    const bool right_to_left = control.effective_layout_direction() == LayoutDirection::right_to_left;
    return right_to_left;
}

BindingValue right_to_left_value(const char* choice, std::int64_t value) {
    const BindingValue spelled{PropertyEnumValue{"right_to_left", choice, value}};
    return spelled;
}

void test_resolution_through_the_parent_chain() {
    std::shared_ptr<Panel> root = make_panel("rtl.resolve.root");
    std::shared_ptr<Panel> middle = make_panel("rtl.resolve.middle");
    std::shared_ptr<Panel> leaf = make_panel("rtl.resolve.leaf");
    std::shared_ptr<Panel> blocked = make_panel("rtl.resolve.blocked");
    std::shared_ptr<Panel> blocked_leaf = make_panel("rtl.resolve.blocked_leaf");
    (*root).add_child(middle);
    (*middle).add_child(leaf);
    (*middle).add_child(blocked);
    (*blocked).add_child(blocked_leaf);
    (*blocked).layout_direction = LayoutDirection::left_to_right;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    (*held_window).perform_layout();

    require(!resolves_right_to_left(*root) && !resolves_right_to_left(*leaf) &&
                (*leaf).property_value("right_to_left") == std::optional<BindingValue>(right_to_left_value("inherit", 2)),
            "every control inherits by default and the top-level window's default is No");

    (*root).layout_direction = layout_direction_of(RightToLeft::yes);
    require(resolves_right_to_left(*middle) && resolves_right_to_left(*leaf),
            "Inherit resolves through the parent chain to the nearest Yes");
    require(!resolves_right_to_left(*blocked) && !resolves_right_to_left(*blocked_leaf),
            "an explicit No holds for its own subtree under a Yes ancestor");
    require((*root).property_value("right_to_left") == std::optional<BindingValue>(right_to_left_value("yes", 1)),
            "the registry reads the stored direction in RightToLeft's spelling");

    (*blocked).set_property_value("right_to_left", right_to_left_value("inherit", 2));
    require((*blocked).layout_direction.read() == LayoutDirection::inherit && resolves_right_to_left(*blocked_leaf),
            "writing right_to_left writes the one stored direction");
    (*root).set_property_value("right_to_left", right_to_left_value("no", 0));
    require((*root).layout_direction.read() == LayoutDirection::left_to_right && !resolves_right_to_left(*leaf) &&
                !resolves_right_to_left(*blocked_leaf),
            "No at the root lays the whole tree out left to right");

    require(right_to_left_of(LayoutDirection::right_to_left) == RightToLeft::yes &&
                right_to_left_of(LayoutDirection::left_to_right) == RightToLeft::no &&
                right_to_left_of(LayoutDirection::inherit) == RightToLeft::inherit &&
                layout_direction_of(RightToLeft::no) == LayoutDirection::left_to_right &&
                layout_direction_of(RightToLeft::inherit) == LayoutDirection::inherit,
            "the two spellings convert both ways");
}

void test_change_event_scope_and_order() {
    std::shared_ptr<Panel> root = make_panel("rtl.event.root");
    std::shared_ptr<Panel> first = make_panel("rtl.event.first");
    std::shared_ptr<Panel> first_leaf = make_panel("rtl.event.first_leaf");
    std::shared_ptr<Panel> second = make_panel("rtl.event.second");
    std::shared_ptr<Panel> second_leaf = make_panel("rtl.event.second_leaf");
    std::shared_ptr<Panel> third = make_panel("rtl.event.third");
    // add_child puts each new child on top, the end of painter order: first,
    // second, third in painter order.
    (*root).add_child(first);
    (*first).add_child(first_leaf);
    (*root).add_child(second);
    (*second).add_child(second_leaf);
    (*root).add_child(third);
    (*second).layout_direction = LayoutDirection::left_to_right;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    (*held_window).perform_layout();

    std::vector<std::string> log{};
    const SubscriptionToken root_token = (*root).right_to_left_changed().subscribe(DirectionRecorder{&log, "root"});
    const SubscriptionToken first_token = (*first).right_to_left_changed().subscribe(DirectionRecorder{&log, "first"});
    const SubscriptionToken first_leaf_token =
        (*first_leaf).right_to_left_changed().subscribe(DirectionRecorder{&log, "first_leaf"});
    const SubscriptionToken second_token =
        (*second).right_to_left_changed().subscribe(DirectionRecorder{&log, "second"});
    const SubscriptionToken second_leaf_token =
        (*second_leaf).right_to_left_changed().subscribe(DirectionRecorder{&log, "second_leaf"});
    const SubscriptionToken third_token = (*third).right_to_left_changed().subscribe(DirectionRecorder{&log, "third"});

    (*root).layout_direction = LayoutDirection::right_to_left;
    const std::vector<std::string> written{"root:rtl", "first:rtl", "first_leaf:rtl", "third:rtl"};
    require(log == written,
            "a write raises on every control whose resolved value changed, an ancestor before its descendants, "
            "siblings in painter order, and not under an explicit No");
    require(resolves_right_to_left(*first_leaf), "every resolved value is updated before the events run");

    log.clear();
    (*root).layout_direction = LayoutDirection::right_to_left;
    (*second).layout_direction = LayoutDirection::left_to_right;
    require(log.empty(), "an equal write raises nothing");
    (*first).layout_direction = LayoutDirection::right_to_left;
    require(log.empty(), "an explicit Yes under a Yes parent leaves the resolved value and raises nothing");
    (*first).layout_direction = LayoutDirection::inherit;

    const Control::Ptr removed = (*root).remove_child((*first).runtime_id());
    const std::vector<std::string> detached{"first:ltr", "first_leaf:ltr"};
    require(log == detached, "a removed subtree resolves left to right as a root, and says so");

    log.clear();
    (*root).add_child(removed);
    const std::vector<std::string> attached{"first:rtl", "first_leaf:rtl"};
    require(log == attached, "an attached subtree takes its new parent's direction, and says so");

    // A move between two right-to-left parents changes nothing; the
    // removal from the old parent inside the move announces nothing.
    log.clear();
    (*third).layout_direction = LayoutDirection::right_to_left;
    (*third).add_child(first);
    require(log.empty() && resolves_right_to_left(*first_leaf),
            "a reparenting under the same resolved value raises nothing");
    (*second).add_child(first);
    const std::vector<std::string> moved{"first:ltr", "first_leaf:ltr"};
    require(log == moved, "a reparenting under another resolved value raises once per changed control");
}

void test_change_marks_typed_dirtiness() {
    std::shared_ptr<Panel> root = make_panel("rtl.dirty.root");
    std::shared_ptr<Panel> child = make_panel("rtl.dirty.child");
    std::shared_ptr<Panel> grandchild = make_panel("rtl.dirty.grandchild");
    (*root).add_child(child);
    (*child).add_child(grandchild);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();

    (*root).layout_direction = LayoutDirection::right_to_left;
    const Dirty marked = (*grandchild).dirty();
    require(has_dirty(marked, Dirty::arrange) && has_dirty(marked, Dirty::paint) &&
                has_dirty(marked, Dirty::hit_test) && !has_dirty(marked, Dirty::measure),
            "a resolved change marks arrange, paint and hit-test dirty, never measure");
    window.perform_layout();
    (*child).layout_direction = LayoutDirection::right_to_left;
    require(!has_dirty((*grandchild).dirty(), Dirty::layout),
            "a write that leaves the resolved value as it was marks no layout below it");
}

// A control laid out at its requested bounds in a root panel, which owns
// it, in its own window; set_root_direction writes the root's direction.
struct Hosted final {
    std::shared_ptr<Panel> root{};
    std::unique_ptr<Window> window{};
};

Hosted host(const Control::Ptr& control, Rect bounds, const char* id) {
    Hosted hosted{};
    hosted.root = make_panel(id);
    (*control).set_requested_bounds(bounds);
    (*control).margin = Insets{0.0, 0.0, 0.0, 0.0};
    (*hosted.root).add_child(control);
    hosted.window = std::make_unique<Window>(hosted.root, Size{300.0, 100.0});
    (*hosted.window).perform_layout();
    return hosted;
}

void set_root_direction(Hosted& hosted, LayoutDirection direction) {
    (*hosted.root).layout_direction = direction;
    (*hosted.window).perform_layout();
}

void test_track_bar_follows() {
    std::shared_ptr<TrackBar> bar = make_control<TrackBar>(StableId("rtl.track"));
    (*bar).orientation = Orientation::horizontal;
    (*bar).value = 0.0;
    Hosted hosted = host(bar, {0.0, 0.0, 200.0, 30.0}, "rtl.track.root");
    const Rect forward = (*bar).parts().thumb.bounds;
    set_root_direction(hosted, LayoutDirection::right_to_left);
    const Rect mirrored = (*bar).parts().thumb.bounds;
    const double forward_centre = forward.x + forward.width * 0.5;
    const double mirrored_centre = mirrored.x + mirrored.width * 0.5;
    require(forward_centre < 100.0 && mirrored_centre > 100.0 && forward_centre + mirrored_centre == 200.0,
            "a right-to-left track bar puts its minimum at the right edge");
    KeyEvent left{};
    left.physical_key = PhysicalKey::left;
    (*bar).on_key(left);
    require(left.handled && (*bar).value.read() > 0.0, "on a right-to-left track bar Left moves the thumb left: up");
}

void test_scroll_bar_follows() {
    std::shared_ptr<HScrollBar> bar = make_control<HScrollBar>(StableId("rtl.scroll"));
    (*bar).value = 0.0;
    Hosted hosted = host(bar, {0.0, 0.0, 180.0, 18.0}, "rtl.scroll.root");
    const ScrollBar::Parts forward = (*bar).parts();
    require(forward.thumb.bounds.x == forward.track.bounds.x, "left to right, the minimum's thumb is at the left");
    set_root_direction(hosted, LayoutDirection::right_to_left);
    const ScrollBar::Parts mirrored = (*bar).parts();
    require(mirrored.thumb.bounds.right() == mirrored.track.bounds.right() &&
                mirrored.decrement_arrow.bounds.x >= mirrored.increment_arrow.bounds.x,
            "a right-to-left scroll bar puts its minimum, and its decrement button, at the right");
}

void test_combo_box_follows() {
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("rtl.combo"));
    Hosted hosted = host(combo, {0.0, 0.0, 120.0, 24.0}, "rtl.combo.root");
    require((*combo).parts().drop_down.bounds.x == 96.0, "left to right, the drop button is at the right");
    set_root_direction(hosted, LayoutDirection::right_to_left);
    require((*combo).parts().drop_down.bounds.x == 1.0, "right to left, the drop button is at the left");
}

void test_check_box_follows() {
    std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("rtl.check"), "Option");
    (*check).auto_size = false;
    Hosted hosted = host(check, {0.0, 0.0, 120.0, 20.0}, "rtl.check.root");
    require((*check).parts().well.bounds.x == 1.0, "left to right, the box is at the left");
    set_root_direction(hosted, LayoutDirection::right_to_left);
    require((*check).parts().well.bounds.right() == 119.0, "right to left, the box is at the right");
}

} // namespace

int main() {
    try {
        test_resolution_through_the_parent_chain();
        test_change_event_scope_and_order();
        test_change_marks_typed_dirtiness();
        test_track_bar_follows();
        test_scroll_bar_follows();
        test_combo_box_follows();
        test_check_box_follows();
        std::cout << "right-to-left-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "right-to-left-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
