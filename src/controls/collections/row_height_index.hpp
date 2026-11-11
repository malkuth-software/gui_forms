#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace gui_forms {

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
// Leaves hold runs and branches hold children, at most node_capacity each.
// Totals are recomputed from the children on every path a change touches (no
// running deltas), so rounding does not accumulate. Plain node layout; the
// branchless or SIMD layout is a later optimisation (owner, 2026-11-10).
//
// The structure is a value owned by one control and used on its thread.
// Depth is O(log n) of the largest row count it has held: nodes emptied by an
// erase are removed and a one-child root is collapsed, but partly filled
// nodes are not merged (their count is bounded by the runs). Recursion is
// bounded by that depth.
class RowHeightIndex final {
public:
    static constexpr std::size_t node_capacity = 32U;

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

    // Storage, for bounded-memory checks: nodes and runs held.
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t run_count() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

    // Advances on every change to a count or a height (assign, a
    // set_height that changed a value, insert, erase), so a holder can
    // key a kept layout on it. A move carries it; it never goes back.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    struct Run final {
        std::size_t count{};
        double height{};
        bool measured{};
    };
    struct Node final {
        bool leaf{true};
        std::size_t count{};
        double height{};
        // A leaf's runs, in row order.
        std::vector<Run> runs{};
        // A branch's children, in row order, with each one's totals
        // (repeated here so a descent reads one array).
        std::vector<std::unique_ptr<Node>> children{};
        std::vector<std::size_t> child_counts{};
        std::vector<double> child_heights{};
    };

    static void recompute(Node& node) noexcept;
    static void merge_neighbours(std::vector<Run>& runs, std::size_t index);
    // Splits an overfull node in two; returns the right half.
    static std::unique_ptr<Node> split(Node& node);
    // Adopts a split-off right sibling after `index` in a branch.
    static void adopt_split(Node& branch, std::size_t index, std::unique_ptr<Node> sibling);
    static std::unique_ptr<Node> set_height_in(Node& node, std::size_t row, double height, bool& changed);
    std::unique_ptr<Node> insert_in(Node& node, std::size_t row, std::size_t count);
    static void erase_in(Node& node, std::size_t row, std::size_t count);
    static std::size_t count_nodes(const Node& node) noexcept;
    static std::size_t count_runs(const Node& node) noexcept;
    void grow_root(std::unique_ptr<Node> sibling);
    void shrink_root();

    std::unique_ptr<Node> root_{};
    double estimate_{1.0};
    std::uint64_t revision_{0U};
};

} // namespace gui_forms
