#pragma once

#include "gui_forms/dirty.hpp"
#include "gui_forms/layout/resize_axes/resize_axes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gui_forms {

class Control;

namespace detail {

// A control's place in its surface root's control table (property-
// capabilities study, decisions 6 and 11): a dense index, assigned at
// attach and released at detach, and the generation the slot had when it
// was assigned. A released slot's generation advances, so a slot kept past
// its control's detach is rejected (ControlTable::control_at answers null).
struct ControlSlot final {
    static constexpr std::uint32_t unassigned = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index{unassigned};
    std::uint32_t generation{};

    [[nodiscard]] bool assigned() const noexcept { return index != unassigned; }

    friend bool operator==(const ControlSlot& left, const ControlSlot& right) noexcept {
        return left.index == right.index && left.generation == right.generation;
    }
};

// The kinds the table keeps a bitset for. The exact Dirty bits stay in
// Control::dirty_; a kind's bit says only that the control has some of
// them pending.
enum class DirtyKind : std::uint8_t {
    // Dirty::measure or Dirty::arrange.
    layout,
    // Dirty::paint.
    paint,
    // Every other kind: hit test, text, style, resource, semantics,
    // accessibility.
    other,
    count,
};

inline constexpr std::size_t dirty_kind_count = static_cast<std::size_t>(DirtyKind::count);

// A dense bitset over table indices with a summary level (study option A2):
// summary bit w is set exactly when word w has a bit set, so walking the set
// bits and clearing them cost the set words, not the capacity. Capacity
// grows with the table and never shrinks; every bit past the live length is
// clear.
class DirtyBitset final {
public:
    // Makes room for indices below `capacity`; existing bits keep.
    void reserve(std::size_t capacity);
    void set(std::uint32_t index) noexcept;
    void reset(std::uint32_t index) noexcept;
    [[nodiscard]] bool test(std::uint32_t index) const noexcept;
    [[nodiscard]] bool any() const noexcept;
    // Appends the set indices to `indices` in increasing order and clears
    // them. `indices` is the caller's storage, reused across flushes; it is
    // not emptied first. Returns how many were appended.
    std::size_t take(std::vector<std::uint32_t>& indices);
    // Clears every bit; the capacity stays.
    void clear() noexcept;

private:
    // Bit b of words_[w] is index 64 * w + b.
    std::vector<std::uint64_t> words_{};
    // Bit s of summary_[v] says words_[64 * v + s] is not zero.
    std::vector<std::uint64_t> summary_{};
};

// The control table of one surface root: one slot per attached control,
// the per-kind pending bitsets and each slot's resize registration. It
// observes its controls (the tree owns them): a slot is released before
// its control leaves the surface, so a live slot's pointer is valid. Used
// only on the surface root's thread.
class ControlTable final {
public:
    // Gives `control` a slot: a released one first (most recently released
    // first), else a new one at the end. The slot's bits are clear.
    [[nodiscard]] ControlSlot assign(Control& control);
    // Clears the slot's bits and registration, advances its generation and
    // keeps it for reuse. A slot that is not live changes nothing.
    void release(ControlSlot slot) noexcept;
    // The control holding `slot`, or null when the slot was released since.
    [[nodiscard]] Control* control_at(ControlSlot slot) const noexcept;
    // Sets, for a live slot, the bit of every kind `dirty` reaches.
    void touch(ControlSlot slot, Dirty dirty) noexcept;
    [[nodiscard]] bool pending(DirtyKind kind) const noexcept;
    [[nodiscard]] bool pending(ControlSlot slot, DirtyKind kind) const noexcept;
    // Appends the indices pending `kind` (see DirtyBitset::take) and clears
    // them.
    std::size_t take(DirtyKind kind, std::vector<std::uint32_t>& indices);
    // The control at a live index, as take() reports them; null for a
    // released one.
    [[nodiscard]] Control* control_at_index(std::uint32_t index) const noexcept;

    // The resize registration of a live slot (ResizeAxes::none for others).
    [[nodiscard]] ResizeAxes resize_response(ControlSlot slot) const noexcept;
    void set_resize_response(ControlSlot slot, ResizeAxes axes) noexcept;

    // Slots in use, and slots ever made (live plus released).
    [[nodiscard]] std::size_t live_count() const noexcept { return live_; }
    [[nodiscard]] std::size_t slot_count() const noexcept { return controls_.size(); }

private:
    [[nodiscard]] bool live(ControlSlot slot) const noexcept;

    // By index: the control (null while released), its current generation
    // and its resize registration. The three have slot_count() elements.
    std::vector<Control*> controls_{};
    std::vector<std::uint32_t> generations_{};
    std::vector<ResizeAxes> responses_{};
    // Released indices, reused last in, first out.
    std::vector<std::uint32_t> free_{};
    std::array<DirtyBitset, dirty_kind_count> pending_{};
    std::size_t live_{};
};

// The kinds `dirty` reaches.
[[nodiscard]] bool dirty_reaches(Dirty dirty, DirtyKind kind) noexcept;

} // namespace detail
} // namespace gui_forms
