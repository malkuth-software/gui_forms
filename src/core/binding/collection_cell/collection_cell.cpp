#include "gui_forms/binding/collection_cell/collection_cell.hpp"

#include <algorithm>
#include <limits>

namespace gui_forms {
namespace {

// The bitset indexes positions as 32-bit values.
constexpr std::size_t bit_reach = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;

// Whether a taken position is at or past the watermark.
struct AtOrPastWatermark final {
    std::size_t watermark{};

    [[nodiscard]] bool operator()(std::uint32_t position) const noexcept {
        return static_cast<std::size_t>(position) >= watermark;
    }
};

} // namespace

CollectionEdit CollectionEdit::assigned(std::size_t count) noexcept {
    const CollectionEdit edit(CollectionEditKind::assign, 0U, count);
    return edit;
}

CollectionEdit CollectionEdit::inserted(std::size_t index, std::size_t count) noexcept {
    const CollectionEdit edit(CollectionEditKind::insert, index, count);
    return edit;
}

CollectionEdit CollectionEdit::erased(std::size_t index, std::size_t count) noexcept {
    const CollectionEdit edit(CollectionEditKind::erase, index, count);
    return edit;
}

CollectionEdit CollectionEdit::replaced(std::size_t index) noexcept {
    const CollectionEdit edit(CollectionEditKind::replace, index, 1U);
    return edit;
}

stxlib::Optional<std::size_t> CollectionEdit::remap(std::size_t before) const noexcept {
    switch (kind_) {
    case CollectionEditKind::assign:
        if (before < count_) return stxlib::Optional<std::size_t>::of(before);
        return stxlib::Optional<std::size_t>::none();
    case CollectionEditKind::insert:
        if (before < index_) return stxlib::Optional<std::size_t>::of(before);
        return stxlib::Optional<std::size_t>::of(before + count_);
    case CollectionEditKind::erase:
        if (before < index_) return stxlib::Optional<std::size_t>::of(before);
        if (before - index_ < count_) return stxlib::Optional<std::size_t>::none();
        return stxlib::Optional<std::size_t>::of(before - count_);
    case CollectionEditKind::replace:
        return stxlib::Optional<std::size_t>::of(before);
    default:
        break;
    }
    // Unreachable for an edit made by the factories; the fallback drops
    // the position.
    return stxlib::Optional<std::size_t>::none();
}

void CollectionDirty::reserve(std::size_t length) {
    const std::size_t capacity = std::min(length, bit_reach);
    replaced_.reserve(capacity);
}

void CollectionDirty::record(const CollectionEdit& edit) noexcept {
    if (edit.kind() == CollectionEditKind::replace) {
        const std::size_t position = edit.index();
        if (structural_from_.present() && position >= *structural_from_.value()) return;
        if (position >= bit_reach) {
            structural_from_.set(position);
            return;
        }
        replaced_.set(static_cast<std::uint32_t>(position));
        return;
    }
    // An assignment moves everything; an insertion or erasure everything
    // from its index.
    const std::size_t from = edit.kind() == CollectionEditKind::assign ? 0U : edit.index();
    if (structural_from_.absent() || from < *structural_from_.value()) structural_from_.set(from);
    if (from == 0U) replaced_.clear();
}

bool CollectionDirty::any() const noexcept {
    const bool pending = structural_from_.present() || replaced_.any();
    return pending;
}

stxlib::Optional<std::size_t> CollectionDirty::take(std::vector<std::uint32_t>& replaced) {
    const std::size_t first_appended = replaced.size();
    try {
        static_cast<void>(replaced_.take(replaced));
    } catch (...) {
        // Some bits were taken and lost with the failed append: the next
        // take repaints everything.
        replaced_.clear();
        structural_from_.set(0U);
        throw;
    }
    const stxlib::Optional<std::size_t> structural_from = structural_from_;
    structural_from_.reset();
    if (structural_from.present()) {
        const std::vector<std::uint32_t>::iterator appended =
            replaced.begin() + static_cast<std::vector<std::uint32_t>::difference_type>(first_appended);
        const std::vector<std::uint32_t>::iterator past =
            std::find_if(appended, replaced.end(), AtOrPastWatermark{*structural_from.value()});
        replaced.erase(past, replaced.end());
    }
    return structural_from;
}

void CollectionDirty::clear() noexcept {
    replaced_.clear();
    structural_from_.reset();
}

} // namespace gui_forms
