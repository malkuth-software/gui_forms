#pragma once

#include "gui_forms/dirty.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

class Control;

// One item of a content region, such as a DataGridView cell: two numbers
// the owning control gives meaning to (for the grid, a row position and a
// column index).
struct ContentItemKey final {
    std::uint64_t major{};
    std::uint64_t minor{};

    friend bool operator==(const ContentItemKey& left, const ContentItemKey& right) noexcept {
        const bool equal = left.major == right.major && left.minor == right.minor;
        return equal;
    }
};

// A control's content region (property-capabilities study, decision 10):
// the control owns its chrome (frame, headers, scroll bars), the region its
// items' lazy pending set. note_item records an item whose paint changed
// and, on the first entry since the last gather, asks the owner's window to
// gather the region after its next layout. The owner's
// Control::gather_content_regions then turns each pending item into damage
// at the item's final geometry and calls clear_pending. A redraw of the
// whole owner (Control::redraw_all) folds the region instead: fold_pending
// drops the entries, which would damage rectangles about to move anyway.
//
// The owning control holds the region as a member; the region observes its
// owner and is used only on the owner's UI thread.
class ContentRegion final {
public:
    // Up to `capacity` items are kept pending (at least one); the storage
    // is reserved at the first note, the region's only allocation. Past the
    // capacity the region asks for a paint of its whole owner instead.
    ContentRegion(Control& owner, std::size_t capacity) noexcept;
    ContentRegion(const ContentRegion&) = delete;
    ContentRegion& operator=(const ContentRegion&) = delete;

    // Records that `item` changed with `kinds`. Paint is gathered lazily;
    // any other kinds (semantics, hit test, layout) go to the owner's
    // Control::invalidate at once. A detached or initializing owner, or a
    // region past its capacity, gets Control::invalidate(Dirty::paint)
    // instead of an entry. May throw (the first note's reservation, the
    // window's pending list) before anything is recorded.
    void note_item(ContentItemKey item, Dirty kinds);
    // The items noted since the last gather or fold, in note order. An item
    // noted twice in a row is kept once; one noted twice apart is kept
    // twice, and its rectangle is damaged twice (the same union).
    [[nodiscard]] std::span<const ContentItemKey> pending() const noexcept;
    // The gather's end: drops the entries; the storage stays.
    void clear_pending() noexcept;
    // The redraw-all's fold: returns Dirty::paint when anything was
    // pending (or the region had overflowed) and drops the entries,
    // drawing nothing.
    [[nodiscard]] Dirty fold_pending() noexcept;

private:
    Control& owner_;
    std::vector<ContentItemKey> pending_{};
    std::size_t capacity_{1U};
    // Past the capacity since the last gather: the owner's whole paint is
    // already asked, and further notes record nothing.
    bool overflowed_{};
};

} // namespace gui_forms
