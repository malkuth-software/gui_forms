#pragma once

#include "../../core/collections/order_statistic_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

namespace detail {

// A run of rows sharing one height and state: the entries of a
// RowHeightIndex.
struct RowRun final {
    std::size_t count{};
    double height{};
    bool measured{};
};

// A node's summary: its subtree's total height and how many of its rows are
// measured (so the whole index's measured rows are an O(1) read).
struct RowSummary final {
    double height{};
    std::size_t measured{};
};

struct RowRuns final {
    using Entry = RowRun;
    using Summary = RowSummary;
    static constexpr bool tracks_leaves = false;

    [[nodiscard]] static std::size_t entry_count(const RowRun& run) noexcept { return run.count; }
    [[nodiscard]] static std::uint32_t entry_key(const RowRun&) noexcept { return 0U; }
    [[nodiscard]] static RowSummary summarize_entries(std::span<const RowRun> runs) noexcept {
        RowSummary summary{};
        for (const RowRun& run : runs) {
            summary.height += static_cast<double>(run.count) * run.height;
            if (run.measured) summary.measured += run.count;
        }
        return summary;
    }
    [[nodiscard]] static RowSummary summarize_children(std::span<const RowSummary> children) noexcept {
        RowSummary summary{};
        for (const RowSummary& child : children) {
            summary.height += child.height;
            summary.measured += child.measured;
        }
        return summary;
    }
};

} // namespace detail

// The vertical extent of a long list of rows (audit N94, owner ruling D10 b,
// POST_0.5_BACKLOG "Startup bar"): a height-indexed B-tree whose nodes keep
// their subtree's total height and row count.
//
//   offset_of(row) and row_at(offset)   O(log n)
//   set_height(row) (a re-measure)      O(log n)
//   insert(row, count), erase(row, count) of any size   O(log n) node visits
//                                       per touched path (a bulk erase visits
//                                       the nodes it empties as well)
//
// Rows not yet measured carry the estimated height. They are kept as runs (a
// count and one height per run), so a million estimated rows are one run and
// startup needs no measuring pass; memory grows with the runs that measuring
// and edits create, not with the row count. A measured row is a run of one
// until its neighbours match it.
//
// The runs are the entries of the shared order-statistic B-tree family
// (core/collections/order_statistic_tree.hpp, also behind BindingOrderTree):
// leaves hold runs and branches hold children, at most node_capacity each,
// and totals are recomputed from the children on every path a change touches
// (no running deltas), so rounding does not accumulate. Plain node layout;
// the branchless or SIMD layout is a later optimisation (owner, 2026-11-10).
//
// The structure is a value owned by one control and used on its thread.
// Nothing recurses. The depth is at most Tree::depth_limit(count()), which
// is never more than Tree::max_depth (17 for a 64-bit size_t): nodes emptied
// by an erase are removed and a one-child root is collapsed, partly filled
// nodes are not merged (their count is bounded by the runs), and a change
// that would leave the tree deeper than the limit compacts it.
class RowHeightIndex final {
public:
    using Tree = detail::OrderStatisticTree<detail::RowRuns>;
    static constexpr std::size_t node_capacity = Tree::node_capacity;

    // An empty index whose new rows are estimated at `estimated_height`
    // (finite and > 0; otherwise 1 is used).
    explicit RowHeightIndex(double estimated_height);
    ~RowHeightIndex();
    RowHeightIndex(const RowHeightIndex&) = delete;
    RowHeightIndex& operator=(const RowHeightIndex&) = delete;
    RowHeightIndex(RowHeightIndex&& other) noexcept;
    RowHeightIndex& operator=(RowHeightIndex&& other) noexcept;

    // Replaces every row with `count` estimated rows at `estimated_height`,
    // which becomes the estimate for later insertions. O(1) after releasing
    // the old nodes.
    void assign(std::size_t count, double estimated_height);

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] double total_height() const noexcept;
    [[nodiscard]] double estimated_height() const noexcept { return estimate_; }

    // The top of `row`; `row == count()` gives total_height(). Rows past
    // that are clamped to it.
    [[nodiscard]] double offset_of(std::size_t row) const noexcept;
    // The row whose extent holds `offset`: 0 for an offset at or above 0,
    // the last row for one at or past the bottom. 0 for an empty index.
    [[nodiscard]] std::size_t row_at(double offset) const noexcept;
    // The row's height, measured or estimated; 0 past the end.
    [[nodiscard]] double height_of(std::size_t row) const noexcept;
    [[nodiscard]] bool measured(std::size_t row) const noexcept;

    // Records a measured height (finite, >= 0; otherwise refused with
    // false). A row past the end is refused too. Returns whether the
    // stored height or state changed.
    bool set_height(std::size_t row, double height);
    // Inserts `count` estimated rows before `row` (clamped to count()).
    void insert(std::size_t row, std::size_t count);
    // Erases rows [row, row + count) clamped to the end.
    void erase(std::size_t row, std::size_t count);
    // Returns rows [row, row + count) (clamped to the end) to the estimate,
    // unmeasured; every row keeps its position. An erase and an insert of
    // as many estimated rows, so the forgotten runs' nodes are released.
    void forget(std::size_t row, std::size_t count);
    // Rows holding a measured height, O(1) (the root's summary).
    [[nodiscard]] std::size_t measured_count() const noexcept;

    // Storage, for bounded-memory checks: nodes and runs held.
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t run_count() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;
    // The heap bytes the index holds: each node and the capacity of its
    // entry, child, count and summary arrays. Walks every node (O(nodes))
    // with one frame per level, no recursion and no allocation.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

    // Advances on every change to a count or a height (assign, a
    // set_height that changed a value, insert, erase), so a holder can
    // key a kept layout on it. A move carries it; it never goes back.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    using Run = detail::RowRun;
    using Node = Tree::Node;

    static void merge_neighbours(std::vector<Run>& runs, std::size_t index);
    // One node's heap bytes (storage_bytes).
    [[nodiscard]] static std::size_t node_bytes(const Node& node) noexcept;
    // Erases up to `count` rows of the leaf's runs from `row` (within the
    // leaf) and joins equal neighbours; returns the rows erased.
    static std::size_t erase_from_leaf(std::vector<Run>& runs, std::size_t row, std::size_t count);

    Tree tree_{};
    double estimate_{1.0};
    std::uint64_t revision_{0U};
};

} // namespace gui_forms
