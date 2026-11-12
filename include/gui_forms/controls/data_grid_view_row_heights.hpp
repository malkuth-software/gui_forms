#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace gui_forms {

// One row's own height, by its presentation position, for
// DataGridViewRowHeights::reset.
struct DataGridViewRowHeight final {
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
// Every row has the default height except those given their own. While none
// is, nothing is stored and every query is arithmetic. Once one is, the index
// keeps each row's difference from the default and a Fenwick tree of the
// differences, 16 bytes a row. A row's height costs O(1); a row's top, the
// row at an offset and setting one row's height cost O(log count); reset
// with heights costs O(count + heights given).
//
// Offsets are sums of doubles. Heights of a few significant bits (whole and
// half units, quarter units at a text scale) add exactly; others may drift
// by rounding after many set_height calls, which reset clears. Single UI
// thread.
class DataGridViewRowHeights final {
public:
    // `count` rows of `default_height`, none with its own.
    void reset(std::size_t count, double default_height);
    // As reset(count, default_height), then each given height in order; a
    // later height for the same position wins. Positions must be below
    // `count` and heights positive (the grid validates them first).
    void reset(std::size_t count, double default_height, std::span<const DataGridViewRowHeight> heights);
    // One row's height; position below count(), height positive.
    void set_height(std::size_t position, double height);

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] double default_height() const noexcept { return default_height_; }
    // Whether every row has the default height (nothing stored).
    [[nodiscard]] bool uniform() const noexcept { return differences_.empty(); }
    // The height of the row at a position below count().
    [[nodiscard]] double height(std::size_t position) const noexcept;
    // The distance from the first row's top to a row's top: the heights of
    // the rows before `position`, which is at most count() (count() gives
    // the total).
    [[nodiscard]] double top(std::size_t position) const noexcept;
    [[nodiscard]] double total() const noexcept;
    // The row whose span [top, top + height) holds `offset`: 0 for an offset
    // before the first row, count() for one at or past the total.
    [[nodiscard]] std::size_t position_at(double offset) const noexcept;

private:
    // Allocates the differences and the tree, all zero.
    void begin_differences();
    // The sum of the differences of the rows before `position`.
    [[nodiscard]] double difference_before(std::size_t position) const noexcept;

    std::size_t count_{};
    double default_height_{};
    // Per row: its height minus the default; empty while uniform.
    std::vector<double> differences_{};
    // Fenwick tree over differences_: tree_[index - 1] holds the sum over
    // the rows (index - lowest set bit of index, index], 1-based.
    std::vector<double> tree_{};
    double difference_total_{};
    // Rows whose difference is not zero; at zero the storage is released.
    std::size_t different_rows_{};
    // The largest power of two not above count_ (0 for no rows): the first
    // step of position_at's descent.
    std::size_t top_step_{};
};

} // namespace gui_forms
