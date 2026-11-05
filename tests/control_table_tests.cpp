// Property-capabilities migration M0: the surface root's control table.
// Every attached control holds a live slot; a detached control's slot is
// released, its generation advanced so that the old slot is rejected, and
// reused by the next attachment; writes touch their control's bit by kind
// and a read of settled state (a layout pass, a paint) drains the bits.
// M0.2: a write only touches; the settle before a read does the ancestor
// marks and places the damage at the bounds as they are then.
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/surface_root/control_table/control_table.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using detail::ControlSlot;
using detail::ControlTable;
using detail::DirtyBitset;
using detail::DirtyKind;
using detail::PendingTouch;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "control table failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

// The bitset alone: set bits come back in increasing order across words and
// summary groups, and taking them clears them.
void test_bitset_take_order() {
    DirtyBitset bits;
    bits.reserve(5000U);
    expect(!bits.any(), "a new bitset has no bit set");
    const std::vector<std::uint32_t> wanted{0U, 63U, 64U, 4095U, 4096U, 4999U};
    for (std::vector<std::uint32_t>::const_reverse_iterator index = wanted.rbegin(); index != wanted.rend(); ++index) {
        bits.set(*index);
    }
    expect(bits.test(4096U) && !bits.test(4097U), "test reads single bits");
    bits.reset(63U);
    expect(!bits.test(63U), "reset clears one bit");
    std::vector<std::uint32_t> taken{};
    const std::size_t count = bits.take(taken);
    const std::vector<std::uint32_t> expected{0U, 64U, 4095U, 4096U, 4999U};
    expect(count == expected.size() && taken == expected, "take appends the set indices in order");
    expect(!bits.any(), "take clears what it took");
    bits.set(7U);
    bits.clear();
    expect(!bits.any() && !bits.test(7U), "clear empties the bitset");
}

// The table alone: slots are dense, released ones are reused last in, first
// out with a new generation, and a stale slot reaches nothing.
void test_table_slots() {
    std::shared_ptr<Panel> first = make_control<Panel>(StableId("table.first"));
    std::shared_ptr<Panel> second = make_control<Panel>(StableId("table.second"));
    std::shared_ptr<Panel> third = make_control<Panel>(StableId("table.third"));
    ControlTable table;
    const ControlSlot a = table.assign(*first);
    const ControlSlot b = table.assign(*second);
    expect(a.index == 0U && b.index == 1U, "slots are dense");
    expect(table.live_count() == 2U && table.slot_count() == 2U, "two live slots");
    table.touch(b, Dirty::paint | Dirty::semantics, PendingTouch{});
    expect(table.pending(b, DirtyKind::paint) && table.pending(b, DirtyKind::other) &&
               !table.pending(b, DirtyKind::layout),
           "a touch sets the kinds it reaches");
    table.set_resize_response(b, ResizeAxes::horizontal);
    table.release(b);
    expect(table.control_at(b) == nullptr, "a released slot reaches no control");
    expect(!table.pending(DirtyKind::paint), "release clears the slot's bits");
    table.touch(b, Dirty::paint, PendingTouch{});
    expect(!table.pending(DirtyKind::paint), "a stale slot's touch changes nothing");
    const ControlSlot c = table.assign(*third);
    expect(c.index == b.index && c.generation != b.generation, "the released index is reused with a new generation");
    expect(table.control_at(c) == third.get() && table.control_at(b) == nullptr, "the old slot stays stale");
    expect(table.resize_response(c) == ResizeAxes::none, "a reused slot starts without a registration");
    expect(table.live_count() == 2U && table.slot_count() == 2U, "reuse does not grow the table");
}

// A window: attach assigns, detach releases, re-attach reuses; a mark
// touches; a layout pass drains.
void test_window_lifecycle() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("table.root"));
    std::shared_ptr<Panel> child = make_control<Panel>(StableId("table.child"));
    std::shared_ptr<Panel> grandchild = make_control<Panel>(StableId("table.grandchild"));
    (*child).add_child(grandchild);
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    const ControlTable& table = window.control_table();
    expect(table.live_count() == 3U, "every attached control holds a slot");
    const ControlSlot child_slot = (*child).surface_slot();
    expect(table.control_at(child_slot) == child.get(), "the child's slot reaches the child");

    // A layout pass settles first; the marks it makes itself (the arranged
    // controls' paint) wait for the next read, here taking the damage.
    window.perform_layout();
    static_cast<void>(window.take_damage());
    expect(!table.pending(DirtyKind::layout) && !table.pending(DirtyKind::paint) &&
               !table.pending(DirtyKind::other),
           "a layout pass and taking the damage leave nothing pending");
    (*child).invalidate(Dirty::paint);
    expect(table.pending(child_slot, DirtyKind::paint), "invalidate touches the control's paint bit");
    static_cast<void>(window.take_damage());
    expect(!table.pending(DirtyKind::paint), "the next read drains it");

    Control::Ptr removed = (*root).remove_child((*child).runtime_id());
    expect(table.live_count() == 1U, "a detached subtree releases its slots");
    expect(table.control_at(child_slot) == nullptr, "the detached child's old slot is stale");
    expect(!(*child).surface_slot().assigned(), "a detached control holds no slot");
    (*root).add_child(std::move(removed));
    expect(table.live_count() == 3U && table.slot_count() == 3U, "re-attaching reuses the released slots");
    expect(table.control_at((*grandchild).surface_slot()) == grandchild.get(),
           "the re-attached grandchild's slot reaches it");
}

// M0.2: the bookkeeping waits for the settle. A paint touch leaves its bit
// pending until a read; the read sets the ancestors' flags and places the
// damage. Two touches of one control settle once: the marks are counted
// at the writes, the damage area once. Localized damage lands at the
// control's place in the window. A detach settles first.
void test_settle_defers_bookkeeping() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("settle.root"));
    std::shared_ptr<Panel> child = make_control<Panel>(StableId("settle.child"));
    std::shared_ptr<Panel> grandchild = make_control<Panel>(StableId("settle.grandchild"));
    (*child).set_bounds(Rect{10.0, 20.0, 100.0, 50.0});
    (*grandchild).set_bounds(Rect{5.0, 5.0, 30.0, 20.0});
    (*child).add_child(grandchild);
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    const ControlTable& table = window.control_table();
    window.perform_layout();
    static_cast<void>(window.take_damage());
    const Rect placed = (*grandchild).absolute_bounds();
    expect(placed == Rect{15.0, 25.0, 30.0, 20.0}, "the grandchild is placed inside the child");

    (*grandchild).invalidate(Dirty::paint);
    expect(table.pending((*grandchild).surface_slot(), DirtyKind::paint), "a paint write touches its bit");
    expect(has_dirty((*root).subtree_dirty(), Dirty::paint), "reading an ancestor's flags settles the touch");
    expect(!table.pending(DirtyKind::paint), "the settle drains the bit");
    const DamageRegion whole = window.take_damage();
    expect(whole.bounds() == placed, "the settled damage is the control's bounds");

    window.reset_activity_metrics();
    (*grandchild).invalidate(Dirty::paint);
    (*grandchild).invalidate(Dirty::paint);
    const MetricsSnapshot twice = window.metrics_snapshot();
    expect(twice.dirty_marks == 2U, "each write counts as a mark");
    expect(twice.requested_damage_area == placed.area(), "two touches of one control damage it once");
    static_cast<void>(window.take_damage());

    (*grandchild).invalidate(Rect{1.0, 2.0, 3.0, 4.0});
    expect(table.pending(DirtyKind::paint), "a localized paint write touches the bit");
    const DamageRegion local = window.take_damage();
    expect(local.bounds() == (Rect{16.0, 27.0, 3.0, 4.0}), "localized damage lands at the control's place");

    (*grandchild).invalidate(Dirty::paint);
    (*grandchild).invalidate(Rect{1.0, 1.0, 2.0, 2.0});
    Control::Ptr removed = (*root).remove_child((*child).runtime_id());
    expect(!(*grandchild).surface_slot().assigned(), "the detached grandchild's slot is released");
    const DamageRegion vacated = window.take_damage();
    expect(vacated.bounds().contains(placed), "the detached grandchild's place is damaged");
    (*root).add_child(std::move(removed));
}

// M0.3: a subtree mark waits for the settle too, and covers the touches
// inside it: a descendant's touch and a nested subtree mark of the same
// kinds add no damage area of their own. A container escalating a kind
// takes a descendant's touch of it as its own subtree mark.
class EscalatingPanel final : public Panel {
public:
    using Panel::Panel;
    [[nodiscard]] Dirty child_touch_escalation() const noexcept override { return Dirty::paint; }
};

double settled_area(Window& window) {
    const MetricsSnapshot metrics = window.metrics_snapshot();
    static_cast<void>(window.take_damage());
    window.reset_activity_metrics();
    return metrics.requested_damage_area;
}

void test_subtree_marks_settle() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("subtree.root"));
    std::shared_ptr<EscalatingPanel> child = make_control<EscalatingPanel>(StableId("subtree.child"));
    std::shared_ptr<Panel> left = make_control<Panel>(StableId("subtree.left"));
    std::shared_ptr<Panel> right = make_control<Panel>(StableId("subtree.right"));
    (*child).set_bounds(Rect{10.0, 20.0, 100.0, 50.0});
    (*left).set_bounds(Rect{5.0, 5.0, 30.0, 20.0});
    (*right).set_bounds(Rect{50.0, 5.0, 30.0, 20.0});
    (*child).add_child(left);
    (*child).add_child(right);
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    const ControlTable& table = window.control_table();
    window.perform_layout();
    static_cast<void>(settled_area(window));

    (*root).invalidate_subtree(Dirty::paint);
    expect(table.pending(DirtyKind::subtree), "a subtree mark touches the subtree bit");
    expect(has_dirty((*right).dirty(), Dirty::paint), "the settle marks every descendant");
    expect(!table.pending(DirtyKind::subtree), "and drains the subtree bit");
    const double whole = settled_area(window);
    expect(whole > 0.0, "the subtree mark requests its damage at the settle");

    (*right).invalidate(Dirty::paint);
    (*child).invalidate_subtree(Dirty::paint);
    (*root).invalidate_subtree(Dirty::paint);
    expect(settled_area(window) == whole, "touches and marks inside a pending subtree mark add nothing");

    window.perform_layout();
    static_cast<void>(window.take_damage());
    static_cast<void>(settled_area(window));
    (*left).invalidate(Dirty::paint);
    const double escalated = settled_area(window);
    expect(escalated > (*left).absolute_bounds().area(),
           "the escalating container takes its child's paint as its subtree mark");
}

} // namespace

int main() {
    test_bitset_take_order();
    test_table_slots();
    test_window_lifecycle();
    test_settle_defers_bookkeeping();
    test_subtree_marks_settle();
    std::cout << "control table tests passed\n";
    return EXIT_SUCCESS;
}
