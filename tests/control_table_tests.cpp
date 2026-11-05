// Property-capabilities migration M0: the surface root's control table.
// Every attached control holds a live slot; a detached control's slot is
// released, its generation advanced so that the old slot is rejected, and
// reused by the next attachment; writes touch their control's bit by kind
// and a read of settled state (a layout pass, a paint) drains the bits.
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
    table.touch(b, Dirty::paint | Dirty::semantics);
    expect(table.pending(b, DirtyKind::paint) && table.pending(b, DirtyKind::other) &&
               !table.pending(b, DirtyKind::layout),
           "a touch sets the kinds it reaches");
    table.set_resize_response(b, ResizeAxes::horizontal);
    table.release(b);
    expect(table.control_at(b) == nullptr, "a released slot reaches no control");
    expect(!table.pending(DirtyKind::paint), "release clears the slot's bits");
    table.touch(b, Dirty::paint);
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

} // namespace

int main() {
    test_bitset_take_order();
    test_table_slots();
    test_window_lifecycle();
    std::cout << "control table tests passed\n";
    return EXIT_SUCCESS;
}
