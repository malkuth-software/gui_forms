#include "gui_forms/surface_root/control_table/control_table.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace gui_forms::detail {
namespace {

constexpr std::size_t word_bits = 64U;

// The words needed for `count` bits.
[[nodiscard]] std::size_t words_for(std::size_t count) noexcept {
    const std::size_t words = (count + word_bits - 1U) / word_bits;
    return words;
}

} // namespace

bool dirty_reaches(Dirty dirty, DirtyKind kind) noexcept {
    switch (kind) {
    case DirtyKind::layout: return has_dirty(dirty, Dirty::measure) || has_dirty(dirty, Dirty::arrange);
    case DirtyKind::paint: return has_dirty(dirty, Dirty::paint);
    case DirtyKind::other: {
        const Dirty rest = dirty & (Dirty::hit_test | Dirty::text | Dirty::style | Dirty::resource |
                                    Dirty::semantics | Dirty::accessibility);
        return rest != Dirty::none;
    }
    case DirtyKind::count: return false;
    }
    return false;
}

void DirtyBitset::reserve(std::size_t capacity) {
    const std::size_t words = words_for(capacity);
    if (words <= words_.size()) return;
    words_.resize(words, 0U);
    summary_.resize(words_for(words), 0U);
}

void DirtyBitset::set(std::uint32_t index) noexcept {
    const std::size_t word = index / word_bits;
    words_[word] |= std::uint64_t{1} << (index % word_bits);
    summary_[word / word_bits] |= std::uint64_t{1} << (word % word_bits);
}

void DirtyBitset::reset(std::uint32_t index) noexcept {
    const std::size_t word = index / word_bits;
    words_[word] &= ~(std::uint64_t{1} << (index % word_bits));
    if (words_[word] == 0U) summary_[word / word_bits] &= ~(std::uint64_t{1} << (word % word_bits));
}

bool DirtyBitset::test(std::uint32_t index) const noexcept {
    const std::size_t word = index / word_bits;
    if (word >= words_.size()) return false;
    const bool set_bit = (words_[word] >> (index % word_bits) & 1U) != 0U;
    return set_bit;
}

bool DirtyBitset::any() const noexcept {
    for (const std::uint64_t summary : summary_) {
        if (summary != 0U) return true;
    }
    return false;
}

std::size_t DirtyBitset::take(std::vector<std::uint32_t>& indices) {
    std::size_t appended = 0U;
    for (std::size_t group = 0U; group < summary_.size(); ++group) {
        std::uint64_t summary = summary_[group];
        while (summary != 0U) {
            const std::size_t word = group * word_bits + static_cast<std::size_t>(std::countr_zero(summary));
            summary &= summary - 1U;
            std::uint64_t bits = words_[word];
            while (bits != 0U) {
                const std::size_t bit = static_cast<std::size_t>(std::countr_zero(bits));
                bits &= bits - 1U;
                indices.push_back(static_cast<std::uint32_t>(word * word_bits + bit));
                ++appended;
            }
            words_[word] = 0U;
        }
        summary_[group] = 0U;
    }
    return appended;
}

void DirtyBitset::clear() noexcept {
    for (std::size_t group = 0U; group < summary_.size(); ++group) {
        std::uint64_t summary = summary_[group];
        while (summary != 0U) {
            const std::size_t word = group * word_bits + static_cast<std::size_t>(std::countr_zero(summary));
            summary &= summary - 1U;
            words_[word] = 0U;
        }
        summary_[group] = 0U;
    }
}

ControlSlot ControlTable::assign(Control& control) {
    if (!free_.empty()) {
        const std::uint32_t index = free_.back();
        free_.pop_back();
        controls_[index] = &control;
        ++live_;
        const ControlSlot reused{index, generations_[index]};
        return reused;
    }
    // The last index stays unassigned's sentinel.
    if (controls_.size() >= static_cast<std::size_t>(ControlSlot::unassigned)) {
        throw std::length_error("GUI.Forms control table is full");
    }
    const std::uint32_t index = static_cast<std::uint32_t>(controls_.size());
    const std::size_t count = controls_.size() + 1U;
    // Reserve every bitset before the table grows, so a failed allocation
    // leaves the table as it was.
    for (DirtyBitset& bits : pending_) bits.reserve(count);
    controls_.reserve(count);
    generations_.reserve(count);
    responses_.reserve(count);
    touches_.reserve(count);
    free_.reserve(count);
    controls_.push_back(&control);
    generations_.push_back(0U);
    responses_.push_back(ResizeAxes::none);
    touches_.push_back(PendingTouch{});
    ++live_;
    const ControlSlot made{index, 0U};
    return made;
}

bool ControlTable::live(ControlSlot slot) const noexcept {
    if (!slot.assigned() || slot.index >= controls_.size()) return false;
    const bool current = controls_[slot.index] != nullptr && generations_[slot.index] == slot.generation;
    return current;
}

void ControlTable::release(ControlSlot slot) noexcept {
    if (!live(slot)) return;
    for (DirtyBitset& bits : pending_) bits.reset(slot.index);
    controls_[slot.index] = nullptr;
    responses_[slot.index] = ResizeAxes::none;
    touches_[slot.index] = PendingTouch{};
    // A generation that wraps reuses an old one only after 2^32 reuses of
    // this one index.
    ++generations_[slot.index];
    --live_;
    // assign() reserved room for every index, so this does not allocate.
    free_.push_back(slot.index);
}

Control* ControlTable::control_at(ControlSlot slot) const noexcept {
    if (!live(slot)) return nullptr;
    return controls_[slot.index];
}

Control* ControlTable::control_at_index(std::uint32_t index) const noexcept {
    if (index >= controls_.size()) return nullptr;
    return controls_[index];
}

void ControlTable::touch(ControlSlot slot, Dirty dirty, PendingTouch owed) noexcept {
    if (!live(slot)) return;
    for (std::size_t kind = 0U; kind < dirty_kind_count; ++kind) {
        if (dirty_reaches(dirty, static_cast<DirtyKind>(kind))) pending_[kind].set(slot.index);
    }
    PendingTouch& pending_touch = touches_[slot.index];
    pending_touch.requested |= owed.requested;
    pending_touch.marks_layout_ancestors = pending_touch.marks_layout_ancestors || owed.marks_layout_ancestors;
    pending_touch.damages_bounds = pending_touch.damages_bounds || owed.damages_bounds;
}

PendingTouch ControlTable::take_touch(std::uint32_t index) noexcept {
    if (index >= controls_.size() || controls_[index] == nullptr) return PendingTouch{};
    const PendingTouch taken = touches_[index];
    touches_[index] = PendingTouch{};
    return taken;
}

bool ControlTable::any_pending() const noexcept {
    for (const DirtyBitset& bits : pending_) {
        if (bits.any()) return true;
    }
    return false;
}

bool ControlTable::pending(DirtyKind kind) const noexcept {
    const bool any = pending_[static_cast<std::size_t>(kind)].any();
    return any;
}

bool ControlTable::pending(ControlSlot slot, DirtyKind kind) const noexcept {
    if (!live(slot)) return false;
    const bool set_bit = pending_[static_cast<std::size_t>(kind)].test(slot.index);
    return set_bit;
}

std::size_t ControlTable::take(DirtyKind kind, std::vector<std::uint32_t>& indices) {
    const std::size_t appended = pending_[static_cast<std::size_t>(kind)].take(indices);
    return appended;
}

ResizeAxes ControlTable::resize_response(ControlSlot slot) const noexcept {
    if (!live(slot)) return ResizeAxes::none;
    return responses_[slot.index];
}

void ControlTable::set_resize_response(ControlSlot slot, ResizeAxes axes) noexcept {
    if (!live(slot)) return;
    responses_[slot.index] = axes;
}

} // namespace gui_forms::detail
