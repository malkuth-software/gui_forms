#include "gui_forms/control/content_region/content_region.hpp"

#include "gui_forms/control/control/control.hpp"

#include <algorithm>

namespace gui_forms {

ContentRegion::ContentRegion(Control& owner, std::size_t capacity) noexcept
    : owner_(owner), capacity_(std::max<std::size_t>(capacity, 1U)) {}

void ContentRegion::note_item(ContentItemKey item, Dirty kinds) {
    const Dirty immediate = without_dirty(kinds, Dirty::paint);
    if (immediate != Dirty::none) owner_.invalidate(immediate);
    if (!has_dirty(kinds, Dirty::paint) || overflowed_) return;
    if (!pending_.empty() && pending_.back() == item) return;
    if (pending_.size() == capacity_) {
        // Too many changes: a paint of the whole owner is cheaper.
        pending_.clear();
        overflowed_ = true;
        owner_.invalidate(Dirty::paint);
        return;
    }
    if (pending_.capacity() < capacity_) pending_.reserve(capacity_);
    // The first entry since the last gather asks the owner's window for one;
    // an owner with no window to gather it paints whole.
    if (pending_.empty() && !owner_.request_content_gather()) {
        owner_.invalidate(Dirty::paint);
        return;
    }
    pending_.push_back(item);
}

std::span<const ContentItemKey> ContentRegion::pending() const noexcept {
    const std::span<const ContentItemKey> items(pending_.data(), pending_.size());
    return items;
}

void ContentRegion::clear_pending() noexcept {
    pending_.clear();
    overflowed_ = false;
}

Dirty ContentRegion::fold_pending() noexcept {
    const Dirty folded = pending_.empty() && !overflowed_ ? Dirty::none : Dirty::paint;
    clear_pending();
    return folded;
}

} // namespace gui_forms
