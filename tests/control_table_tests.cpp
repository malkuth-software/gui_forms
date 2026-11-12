// Property-capabilities migration M0: the surface root's control table.
// Every attached control holds a live slot; a detached control's slot is
// released, its generation advanced so that the old slot is rejected, and
// reused by the next attachment; writes touch their control's bit by kind
// and a read of settled state (a layout pass, a paint) drains the bits.
// M0.2: a write only touches; the settle before a read does the ancestor
// marks and places the damage at the bounds as they are then.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/control_layout_styles/control_layout_styles.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/layout/resize_axes/resize_axes.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/surface_root/control_table/control_table.hpp"
#include "gui_forms/control/content_region/content_region.hpp"

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

// A panel painting a grid of 10 by 10 items itself, through one content
// region: item (major, minor) is the square at (10 * major, 10 * minor).
// It counts the items its gathers saw.
class RegionPanel final : public Panel {
public:
    explicit RegionPanel(StableId id, std::size_t capacity) : Panel(std::move(id)), region(*this, capacity) {}

    void gather_content_regions() override {
        for (const ContentItemKey& item : region.pending()) {
            invalidate(Rect{10.0 * static_cast<double>(item.major), 10.0 * static_cast<double>(item.minor), 10.0, 10.0});
            ++gathered;
        }
        region.clear_pending();
    }

    [[nodiscard]] Dirty fold_content_regions() noexcept override {
        const Dirty folded = region.fold_pending();
        return folded;
    }

    ContentRegion region;
    std::size_t gathered{};
};

// Content regions (study decision 10): noted items are gathered after the
// layout as damage at their squares, with no layout of their own; a
// redraw-all folds them (nothing gathered, the whole control damaged); past
// its capacity a region paints its whole owner; a detached owner paints
// whole.
void test_content_regions() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("region.root"));
    std::shared_ptr<RegionPanel> owner = std::make_shared<RegionPanel>(StableId("region.owner"), 3U);
    (*owner).set_bounds(Rect{0.0, 0.0, 100.0, 100.0});
    (*root).add_child(owner);
    Window window(root, Size{200.0, 200.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    static_cast<void>(settled_area(window));

    (*owner).region.note_item(ContentItemKey{1U, 2U}, Dirty::paint);
    (*owner).region.note_item(ContentItemKey{1U, 2U}, Dirty::paint);
    (*owner).region.note_item(ContentItemKey{5U, 5U}, Dirty::paint);
    expect((*owner).region.pending().size() == 2U, "an item noted twice in a row is kept once");
    window.perform_layout();
    const MetricsSnapshot metrics = window.metrics_snapshot();
    expect((*owner).gathered == 2U && (*owner).region.pending().empty(), "the layout's end gathers the region");
    expect(metrics.controls_arranged == 0U, "a noted item lays nothing out");
    const DamageRegion damage = window.take_damage();
    expect(damage.bounds() == Rect{10.0, 20.0, 50.0, 40.0}, "the damage is the noted items' squares");
    window.reset_activity_metrics();

    (*owner).gathered = 0U;
    (*owner).region.note_item(ContentItemKey{2U, 2U}, Dirty::paint);
    (*owner).redraw_all(Dirty::paint);
    window.perform_layout();
    expect((*owner).gathered == 0U, "a redraw-all folds the pending items: none is gathered");
    expect(window.take_damage().bounds() == Rect{0.0, 0.0, 100.0, 100.0}, "and the whole control repaints");

    for (std::uint64_t item = 0U; item < 4U; ++item) {
        (*owner).region.note_item(ContentItemKey{item, 0U}, Dirty::paint);
    }
    window.perform_layout();
    expect((*owner).gathered == 0U && window.take_damage().bounds() == Rect{0.0, 0.0, 100.0, 100.0},
           "past its capacity a region paints its whole owner");

    (*owner).region.note_item(ContentItemKey{3U, 3U}, Dirty::paint | Dirty::semantics);
    expect(has_dirty((*owner).dirty(), Dirty::semantics), "other kinds reach the owner at the note");
    const Control::Ptr removed = (*root).remove_child((*owner).runtime_id());
    expect((*owner).region.pending().empty(), "a detach folds the owner's regions");
    (*owner).region.note_item(ContentItemKey{4U, 4U}, Dirty::paint);
    expect((*owner).region.pending().empty() && has_dirty((*owner).dirty(), Dirty::paint),
           "a detached owner paints whole instead of recording");
}

// Reduced M0.4: a control's resize registration is asked again when its
// arranged size changes and kept in its slot; resize_relayouts counts the
// size changes along an axis the children follow. The root holds a strip
// docked top (a mark anchored to its right edge) and a filler docked fill
// (a mark at its top left). A wider window moves the strip's mark and the
// root's children; a taller one leaves the strip alone and moves nothing in
// the filler.
void test_resize_registrations() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("resize.root"));
    std::shared_ptr<Panel> strip = make_control<Panel>(StableId("resize.strip"));
    std::shared_ptr<Panel> strip_mark = make_control<Panel>(StableId("resize.strip.mark"));
    std::shared_ptr<Panel> filler = make_control<Panel>(StableId("resize.filler"));
    std::shared_ptr<Panel> filler_mark = make_control<Panel>(StableId("resize.filler.mark"));
    (*strip).set_requested_bounds(Rect{0.0, 0.0, 200.0, 30.0});
    (*strip).dock = DockStyle::top;
    (*strip_mark).set_requested_bounds(Rect{170.0, 5.0, 20.0, 20.0});
    (*strip_mark).anchor = AnchorStyles::top | AnchorStyles::right;
    (*strip).add_child(strip_mark);
    (*filler).dock = DockStyle::fill;
    (*filler_mark).set_requested_bounds(Rect{5.0, 5.0, 20.0, 20.0});
    (*filler).add_child(filler_mark);
    (*root).add_child(strip);
    (*root).add_child(filler);
    Window window(root, Size{200.0, 100.0});
    const ControlTable& table = window.control_table();
    window.perform_layout();
    expect(window.metrics_snapshot().resize_relayouts == 0U, "the first arrange is not a resize");

    window.resize(Size{240.0, 100.0});
    window.perform_layout();
    expect(table.resize_response((*root).surface_slot()) == ResizeAxes::both,
           "a root with a filling child registers both axes");
    expect(table.resize_response((*strip).surface_slot()) == ResizeAxes::horizontal,
           "a strip holding a right-anchored mark registers the horizontal axis");
    expect(table.resize_response((*filler).surface_slot()) == ResizeAxes::none,
           "a filler holding a top-left mark registers no axis");
    expect(window.metrics_snapshot().resize_relayouts == 2U,
           "a wider window relays out the root and the strip, not the filler");
    expect((*strip_mark).arranged_bounds().x == 210.0, "the right-anchored mark follows the wider strip");

    window.resize(Size{240.0, 140.0});
    window.perform_layout();
    expect(window.metrics_snapshot().resize_relayouts == 3U,
           "a taller window relays out the root only: the strip keeps its size, the filler's mark stays");
    expect(table.resize_response((*strip).surface_slot()) == ResizeAxes::horizontal,
           "a control whose size did not change keeps its registration");
}

} // namespace

int main() {
    test_bitset_take_order();
    test_table_slots();
    test_window_lifecycle();
    test_settle_defers_bookkeeping();
    test_subtree_marks_settle();
    test_content_regions();
    test_resize_registrations();
    std::cout << "control table tests passed\n";
    return EXIT_SUCCESS;
}
