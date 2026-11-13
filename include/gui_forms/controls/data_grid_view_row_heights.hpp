#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

class RowHeightIndex;

// One row's own height, by its presentation position, for
// DataGridViewRowHeights::reset.
struct GUI_FORMS_API DataGridViewRowHeight final {
    std::size_t position{};
    double height{};

    friend bool operator==(const DataGridViewRowHeight& left, const DataGridViewRowHeight& right) noexcept {
        const bool equal = left.position == right.position && left.height == right.height;
        return equal;
    }
};

// A grid's row heights by presentation position, renderer-free, in logical
// units at text scale 1 (owner decision of 2026-11-02: real per-row heights
// for every row source, with no linear walk in scrolling, hit testing or
// painting).
//
// The one data engine (owner, 2026-11-13): the heights live in the row
// index VirtualRowsView uses, GUI.Forms' RowHeightIndex over stx_btree's
// order-statistic tree (src/controls/collections/row_height_index.hpp).
// Every row has the default height except those given their own. Rows at
// the default are estimated rows of the index: a million of them are one
// run, and reset costs O(1) before the heights given. A row's height, its
// top, the row at an offset and setting one row's height cost O(log
// count); reset with heights stores each run of consecutive positions in
// one leaf pass.
//
// Heights are kept as 26.6 fixed point, 64 units a logical unit, each
// rounded to the nearest unit (stored_height). Offsets are exact integer
// sums of the stored heights, so a row's top does not depend on the order
// of earlier edits. Single UI thread. Not copyable; moved only by its
// owner's construction.
class GUI_FORMS_API DataGridViewRowHeights final {
public:
    DataGridViewRowHeights();
    ~DataGridViewRowHeights();
    DataGridViewRowHeights(const DataGridViewRowHeights&) = delete;
    DataGridViewRowHeights& operator=(const DataGridViewRowHeights&) = delete;

    // `count` rows of `default_height`, none with its own.
    void reset(std::size_t count, double default_height);
    // As reset(count, default_height), then each given height in order; a
    // later height for the same position wins. Positions must be below
    // `count` and heights positive (the grid validates them first).
    void reset(std::size_t count, double default_height, std::span<const DataGridViewRowHeight> heights);
    // One row's height; position below count(), height positive.
    void set_height(std::size_t position, double height);

    [[nodiscard]] std::size_t count() const noexcept;
    // The default as given to reset (the index stores it rounded).
    [[nodiscard]] double default_height() const noexcept { return default_height_; }
    // Whether every row has the default height (no row measured).
    [[nodiscard]] bool uniform() const noexcept;
    // The height of the row at a position below count(), as stored.
    [[nodiscard]] double height(std::size_t position) const noexcept;
    // The distance from the first row's top to a row's top: the heights of
    // the rows before `position`, which is at most count() (count() gives
    // the total).
    [[nodiscard]] double top(std::size_t position) const noexcept;
    [[nodiscard]] double total() const noexcept;
    // The row whose span [top, top + height) holds `offset`: 0 for an offset
    // before the first row, count() for one at or past the total.
    [[nodiscard]] std::size_t position_at(double offset) const noexcept;
    // A height as this index stores it (rounded to 1/64 logical unit).
    [[nodiscard]] static double stored_height(double height) noexcept;
    // The index's heap bytes, for bounded-memory checks (RowHeightIndex::
    // storage_bytes).
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    // Stores one row's height: measured when it differs from the default,
    // else returned to the estimate.
    void store(std::size_t position, double height);

    // Owned; never null. Held by pointer so this public header does not
    // carry the stx tree.
    std::unique_ptr<RowHeightIndex> index_;
    double default_height_{};
};

} // namespace gui_forms
