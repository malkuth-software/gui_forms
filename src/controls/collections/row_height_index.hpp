#pragma once

#include <stx_btree/row_height_index.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// The vertical extent of a long list of rows (audit N94, owner ruling D10 b,
// POST_0.5_BACKLOG "Startup bar"): GUI.Forms' adapter over stx_btree's
// RowHeightIndex (stx_auxlib at stx_auxlib.lock's commit,
// btree/include/stx_btree/row_height_index.hpp; its design, invariants,
// proofs and measurements are in stx_auxlib's
// btree/docs/ORDER_STATISTIC_RESEARCH.md).
//
//   offset_of(row), row_at(offset), height_of(row)   O(log n)
//   set_height(row) (a re-measure)                    O(log n)
//   set_heights(row, heights)        one leaf pass per leaf touched
//   insert, erase, forget, coarsen of any size        O(log n) a leaf touched
//   cursors: a seek O(log n), a step O(1) amortized, a seek from a valid
//   cursor O(log_B d) for a target d rows away
//
// Heights cross this API in logical units (double) and are kept inside as
// 26.6 fixed point: 64 units a logical unit, each height rounded to the
// nearest unit (stored_height() gives the stored value). Offsets are exact
// integer sums of the stored heights, so offset_of(row) does not depend on
// the order of earlier edits, and coarsen keeps every offset outside its
// range bit for bit. A height is held within [0, maximum_height]; offsets
// convert to double exactly up to 2^53 units (1.4e14 logical units).
//
// Rows are kept as runs: a million estimated rows are one 8-byte entry, a
// measured row takes 4 bytes in a dense leaf (8 in a packed one), and a
// coarsened stretch is one approximate run holding the stretch's exact
// total. Rows are estimated, measured or approximate; measured() is true
// for measured rows only.
//
// Failure: assign, insert or forget past the index's limits (2^62 - 1 rows
// or units in all) throws std::length_error and leaves the index
// unchanged; a set_height or set_heights past them is refused with false.
// Allocation failure throws std::bad_alloc and leaves the index unchanged.
//
// One control owns the index and uses it on its thread. A cursor is valid
// until the index's next edit; a stale cursor re-seeks (from the root for
// the seeks, from its row for advance). A moved-from index may only be
// destroyed or assigned to.
class RowHeightIndex final {
public:
    using Index = stx_btree::RowHeightIndex<>;
    static constexpr double units_per_logical = 64.0;
    // The tallest row, 2^38 - 1 units.
    static constexpr double maximum_height = static_cast<double>(Index::height_limit.value) / units_per_logical;

    // A position in the rows, with the path to it.
    class Cursor final {
    public:
        [[nodiscard]] std::size_t row() const noexcept;
        [[nodiscard]] double top() const noexcept;
        // 0 at the end.
        [[nodiscard]] double height() const noexcept;
        [[nodiscard]] bool measured() const noexcept;
        [[nodiscard]] bool at_end() const noexcept;

    private:
        friend class RowHeightIndex;
        Index::Cursor cursor_{};
    };

    // An empty index whose new rows are estimated at `estimated_height`
    // (finite and > 0; otherwise 1 is used).
    explicit RowHeightIndex(double estimated_height);
    ~RowHeightIndex();
    RowHeightIndex(const RowHeightIndex&) = delete;
    RowHeightIndex& operator=(const RowHeightIndex&) = delete;
    RowHeightIndex(RowHeightIndex&& other) noexcept;
    RowHeightIndex& operator=(RowHeightIndex&& other) noexcept;

    // Replaces every row with `count` estimated rows at `estimated_height`,
    // which becomes the estimate for later insertions.
    void assign(std::size_t count, double estimated_height);

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] double total_height() const noexcept;
    // The estimate as stored (rounded to a unit).
    [[nodiscard]] double estimated_height() const noexcept;

    // The top of `row`; `row == count()` gives total_height(), and rows
    // past that are clamped to it.
    [[nodiscard]] double offset_of(std::size_t row) const noexcept;
    // The row whose extent holds `offset`: 0 for an offset above the top
    // (or not a number), the last row for one at or past the bottom, 0 for
    // an empty index.
    [[nodiscard]] std::size_t row_at(double offset) const noexcept;
    // The row's height, measured, approximate or estimated; 0 past the end.
    [[nodiscard]] double height_of(std::size_t row) const noexcept;
    [[nodiscard]] bool measured(std::size_t row) const noexcept;

    // Records a measured height (finite and >= 0, held at maximum_height);
    // a height that is not, or a row past the end, is refused with false.
    // Returns whether the stored height or state changed.
    bool set_height(std::size_t row, double height);
    // Records measured heights for rows [row, row + heights.size()), one
    // leaf pass per leaf touched; refused as a whole (false, unchanged)
    // when the range passes the end or any height is not finite or is
    // negative. Heights are held at maximum_height. True when stored.
    bool set_heights(std::size_t row, std::span<const double> heights);
    // Staging, for heights gathered row by row and stored in one pass:
    // stage_height appends a measured height (refused with false when not
    // finite or negative; held at maximum_height); store_staged stores the
    // first `length` staged heights at rows [row, row + length) as
    // set_heights does and clears the stage whether it stored or not;
    // clear_staged drops them. The stage's storage is kept between uses.
    void clear_staged() noexcept;
    bool stage_height(double height);
    [[nodiscard]] std::size_t staged_count() const noexcept;
    bool store_staged(std::size_t row, std::size_t length);
    // Inserts `count` estimated rows before `row` (clamped to count()).
    void insert(std::size_t row, std::size_t count);
    // Erases rows [row, row + count), clamped to the end.
    void erase(std::size_t row, std::size_t count);
    // Returns rows [row, row + count) (clamped to the end) to the estimate,
    // unmeasured; every row keeps its position in the sequence.
    void forget(std::size_t row, std::size_t count);
    // Replaces rows [row, row + count) (clamped to the end) by one
    // approximate run holding their exact total: they are no longer
    // measured, their storage shrinks to an entry or a few, and the offset
    // of every row at or before `row` and at or after the range's end is
    // unchanged bit for bit.
    void coarsen(std::size_t row, std::size_t count);
    // Rows holding a measured height, O(1).
    [[nodiscard]] std::size_t measured_count() const noexcept;

    // Cursors: seek_row places at `row` (past the end: at_end());
    // seek_offset at row_at(offset); seek_offset_from moves a valid cursor
    // there, climbing only as far as needed; advance steps to the next row.
    void seek_row(Cursor& cursor, std::size_t row) const noexcept;
    void seek_offset(Cursor& cursor, double offset) const noexcept;
    void seek_offset_from(Cursor& cursor, double offset) const noexcept;
    void advance(Cursor& cursor) const noexcept;

    // A height as set_height would store it, in logical units; 0 for one
    // it would refuse.
    [[nodiscard]] static double stored_height(double height) noexcept;
    // An estimate as assign or the constructor would store it.
    [[nodiscard]] static double stored_estimate(double estimated_height) noexcept;

    // Storage, for bounded-memory checks, all O(1): nodes, runs (packed
    // entries plus dense rows) and the depth.
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t run_count() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;
    // The heap bytes the index holds: the stx index object (its fixed edit
    // scratch included) and its live pages; the few spare pages it keeps
    // for edits (at most 8 leaves and 32 branches) are not counted. O(1).
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    // The first broken structural or arithmetic invariant of the stx tree
    // (its study, section 8.2), or null when all hold; O(n), for tests.
    [[nodiscard]] const char* invariant_failure() const noexcept;

    // Advances on every change to a count or a height (assign, a
    // set_height that changed a value, a stored set_heights, insert, erase,
    // forget, coarsen), so a holder can key a kept layout on it. A move
    // carries it and advances both sides; it never goes back.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    // A height in units: rounded to nearest, held within [0, the limit].
    [[nodiscard]] static std::int64_t height_units(double height) noexcept;
    // The unit holding a logical offset: floor(offset * 64), held within
    // [-1, total units]; not a number gives 0.
    [[nodiscard]] std::int64_t offset_units(double offset) const noexcept;

    // Owned; held by pointer because the stx index does not move. Null
    // only in a moved-from index.
    std::unique_ptr<Index> index_;
    // The staged heights in units (set_heights and the stage); the
    // capacity is kept between calls.
    std::vector<stx_btree::HeightUnits> batch_{};
    std::uint64_t revision_{0U};
};

} // namespace gui_forms
