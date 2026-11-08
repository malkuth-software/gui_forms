#pragma once

#include "gui_forms/surface_root/control_table/control_table.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms {

// What one edit of a collection did (in-place collections design, owner
// 2026-11-07): an assignment of the whole sequence, an insertion or erasure
// of `count` elements at `index`, or the replacement of the element at
// `index`.
enum class CollectionEditKind : std::uint8_t {
    assign,
    insert,
    erase,
    replace,
};

// One edit's description, made only by the named factories and read only
// through the queries, so an edit is always one of the four shapes. The
// collection's hooks receive it: prepare_edit before anything changes,
// written after the elements are stored.
class CollectionEdit final {
public:
    // The whole sequence replaced by `count` elements (also clear(), with 0).
    [[nodiscard]] static CollectionEdit assigned(std::size_t count) noexcept;
    // `count` elements inserted before the element at `index`.
    [[nodiscard]] static CollectionEdit inserted(std::size_t index, std::size_t count) noexcept;
    // `count` elements erased from `index`.
    [[nodiscard]] static CollectionEdit erased(std::size_t index, std::size_t count) noexcept;
    // The element at `index` replaced by another value.
    [[nodiscard]] static CollectionEdit replaced(std::size_t index) noexcept;

    [[nodiscard]] CollectionEditKind kind() const noexcept { return kind_; }
    // The first position the edit touched: 0 for an assignment.
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    // The elements assigned, inserted or erased; 1 for a replacement.
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    // Whether positions moved or the length changed: every kind but replace.
    [[nodiscard]] bool structural() const noexcept { return kind_ != CollectionEditKind::replace; }
    // Where an element at position `before` is after the edit, or nullopt
    // when the edit removed it. An assignment keeps the positions below its
    // new length (the elements there are new; the position is what a
    // selection without stable identities keeps); a replacement keeps every
    // position.
    [[nodiscard]] std::optional<std::size_t> remap(std::size_t before) const noexcept;

private:
    CollectionEdit(CollectionEditKind kind, std::size_t index, std::size_t count) noexcept
        : kind_(kind), index_(index), count_(count) {}

    CollectionEditKind kind_;
    std::size_t index_;
    std::size_t count_;
};

// A collection's pending damage between two settles (design: settled at
// flush, like the control table's DirtyBitset): the replaced positions, as
// bits, and a structural watermark, the lowest position an insertion,
// erasure or assignment touched. Every position at or past the watermark
// is dirty (its element moved or is new), so a bit there says nothing more
// and is dropped at the take. Edits between two takes coalesce: the lowest
// watermark wins and the bits unite. Used on its control's thread.
class CollectionDirty final {
public:
    // Makes room for replacement bits below `length`; existing bits keep.
    // The only allocation; called before the edit is stored.
    void reserve(std::size_t length);
    // Records an edit already stored. A replacement needs room reserved
    // for its position; a position past the bitset's reach (2^32) lowers
    // the watermark instead.
    void record(const CollectionEdit& edit) noexcept;
    [[nodiscard]] bool any() const noexcept;
    // Appends the replaced positions below the watermark to `replaced`, in
    // increasing order (the caller's storage, reused; not emptied first),
    // clears the record and returns the watermark, or nullopt when no
    // structural edit is pending. If appending fails, the record becomes
    // "everything dirty" (watermark 0) for the next take, and the failure
    // propagates.
    [[nodiscard]] std::optional<std::size_t> take(std::vector<std::uint32_t>& replaced);
    void clear() noexcept;

private:
    detail::DirtyBitset replaced_{};
    std::optional<std::size_t> structural_from_{};
};

// A collection property's only storage, as the single member of its
// capability: PropertyCell's shape for a sequence edited in place (design
// accepted 2026-11-07). The read is `items.read()` (a span) or `items[i]`;
// the writes are `items = elements`, `items.insert(i, x)`,
// `items.append(x)` or `items.append(elements)` (an insertion at the end),
// `items.erase(i, n)`, `items.replace(i, x)` and `items.clear()`.
//
// Each write checks its positions (std::out_of_range), skips an edit that
// changes nothing (an equal assignment or replacement, an empty erasure),
// reserves what the store needs (capacity only: nothing observable
// changes), then calls `Capability::prepare_edit(capability, edit,
// incoming)`, where the declaring control checks and may coerce the
// incoming elements and readies its own per-element state; a throw there
// refuses the edit and the elements have not changed. Nothing fails after
// prepare_edit returns: the cell stores (Element moves without throwing),
// records the edit in its dirty record, and calls
// `Capability::written(capability, edit)`, so state the hook readied is
// always followed by its written.
// An edit of the same collection made while its prepare_edit runs (by a
// handler the hook raised) refuses the outer edit with std::logic_error.
// Writes to an element's fields and reorders the declaring control makes
// itself go through the capability's protected owner operations, which
// record their damage and raise no hooks. The
// declaring control takes the dirty record at its flush through the
// capability (`take_dirty`) and intersects it with what it shows.
//
// Element: copyable or movable, comparable with ==, nothrow movable.
// Capability: the standard-layout class whose only member this cell is.
template <typename Element, typename Capability>
class CollectionCell final {
    static_assert(std::is_nothrow_move_constructible_v<Element> && std::is_nothrow_move_assignable_v<Element>,
                  "a collection stores without failing after it reserves, so its elements move without throwing");

public:
    // Starts empty. There is no constructor from elements, so a braced
    // list on the right of `items = {...}` can only be the elements.
    CollectionCell() : elements_(), dirty_(), revision_(0U) {}
    CollectionCell(const CollectionCell&) = delete;
    CollectionCell& operator=(const CollectionCell&) = delete;

    // The assignment of the whole sequence.
    CollectionCell& operator=(std::vector<Element> elements) {
        if (elements == elements_) return *this;
        const CollectionEdit edit = CollectionEdit::assigned(elements.size());
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>(elements));
        require_unchanged_since(revision);
        elements_ = std::move(elements);
        record(edit);
        Capability::written(capability(), edit);
        return *this;
    }

    // The read.
    [[nodiscard]] std::span<const Element> read() const noexcept { return elements_; }

    // The element at `index`; std::out_of_range past the end.
    [[nodiscard]] const Element& operator[](std::size_t index) const {
        if (index >= elements_.size()) throw std::out_of_range("collection index is outside the collection");
        const Element& element = elements_[index];
        return element;
    }

    // Inserts `element` before the element at `index` (at the end when
    // `index` is the length).
    void insert(std::size_t index, Element element) {
        if (index > elements_.size()) throw std::out_of_range("collection insertion is outside the collection");
        if (elements_.size() == elements_.max_size()) throw std::length_error("collection is full");
        const CollectionEdit edit = CollectionEdit::inserted(index, 1U);
        const std::size_t length = elements_.size() + 1U;
        elements_.reserve(length);
        dirty_.reserve(length);
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>(&element, 1U));
        require_unchanged_since(revision);
        const typename std::vector<Element>::difference_type offset =
            static_cast<typename std::vector<Element>::difference_type>(index);
        elements_.insert(elements_.begin() + offset, std::move(element));
        record(edit);
        Capability::written(capability(), edit);
    }

    // Appends `element`: the insertion at the end (Windows Forms' Items.Add).
    void append(Element element) {
        insert(elements_.size(), std::move(element));
    }

    // Appends every element of `elements`, in order, as one insertion at the
    // end; an empty sequence changes nothing.
    void append(std::vector<Element> elements) {
        if (elements.empty()) return;
        if (elements.size() > elements_.max_size() - elements_.size()) {
            throw std::length_error("collection is full");
        }
        const std::size_t index = elements_.size();
        const CollectionEdit edit = CollectionEdit::inserted(index, elements.size());
        const std::size_t length = index + elements.size();
        elements_.reserve(length);
        dirty_.reserve(length);
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>(elements));
        require_unchanged_since(revision);
        for (Element& element : elements) elements_.push_back(std::move(element));
        record(edit);
        Capability::written(capability(), edit);
    }

    // Erases `count` elements from `index`; an empty erasure changes nothing.
    void erase(std::size_t index, std::size_t count) {
        if (index > elements_.size() || count > elements_.size() - index) {
            throw std::out_of_range("collection erasure is outside the collection");
        }
        if (count == 0U) return;
        const CollectionEdit edit = CollectionEdit::erased(index, count);
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>());
        require_unchanged_since(revision);
        const typename std::vector<Element>::difference_type first =
            static_cast<typename std::vector<Element>::difference_type>(index);
        const typename std::vector<Element>::difference_type last =
            static_cast<typename std::vector<Element>::difference_type>(index + count);
        elements_.erase(elements_.begin() + first, elements_.begin() + last);
        record(edit);
        Capability::written(capability(), edit);
    }

    // Replaces the element at `index`; an equal value changes nothing.
    void replace(std::size_t index, Element element) {
        if (index >= elements_.size()) throw std::out_of_range("collection replacement is outside the collection");
        if (elements_[index] == element) return;
        const CollectionEdit edit = CollectionEdit::replaced(index);
        dirty_.reserve(elements_.size());
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>(&element, 1U));
        require_unchanged_since(revision);
        elements_[index] = std::move(element);
        record(edit);
        Capability::written(capability(), edit);
    }

    // Erases every element: an assignment of nothing.
    void clear() {
        if (elements_.empty()) return;
        const CollectionEdit edit = CollectionEdit::assigned(0U);
        const std::uint64_t revision = revision_;
        Capability::prepare_edit(capability(), edit, std::span<Element>());
        require_unchanged_since(revision);
        elements_.clear();
        record(edit);
        Capability::written(capability(), edit);
    }

private:
    friend Capability;

    // Notes a stored edit: the dirty record and the revision.
    void record(const CollectionEdit& edit) noexcept {
        dirty_.record(edit);
        ++revision_;
    }

    // A prepare_edit hook that edited this collection (through a handler it
    // raised) has invalidated the checked positions and the reservation:
    // the outer edit is refused. The hook's own readied state is the
    // hook's to keep consistent; it can compare revision() itself.
    void require_unchanged_since(std::uint64_t revision) const {
        if (revision != revision_) throw std::logic_error("collection edited during its own prepare_edit");
    }

    // The owner's operations, for the capability's protected forwarding
    // only: per-element field writes and reorders that are not edits of
    // the sequence, so they raise no hooks. Each records its damage.
    //
    // How many edits and owner stores this collection has seen.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // The elements, writable in place. Call mark_replaced (or mark_all)
    // before writing, so a failed mark leaves the elements unchanged.
    [[nodiscard]] std::span<Element> owner_elements() noexcept { return elements_; }
    // Records a write about to happen to the element at `index` (index
    // below the length). Reserves the dirty record: may throw, before the
    // write.
    void mark_replaced(std::size_t index) {
        dirty_.reserve(elements_.size());
        dirty_.record(CollectionEdit::replaced(index));
        ++revision_;
    }
    // Records a write to every element (a field added to each, say).
    void mark_all() noexcept {
        dirty_.record(CollectionEdit::assigned(elements_.size()));
        ++revision_;
    }
    // Stores a reordering or other whole replacement the owner made,
    // without hooks.
    void owner_store(std::vector<Element> elements) noexcept {
        elements_ = std::move(elements);
        dirty_.record(CollectionEdit::assigned(elements_.size()));
        ++revision_;
    }

    // The pending damage, for the capability's take_dirty only.
    [[nodiscard]] std::optional<std::size_t> take_dirty(std::vector<std::uint32_t>& replaced) {
        const std::optional<std::size_t> structural_from = dirty_.take(replaced);
        return structural_from;
    }

    Capability& capability() noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a collection capability is standard layout, with its cell as its only member");
        // Defined: a standard-layout class and its first non-static member
        // are pointer-interconvertible.
        Capability& owner = *reinterpret_cast<Capability*>(this);
        return owner;
    }

    std::vector<Element> elements_;
    CollectionDirty dirty_;
    // Advanced by every stored edit and owner operation.
    std::uint64_t revision_;
};

} // namespace gui_forms
