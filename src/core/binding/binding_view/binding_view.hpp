#pragma once

// The view index of a binding source whose provider cannot sort or filter
// itself (audit N31; the design note in the cp28 queue entry for
// claude/cp28-binding, accepted by the owner 2026-11-10).
//
// Each source row has a slot: the index's own identity for the row, kept
// while rows around it are inserted, removed or moved. A slot owns a copy of
// the row's sort keys (numbers in 8 bytes, text in one owned arena) and a
// filter membership bit; nothing else of the row is held. Two order trees
// hold the slots: source_order_ in provider order, view_order_ (members
// only) in key order, equal keys in source order. A view position and a
// source row convert through both trees in O(log n).
//
//   build             n * (sort and filter columns) reads, O(n log n)
//   insert_rows       c reads per column, O(c log^2 n) (the tie-break ranks)
//   remove_rows       O(c log n)
//   move_rows         O(c log^2 n)
//   change_row        a key or filter column: O(log^2 n); another: O(log n)
//
// Memory: per row a slot (24 bytes per sort key, its text bytes, a member
// byte) and two tree entries, about 40 bytes plus keys. A cell without a
// valid answer sorts after every value, in both directions, and is filtered
// as null.
//
// The view is owned by its source and used on its UI thread; the reader it
// is given reads through the source's validated provider calls.

#include "binding_order_tree.hpp"

#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include "../binding_schema/binding_key_order.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::detail {

// Reads one cell for the view: false when the provider gave no valid answer.
class BindingKeyReader {
public:
    virtual bool read_key(std::size_t row, std::size_t column, BindingValue& value) = 0;

protected:
    BindingKeyReader() = default;
    ~BindingKeyReader() = default;
    BindingKeyReader(const BindingKeyReader&) = default;
    BindingKeyReader& operator=(const BindingKeyReader&) = default;
};

// One row moving from one position to another; a block move is a run of
// these, each relative to the order the ones before it left.
struct BindingRowMove final {
    std::size_t from{};
    std::size_t to{};
};

// Appends the single-row steps that move rows [row, row + count) so the
// first stands at target afterwards (target <= rows - count): rows moving
// up go first to last, rows moving down each go from row to the block's
// last place. Nothing when target == row.
void append_block_move_steps(std::size_t row, std::size_t count, std::size_t target,
                             std::vector<BindingRowMove>& steps);

// A sort key resolved to its column.
struct BindingViewKey final {
    std::size_t column{};
    bool descending{};
};

// A filter predicate resolved to its column.
struct BindingViewPredicate final {
    std::size_t column{};
    BindingPredicate predicate;
};

class BindingView final {
public:
    // Where a changed row was and is in the view (empty: not shown).
    struct RowChange final {
        std::optional<std::size_t> before;
        std::optional<std::size_t> after;
    };

    BindingView(std::vector<BindingViewKey> keys, std::vector<BindingViewPredicate> filter);

    // Indexes rows [0, row_count) afresh (row_count below binding_maximum_rows).
    void build(std::size_t row_count, BindingKeyReader& reader);

    // Rows shown, and source rows indexed.
    [[nodiscard]] std::size_t count() const noexcept { return view_order_.size(); }
    [[nodiscard]] std::size_t row_count() const noexcept { return source_order_.size(); }
    // The source row at view position (below count()).
    [[nodiscard]] std::size_t row_of(std::size_t position) const;
    // The view position of a source row (below row_count()), or empty when
    // the filter hides it.
    [[nodiscard]] std::optional<std::size_t> position_of(std::size_t row) const;
    // Whether the view reads column (a sort or filter column).
    [[nodiscard]] bool uses_column(std::size_t column) const noexcept;
    // Whether the index has grown sparse enough to rebuild (erasures leave
    // partly filled nodes).
    [[nodiscard]] bool wants_rebuild() const noexcept;

    // Rows [row, row + count) were inserted. Appends the shown ones' view
    // positions after the insertion, ascending, to added.
    void insert_rows(std::size_t row, std::size_t count, BindingKeyReader& reader, std::vector<std::size_t>& added);
    // Rows [row, row + count) were removed. Appends the shown ones' view
    // positions before the removal, descending, to removed.
    void remove_rows(std::size_t row, std::size_t count, std::vector<std::size_t>& removed);
    // Rows [row, row + count) moved so the first is at target afterwards.
    // Appends the shown rows' moves in the view, as single-row steps in
    // order (each relative to the view the steps before it left), to moves;
    // a shown row whose view position does not change adds none.
    void move_rows(std::size_t row, std::size_t count, std::size_t target, std::vector<BindingRowMove>& moves);
    // A cell (or the whole row, binding_whole_row) changed.
    RowChange change_row(std::size_t row, std::size_t column, BindingKeyReader& reader);

    // Storage, for bounded-memory checks.
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t text_bytes() const noexcept { return text_.size(); }

private:
    // A compact owned copy of one key. bits holds a boolean, an integer or a
    // double's bits by kind; text is text_[text_offset, + text_size).
    struct KeyCell final {
        std::uint64_t bits{};
        std::uint64_t text_offset{};
        std::uint32_t text_size{};
        BindingValueKind kind{BindingValueKind::null};
        bool error{};
    };

    // The view's order on slots: keys, then the tie-break.
    struct ViewOrder final {
        const BindingView* view{};
        bool before(std::uint32_t left, std::uint32_t right) const;
    };

    std::uint32_t allocate_slot();
    void release_slot(std::uint32_t slot) noexcept;
    // Reads the row's used columns, stores its keys and sets its membership.
    void load_slot(std::uint32_t slot, std::size_t row, BindingKeyReader& reader);
    void drop_keys(std::uint32_t slot) noexcept;
    void store_key(KeyCell& cell, const BindingValue& value, bool valid);
    [[nodiscard]] BindingKeyView key_view(const KeyCell& cell) const noexcept;
    // The key order alone: negative, zero or positive.
    [[nodiscard]] int compare_keys(std::uint32_t left, std::uint32_t right) const noexcept;
    // The tie-break (the owner's ruling, 2026-11-10): equal keys keep the
    // source order, given here as each slot's source position. The one
    // place the order of equal keys is decided.
    [[nodiscard]] static bool tie_before(std::size_t left_position, std::size_t right_position) noexcept;
    void compact_text();

    std::vector<BindingViewKey> keys_;
    std::vector<BindingViewPredicate> filter_;
    // The distinct columns read per row, and where each key and predicate
    // finds its value in scratch_.
    std::vector<std::size_t> used_columns_;
    std::vector<std::size_t> key_value_;
    std::vector<std::size_t> predicate_value_;
    std::vector<BindingValue> scratch_;
    std::vector<unsigned char> scratch_valid_;
    // Slot s's keys are cells_[s * keys_.size(), (s + 1) * keys_.size()).
    std::vector<KeyCell> cells_;
    std::vector<unsigned char> member_;
    std::vector<unsigned char> live_;
    std::vector<std::uint32_t> free_slots_;
    std::string text_;
    std::size_t dead_text_{};
    std::size_t erasures_since_build_{};
    BindingOrderTree source_order_;
    BindingOrderTree view_order_;
};

} // namespace gui_forms::detail
