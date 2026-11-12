// RowHeightIndex (audit N94, owner ruling D10 b): a million rows, jumped
// into anywhere, re-measured, and spliced in bulk, checked against a plain
// vector of heights. Heights are whole numbers so every sum is exact.

#include "../src/controls/collections/row_height_index.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A fixed linear congruential sequence, so a failure reproduces.
struct Sequence final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};

    std::size_t next(std::size_t bound) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t high = state >> 33U;
        const std::size_t value = static_cast<std::size_t>(high % static_cast<std::uint64_t>(bound));
        return value;
    }
};

// Checks the index against the reference at every row in `rows` (and the
// totals), from one prefix-sum pass over the reference.
void require_matches(const gui_forms::RowHeightIndex& index, const std::vector<double>& reference,
                     const std::vector<std::size_t>& rows, const char* stage) {
    const std::string prefix(stage);
    require(index.count() == reference.size(), (prefix + ": count").c_str());
    std::vector<double> tops(reference.size() + 1U, 0.0);
    for (std::size_t row = 0; row < reference.size(); ++row) tops[row + 1U] = tops[row] + reference[row];
    require(index.total_height() == tops[reference.size()], (prefix + ": total height").c_str());
    for (const std::size_t row : rows) {
        if (row >= reference.size()) continue;
        require(index.offset_of(row) == tops[row], (prefix + ": offset_of").c_str());
        require(index.height_of(row) == reference[row], (prefix + ": height_of").c_str());
        // Jump anywhere: the top, the middle and the last point of the row.
        require(index.row_at(tops[row]) == row, (prefix + ": row_at top").c_str());
        if (reference[row] > 1.0) {
            const double middle = tops[row] + reference[row] / 2.0;
            require(index.row_at(middle) == row, (prefix + ": row_at middle").c_str());
        }
    }
}

std::vector<std::size_t> sample_rows(std::size_t count, Sequence& sequence) {
    std::vector<std::size_t> rows{};
    rows.reserve(2100U);
    if (count == 0U) return rows;
    rows.push_back(0U);
    rows.push_back(count - 1U);
    rows.push_back(count / 2U);
    for (std::size_t sample = 0; sample < 2000U; ++sample) rows.push_back(sequence.next(count));
    return rows;
}

constexpr std::size_t million = 1000000U;
constexpr double estimate = 20.0;

void test_empty_and_edges() {
    gui_forms::RowHeightIndex index(estimate);
    require(index.count() == 0U, "empty count");
    require(index.total_height() == 0.0, "empty total");
    require(index.row_at(50.0) == 0U, "empty row_at");
    require(index.offset_of(3U) == 0.0, "empty offset_of");
    require(!index.set_height(0U, 5.0), "set past the end refused");
    index.insert(0U, 3U);
    require(index.count() == 3U && index.total_height() == 60.0, "insert into empty");
    require(index.row_at(-5.0) == 0U, "row_at above the top");
    require(index.row_at(1.0e9) == 2U, "row_at past the bottom");
    require(index.offset_of(10U) == 60.0, "offset_of clamps");
    require(!index.set_height(1U, -1.0), "negative height refused");
    require(index.set_height(1U, 7.0), "set height");
    require(!index.set_height(1U, 7.0), "same height is no change");
    require(index.measured(1U) && !index.measured(0U), "measured state");
    index.erase(0U, 100U);
    require(index.count() == 0U && index.total_height() == 0.0, "erase all");
    gui_forms::RowHeightIndex fallback(-3.0);
    fallback.insert(0U, 2U);
    require(fallback.total_height() == 2.0, "a bad estimate falls back to 1");
}

void test_million_rows() {
    Sequence sequence{};
    gui_forms::RowHeightIndex index(estimate);
    index.assign(million, estimate);
    std::vector<double> reference(million, estimate);
    // Startup: one run, no measuring pass.
    require(index.run_count() == 1U, "a fresh million rows are one run");
    require_matches(index, reference, sample_rows(million, sequence), "assigned");

    // Re-measure 20000 scattered rows, and a block of 5000 measured at the
    // estimate (which must not leave 5000 runs behind).
    for (std::size_t step = 0; step < 20000U; ++step) {
        const std::size_t row = sequence.next(million);
        const double height = static_cast<double>(12U + sequence.next(4U) * 4U);
        index.set_height(row, height);
        reference[row] = height;
    }
    for (std::size_t row = 400000U; row < 405000U; ++row) {
        index.set_height(row, estimate);
        reference[row] = estimate;
    }
    require_matches(index, reference, sample_rows(million, sequence), "re-measured");
    require(index.run_count() <= 2U * 20000U + 2U + 5000U, "runs bounded by the measured rows");
    require(index.depth() <= 5U, "depth stays logarithmic");

    // Bulk insert 250000 rows in the middle and erase 300000 across the
    // measured block.
    index.insert(600000U, 250000U);
    reference.insert(reference.begin() + 600000, 250000U, estimate);
    require_matches(index, reference, sample_rows(reference.size(), sequence), "bulk insert");
    index.erase(300000U, 300000U);
    reference.erase(reference.begin() + 300000, reference.begin() + 600000);
    require_matches(index, reference, sample_rows(reference.size(), sequence), "bulk erase");

    // Many small splices.
    for (std::size_t step = 0; step < 2000U; ++step) {
        const std::size_t row = sequence.next(reference.size());
        const std::size_t count = 1U + sequence.next(40U);
        if (step % 2U == 0U) {
            index.insert(row, count);
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(row), count, estimate);
        } else {
            const std::size_t end = row + count < reference.size() ? row + count : reference.size();
            index.erase(row, count);
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(row),
                            reference.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    require_matches(index, reference, sample_rows(reference.size(), sequence), "small splices");

    // Bounded memory: storage follows the runs, not the rows.
    const std::size_t runs = index.run_count();
    require(runs <= 2U * 20000U + 5000U + 4U * 2000U + 8U, "runs bounded after splices");
    require(index.node_count() <= runs, "nodes no more than the runs");
    require(index.depth() <= 5U, "depth after splices");
    require(index.depth() <= gui_forms::RowHeightIndex::Tree::depth_limit(index.count()),
            "depth within the family's bound after splices");

    // Erase everything, then grow again from empty.
    index.erase(0U, index.count());
    require(index.count() == 0U && index.total_height() == 0.0, "erased to empty");
    require(index.run_count() == 0U, "no runs left");
    index.insert(0U, million);
    std::vector<double> regrown(million, estimate);
    require_matches(index, regrown, sample_rows(million, sequence), "regrown");
    require(index.run_count() == 1U, "regrown is one run");
}

// Many runs, then most of the rows erased: the tree compacts down to the
// depth bound for what is left, and every row still answers as before.
void test_depth_follows_the_count() {
    gui_forms::RowHeightIndex index(estimate);
    index.assign(200000U, estimate);
    std::vector<double> reference(200000U, estimate);
    for (std::size_t row = 0U; row < 100000U; row += 2U) {
        const double height = (row / 2U) % 2U == 0U ? 10.0 : 30.0;
        index.set_height(row, height);
        reference[row] = height;
    }
    require(index.run_count() >= 100000U, "alternating measures leave a run per row");
    require(index.depth() >= 3U, "that many runs need several levels");
    require(index.depth() <= gui_forms::RowHeightIndex::Tree::depth_limit(index.count()), "depth within the bound");
    const std::size_t kept_head = 10U;
    const std::size_t kept_tail = 10U;
    index.erase(kept_head, index.count() - kept_head - kept_tail);
    reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(kept_head),
                    reference.end() - static_cast<std::ptrdiff_t>(kept_tail));
    require(index.count() == 20U, "twenty rows left");
    require(index.depth() <= gui_forms::RowHeightIndex::Tree::depth_limit(index.count()),
            "erasing most rows brings the depth down to the bound for what is left");
    require(index.node_count() <= index.run_count() + 1U, "the compacted tree holds no empty nodes");
    std::vector<std::size_t> rows{};
    for (std::size_t row = 0U; row < reference.size(); ++row) rows.push_back(row);
    require_matches(index, reference, rows, "compacted");
    require(gui_forms::RowHeightIndex::Tree::max_depth <= 17U, "the bound covers every size_t count");
}

void test_move() {
    gui_forms::RowHeightIndex index(estimate);
    index.assign(10U, estimate);
    index.set_height(4U, 3.0);
    gui_forms::RowHeightIndex moved(std::move(index));
    require(moved.count() == 10U && moved.total_height() == 183.0, "move construction");
    gui_forms::RowHeightIndex target(1.0);
    target = std::move(moved);
    require(target.offset_of(5U) == 83.0, "move assignment");
}

// The measured count is the root's summary, kept through edits; forget
// returns a range to the estimate in place; storage_bytes follows the runs
// and falls back once they are forgotten.
void test_measured_count_forget_and_bytes() {
    gui_forms::RowHeightIndex index(estimate);
    index.assign(1'000'000U, estimate);
    const std::size_t empty_bytes = index.storage_bytes();
    require(index.measured_count() == 0U && empty_bytes > 0U, "a fresh index measures nothing and holds a node");
    for (std::size_t row = 0U; row < 20'000U; ++row) {
        index.set_height(row * 50U, 1.0 + static_cast<double>(row % 7U));
    }
    require(index.measured_count() == 20'000U, "every measured row is counted");
    const std::size_t grown = index.storage_bytes();
    require(grown > empty_bytes + 20'000U * sizeof(gui_forms::detail::RowRun),
            "the bytes cover at least the measured runs");
    index.erase(0U, 500U);
    require(index.measured_count() == 19'990U, "an erase removes its measured rows from the count");
    index.insert(0U, 500U);
    const double before = index.offset_of(999'000U);
    const double height = index.height_of(999'000U);
    index.forget(500U, 498'500U);
    require(index.count() == 1'000'000U && index.measured_count() == 10'020U,
            "forget keeps the rows and returns its range to the estimate");
    require(!index.measured(999U * 50U) && index.measured(999'000U) && index.height_of(999'000U) == height,
            "rows outside the forgotten range keep their heights");
    require(index.offset_of(999'000U) != before, "forgotten heights change the offsets after them");
    index.forget(0U, index.count());
    require(index.measured_count() == 0U && index.storage_bytes() < grown,
            "forgetting every row releases the measured runs' storage");
}

} // namespace

int main() {
    try {
        test_empty_and_edges();
        test_million_rows();
        test_depth_follows_the_count();
        test_move();
        test_measured_count_forget_and_bytes();
        std::cout << "row height index tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "row height index tests failed: " << error.what() << '\n';
        return 1;
    }
}
