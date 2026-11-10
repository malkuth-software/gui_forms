// Audit addendum batch d (tree cost): the walks that were proportional to
// the whole tree are replaced by ones proportional to the change. These
// tests hold the observable behaviour those walks gave:
//
// - N77/N88: attaching, removing, disposing and clearing children leave
//   every ancestor's subtree flags exact (refresh_ancestor_summaries, the
//   bulk clear_children);
// - N78: a removed subtree that holds the focus gives it up (the upward
//   contains_control);
// - N79: a semantic action by runtime identity reaches its control, and a
//   detached control's identity reaches nothing (the control table's index);
// - N80: removing a control damages its descendants that overflow it;
// - N81: a press and release over a control still clicks it;
// - N83/N84: hit tests and paints place nested controls at their window
//   positions;
// - N85: a suspended layout request commits once its subtree is laid out;
// - N86: the display cache count follows paints, removals and re-attachment;
// - N87: inherited values follow writes of every input and reparenting.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/surface_root/control_table/control_table.hpp"
#include "support/clip_tracking_painter.hpp"
#include "support/named_callbacks.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "tree cost failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::shared_ptr<Panel> make_panel(const std::string& id, Rect bounds) {
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
    (*panel).set_requested_bounds(bounds);
    return panel;
}

bool has_layout(Dirty flags) {
    const bool layout = has_dirty(flags, Dirty::measure) || has_dirty(flags, Dirty::arrange);
    return layout;
}

// N77/N88: the subtree flags of the root follow attachment and removal
// without a walk of the whole tree.
void test_ancestor_flags_follow_structure() {
    std::shared_ptr<Panel> root = make_panel("flags.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> group = make_panel("flags.group", {0.0, 0.0, 200.0, 100.0});
    std::vector<std::shared_ptr<Panel>> cells;
    for (int index = 0; index < 50; ++index) {
        std::shared_ptr<Panel> cell = make_panel("flags.cell." + std::to_string(index),
                                                 {static_cast<double>(index % 10) * 10.0,
                                                  static_cast<double>(index / 10) * 10.0, 8.0, 8.0});
        (*group).add_child(cell);
        cells.push_back(cell);
    }
    (*root).add_child(group);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    require(!has_layout((*root).subtree_dirty()), "a laid-out window has no layout pending");

    std::shared_ptr<Panel> extra = make_panel("flags.extra", {0.0, 0.0, 10.0, 10.0});
    (*group).add_child(extra);
    require(has_layout((*root).subtree_dirty()) && has_layout((*group).subtree_dirty()),
            "an attached child's layout reaches every ancestor");
    window.perform_layout();
    require(!has_layout((*root).subtree_dirty()), "the pass clears it again on the way back up");

    // A pending layout below the removed child only: removal leaves none.
    (*extra).invalidate(Dirty::measure);
    static_cast<void>((*group).remove_child((*extra).runtime_id()));
    window.perform_layout();
    require(!has_layout((*root).subtree_dirty()) && !has_layout((*group).subtree_dirty()),
            "a removed child's flags leave its former ancestors");

    (*group).clear_children();
    require((*group).children().empty(), "clear_children removes every child");
    for (const std::shared_ptr<Panel>& cell : cells) {
        require(!(*cell).attached() && !(*cell).parent(), "each cleared child is detached and parentless");
    }
    window.perform_layout();
    require(!has_layout((*root).subtree_dirty()), "the cleared group lays out and leaves nothing pending");
    // Cleared children are whole controls again: they attach elsewhere.
    (*root).add_child(cells.front());
    require((*cells.front()).attached() && window.find("flags.cell.0") == cells.front(),
            "a cleared child attaches again under its stable identity");
}

// N78: removing a subtree that holds the focus takes it away.
void test_removed_subtree_gives_up_focus() {
    std::shared_ptr<Panel> root = make_panel("focus.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> group = make_panel("focus.group", {0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> inner = make_panel("focus.inner", {0.0, 0.0, 100.0, 50.0});
    std::shared_ptr<Panel> target = make_panel("focus.target", {0.0, 0.0, 20.0, 20.0});
    std::shared_ptr<Panel> other = make_panel("focus.other", {0.0, 150.0, 20.0, 20.0});
    (*target).focusable = true;
    (*other).focusable = true;
    (*inner).add_child(target);
    (*group).add_child(inner);
    (*root).add_child(group);
    (*root).add_child(other);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    require(window.request_focus(target) && window.focused_control() == target, "the target takes the focus");
    static_cast<void>((*root).remove_child((*other).runtime_id()));
    require(window.focused_control() == target, "removing an unrelated subtree keeps the focus");
    static_cast<void>((*root).remove_child((*group).runtime_id()));
    require(window.focused_control() != target, "removing the subtree that holds the focus takes it");
}

// N79: a semantic action by runtime identity.
void test_semantic_action_by_runtime_identity() {
    std::shared_ptr<Panel> root = make_panel("runtime.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> target = make_panel("runtime.target", {10.0, 10.0, 40.0, 20.0});
    (*target).focusable = true;
    (*root).add_child(target);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    const RuntimeId identity = (*target).runtime_id();
    require(window.control_table().control_with_runtime_id(identity.value) == target.get(),
            "the control table finds an attached control by runtime identity");
    require(window.perform_semantic_action(identity, std::string_view{}, SemanticAction::focus) &&
                window.focused_control() == target,
            "a focus action by runtime identity reaches the control");
    Control::Ptr removed = (*root).remove_child(identity);
    require(window.control_table().control_with_runtime_id(identity.value) == nullptr,
            "a detached control's identity is no longer indexed");
    require(!window.perform_semantic_action(identity, std::string_view{}, SemanticAction::focus),
            "a detached control's identity reaches nothing");
    (*root).add_child(removed);
    require(window.control_table().control_with_runtime_id(identity.value) == target.get(),
            "re-attaching indexes the identity again");
}

// N80: a child that overflows its parent is damaged when the parent leaves.
void test_removal_damages_overflowing_descendants() {
    std::shared_ptr<Panel> root = make_panel("damage.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> parent = make_panel("damage.parent", {0.0, 0.0, 50.0, 50.0});
    std::shared_ptr<Panel> overflow = make_panel("damage.overflow", {40.0, 40.0, 40.0, 40.0});
    std::shared_ptr<Panel> inside = make_panel("damage.inside", {5.0, 5.0, 10.0, 10.0});
    (*parent).add_child(overflow);
    (*parent).add_child(inside);
    (*root).add_child(parent);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    static_cast<void>((*root).remove_child((*parent).runtime_id()));
    const DamageRegion damage = window.take_damage();
    const Rect bounds = damage.bounds();
    require(bounds.x <= 0.0 && bounds.y <= 0.0 && bounds.x + bounds.width >= 80.0 &&
                bounds.y + bounds.height >= 80.0,
            "the removed parent's damage reaches its overflowing child");
}

// N83/N84: nested controls are hit at their window positions.
void test_nested_hit_tests() {
    std::shared_ptr<Panel> root = make_panel("hit.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> outer = make_panel("hit.outer", {100.0, 50.0, 150.0, 100.0});
    std::shared_ptr<Panel> middle = make_panel("hit.middle", {20.0, 10.0, 100.0, 60.0});
    std::shared_ptr<Panel> leaf = make_panel("hit.leaf", {30.0, 20.0, 20.0, 20.0});
    (*middle).add_child(leaf);
    (*outer).add_child(middle);
    (*root).add_child(outer);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    require(window.hit_test(Point{155.0, 85.0}) == leaf, "the leaf is hit at its window position (150, 80)");
    require(window.hit_test(Point{145.0, 85.0}) == middle, "left of the leaf the middle panel is hit");
    require(window.hit_test(Point{105.0, 55.0}) == outer, "inside the outer panel only, it is hit");
    require(window.hit_test(Point{50.0, 20.0}) == root, "outside every child the root is hit");
}

// N85: a request made while suspended commits after the pass that lays its
// subtree out.
void test_suspended_request_commits() {
    std::shared_ptr<Panel> root = make_panel("suspend.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> group = make_panel("suspend.group", {0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> child = make_panel("suspend.child", {0.0, 0.0, 20.0, 20.0});
    (*group).add_child(child);
    (*root).add_child(group);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    (*group).suspend_layout();
    (*child).set_requested_bounds({10.0, 10.0, 30.0, 30.0});
    const LayoutTransactionState suspended = (*group).layout_transaction_state();
    require(suspended.deferred, "a write below a suspended control defers its request");
    (*group).resume_layout(false);
    window.perform_layout();
    const LayoutTransactionState committed = (*group).layout_transaction_state();
    require(!committed.deferred && committed.committed_revision == committed.requested_revision,
            "the pass that lays the subtree out commits the request");
}

// N86: the display cache count without a tree walk.
void test_display_cache_count() {
    std::shared_ptr<Panel> root = make_panel("cache.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> group = make_panel("cache.group", {0.0, 0.0, 200.0, 100.0});
    for (int index = 0; index < 3; ++index) {
        (*group).add_child(make_panel("cache.cell." + std::to_string(index),
                                      {static_cast<double>(index) * 20.0, 0.0, 15.0, 15.0}));
    }
    (*root).add_child(group);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    test_support::ClipTrackingPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 300.0, 200.0}));
    const std::uint64_t painted = window.metrics_snapshot().display_cache_entries;
    require(painted == 5U, "every painted control holds one chunk");
    Control::Ptr removed = (*root).remove_child((*group).runtime_id());
    require(window.metrics_snapshot().display_cache_entries == 1U,
            "a removed subtree's chunks leave the count");
    (*root).add_child(removed);
    require(window.metrics_snapshot().display_cache_entries == 5U,
            "a re-attached subtree brings its kept chunks back");
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 300.0, 200.0}));
    require(window.metrics_snapshot().display_cache_entries == 5U, "repainting replaces chunks one for one");
    (*group).dispose();
    require(window.metrics_snapshot().display_cache_entries == 1U, "a disposed subtree's chunks leave the count");
}

// N87: inherited values follow every input.
void test_inherited_values() {
    std::shared_ptr<Panel> root = make_panel("inherit.root", {0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<Panel> middle = make_panel("inherit.middle", {0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> leaf = make_panel("inherit.leaf", {0.0, 0.0, 20.0, 20.0});
    std::shared_ptr<Panel> elsewhere = make_panel("inherit.elsewhere", {0.0, 120.0, 100.0, 50.0});
    (*middle).add_child(leaf);
    (*root).add_child(middle);
    (*root).add_child(elsewhere);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();
    require((*leaf).effectively_visible() && (*leaf).effectively_enabled(), "a leaf starts visible and enabled");
    require((*leaf).effective_cursor() == CursorKind::arrow &&
                (*leaf).effective_layout_direction() == LayoutDirection::left_to_right &&
                !(*leaf).ambient_back_color() && !(*leaf).on_ceremonial_surface(),
            "a leaf starts with the defaults");

    (*middle).visible = false;
    require(!(*leaf).effectively_visible(), "hiding an ancestor hides the leaf");
    (*middle).visible = true;
    require((*leaf).effectively_visible(), "showing it again shows the leaf");
    (*root).enabled = false;
    require(!(*leaf).effectively_enabled(), "disabling the root disables the leaf");
    (*root).enabled = true;
    require((*leaf).effectively_enabled(), "enabling it again enables the leaf");

    (*middle).cursor = CursorKind::hand;
    require((*leaf).effective_cursor() == CursorKind::hand, "a cursor is inherited");
    (*leaf).cursor = CursorKind::text;
    require((*leaf).effective_cursor() == CursorKind::text, "a cursor of its own wins");
    (*root).layout_direction = LayoutDirection::right_to_left;
    require((*leaf).effective_layout_direction() == LayoutDirection::right_to_left, "a direction is inherited");
    const Color shade{10, 20, 30, 255};
    (*middle).back_color = shade;
    require((*leaf).ambient_back_color() == std::optional<Color>(shade), "a back colour is inherited");
    (*root).ceremonial = true;
    require((*leaf).on_ceremonial_surface(), "a ceremonial ancestor makes the surface ceremonial");

    // Reparenting: the leaf takes its new ancestors' values.
    Control::Ptr moved = (*middle).remove_child((*leaf).runtime_id());
    (*elsewhere).add_child(moved);
    require((*leaf).ambient_back_color() == std::nullopt, "the old parent's colour stays behind");
    require((*leaf).effective_cursor() == CursorKind::text && (*leaf).on_ceremonial_surface() &&
                (*leaf).effective_layout_direction() == LayoutDirection::right_to_left,
            "the values the new ancestors give hold after the move");
    (*elsewhere).visible = false;
    require(!(*leaf).effectively_visible(), "the new parent's visibility governs");
    (*elsewhere).dispose();
    require(!(*leaf).effectively_visible() && !(*leaf).effectively_enabled(),
            "a disposed control is neither visible nor enabled");
}

// N81: a press and release over a control is one click.
void test_press_release_clicks() {
    std::shared_ptr<Button> button = make_control<Button>(StableId("click.button"), "Click");
    (*button).set_requested_bounds({10.0, 10.0, 80.0, 24.0});
    std::shared_ptr<Panel> root = make_panel("click.root", {0.0, 0.0, 200.0, 100.0});
    (*root).add_child(button);
    Window window(root, Size{200.0, 100.0});
    window.perform_layout();
    std::uint64_t clicks = 0U;
    SubscriptionToken token = (*button).clicked().subscribe(
        test_support::IncrementCounter<std::uint64_t, ButtonBase&>(clicks));
    const Point inside{40.0, 20.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, inside}));
    require(clicks == 1U, "a press and release over the button clicks it once");
    const Point outside{150.0, 80.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, outside}));
    require(clicks == 1U, "a release elsewhere does not click");
}

} // namespace

int main() {
    test_ancestor_flags_follow_structure();
    test_removed_subtree_gives_up_focus();
    test_semantic_action_by_runtime_identity();
    test_removal_damages_overflowing_descendants();
    test_nested_hit_tests();
    test_suspended_request_commits();
    test_display_cache_count();
    test_inherited_values();
    test_press_release_clicks();
    std::cout << "tree cost tests passed\n";
    return EXIT_SUCCESS;
}
