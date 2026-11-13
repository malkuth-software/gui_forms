// RowHeightIndex (audit N94, owner ruling D10 b), GUI.Forms' adapter over
// stx_btree's RowHeightIndex: a million rows, jumped into anywhere,
// re-measured, and spliced in bulk, checked against a plain vector of
// heights; then the 1/64-unit quantization, coarsen, the cursors and the
// batched stores. Heights are whole numbers unless a test says otherwise, so
// every sum is exact.

#include "../src/controls/collections/row_height_index.hpp"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
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

// The depth bound of the stx sequence tree's weak B-tree for `rows` rows: a
// tree of depth d >= 2 has a root of at least 2 children, branches below it
// of at least 5 (21 slots / 4) and leaves of at least 31 rows (127 / 4), so
// at least 2 * 5^(d - 2) * 31 rows.
std::size_t depth_bound(std::size_t rows) {
    std::size_t depth = 1U;
    std::size_t least = 2U * 31U;
    while (least <= rows) {
        ++depth;
        if (least > std::numeric_limits<std::size_t>::max() / 5U) break;
        least = least * 5U;
    }
    return depth;
}

void require_structure(const gui_forms::RowHeightIndex& index, const char* stage) {
    const char* failure = index.invariant_failure();
    if (failure != nullptr) throw std::runtime_error(std::string(stage) + ": " + failure);
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
    require(index.depth() <= depth_bound(index.count()), "depth within the weak B-tree's bound after splices");
    require_structure(index, "after splices");

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
    require(index.depth() <= depth_bound(index.count()), "depth within the bound");
    const std::size_t kept_head = 10U;
    const std::size_t kept_tail = 10U;
    index.erase(kept_head, index.count() - kept_head - kept_tail);
    reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(kept_head),
                    reference.end() - static_cast<std::ptrdiff_t>(kept_tail));
    require(index.count() == 20U, "twenty rows left");
    require(index.depth() <= depth_bound(index.count()),
            "erasing most rows brings the depth down to the bound for what is left");
    require_structure(index, "compacted");
    require(index.node_count() <= index.run_count() + 1U, "the compacted tree holds no empty nodes");
    std::vector<std::size_t> rows{};
    for (std::size_t row = 0U; row < reference.size(); ++row) rows.push_back(row);
    require_matches(index, reference, rows, "compacted");
    require(depth_bound(std::numeric_limits<std::size_t>::max()) <= 28U, "the bound covers every size_t count");
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

// The measured count is kept through edits; forget returns a range to the
// estimate in place; storage_bytes follows the runs and falls back once they
// are forgotten.
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
    // A scattered measured row takes at least one 8-byte packed entry.
    require(grown > empty_bytes + 20'000U * 8U, "the bytes cover at least the measured runs");
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

// Heights are kept in 1/64 units, rounded to nearest; offsets are the exact
// sums of the stored heights; a height is held at maximum_height; an
// estimate is stored the same way.
void test_quantization() {
    gui_forms::RowHeightIndex index(20.01);
    require(index.estimated_height() == gui_forms::RowHeightIndex::stored_estimate(20.01) &&
                index.estimated_height() == 1281.0 / 64.0,
            "the estimate is rounded to a unit");
    index.assign(4U, 20.0);
    require(index.set_height(1U, 10.3), "a fractional height is stored");
    require(index.height_of(1U) == 659.0 / 64.0 && index.height_of(1U) == gui_forms::RowHeightIndex::stored_height(10.3),
            "a height is rounded to the nearest 1/64");
    require(!index.set_height(1U, 10.2999), "a height that rounds to the same unit is no change");
    require(index.offset_of(2U) == 20.0 + 659.0 / 64.0 && index.total_height() == 60.0 + 659.0 / 64.0,
            "offsets are the exact sums of the stored heights");
    require(index.row_at(20.0 + 659.0 / 64.0) == 2U && index.row_at(20.0 + 658.5 / 64.0) == 1U,
            "row_at works in whole units");
    require(index.set_height(3U, 1.0e30), "a huge height is held, not refused");
    require(index.height_of(3U) == gui_forms::RowHeightIndex::maximum_height, "held at the tallest row");
    require(!index.set_height(2U, std::nan("")) && !index.set_height(2U, std::numeric_limits<double>::infinity()),
            "heights that are not finite are refused");
    require(index.row_at(std::nan("")) == 0U, "an offset that is not a number finds the first row");
    bool threw = false;
    const std::uint64_t revision = index.revision();
    try {
        index.insert(0U, std::numeric_limits<std::size_t>::max());
    } catch (const std::length_error&) {
        threw = true;
    }
    require(threw && index.count() == 4U && index.revision() == revision, "an insertion past the limits throws and changes nothing");
}

// Coarsen keeps the rows and every offset outside its range bit for bit,
// drops the range's measured state, and shrinks its storage to a run.
void test_coarsen() {
    gui_forms::RowHeightIndex index(estimate);
    index.assign(100'000U, estimate);
    Sequence sequence{};
    for (std::size_t row = 0U; row < 100'000U; ++row) {
        const double height = static_cast<double>(4U + sequence.next(60U)) + 0.25;
        static_cast<void>(index.set_height(row, height));
    }
    require(index.measured_count() == 100'000U, "every row measured");
    std::vector<double> before(100'001U, 0.0);
    for (std::size_t row = 0U; row <= 100'000U; ++row) before[row] = index.offset_of(row);
    const std::size_t measured_bytes = index.storage_bytes();
    index.coarsen(60'000U, 40'000U);
    index.coarsen(0U, 30'000U);
    require_structure(index, "coarsened");
    require(index.count() == 100'000U && index.measured_count() == 30'000U, "coarsen keeps the rows, not their state");
    require(!index.measured(0U) && !index.measured(99'999U) && index.measured(30'000U) && index.measured(59'999U),
            "the coarsened rows are no longer measured");
    bool kept = index.offset_of(0U) == before[0U] && index.total_height() == before[100'000U];
    for (std::size_t row = 30'000U; row <= 60'000U; ++row) {
        if (index.offset_of(row) != before[row]) kept = false;
    }
    require(kept, "offsets outside the coarsened ranges and the total are unchanged");
    require(index.storage_bytes() < measured_bytes, "coarsened rows release their storage");
    require(index.run_count() <= 30'000U + 16U, "the coarsened ranges are a few runs");
    // A coarsened row measured again is measured.
    require(index.set_height(70'000U, 9.0) && index.measured(70'000U), "a coarsened row can be measured again");
}

// A cursor walks every row with the tops and heights offset_of and height_of
// give; seek_offset finds row_at's row; seek_offset_from agrees with a
// fresh seek; a stale cursor re-seeks on advance.
void test_cursors() {
    gui_forms::RowHeightIndex index(estimate);
    index.assign(50'000U, estimate);
    Sequence sequence{};
    for (std::size_t step = 0U; step < 5'000U; ++step) {
        const std::size_t row = sequence.next(50'000U);
        static_cast<void>(index.set_height(row, static_cast<double>(1U + sequence.next(90U))));
    }
    gui_forms::RowHeightIndex::Cursor cursor{};
    index.seek_row(cursor, 0U);
    bool walked = true;
    std::size_t visited = 0U;
    while (!cursor.at_end()) {
        const std::size_t row = cursor.row();
        if (row != visited || cursor.top() != index.offset_of(row) || cursor.height() != index.height_of(row) ||
            cursor.measured() != index.measured(row)) {
            walked = false;
        }
        ++visited;
        index.advance(cursor);
    }
    require(walked && visited == 50'000U, "a cursor walks every row as the queries see it");
    require(cursor.row() == 50'000U && cursor.top() == index.total_height() && cursor.height() == 0.0,
            "the end is after the last row");
    gui_forms::RowHeightIndex::Cursor near{};
    index.seek_offset(near, 1000.0);
    bool sought = true;
    for (std::size_t step = 0U; step < 2'000U; ++step) {
        const double offset = static_cast<double>(sequence.next(static_cast<std::size_t>(index.total_height()))) + 0.5;
        index.seek_offset_from(near, offset);
        gui_forms::RowHeightIndex::Cursor fresh{};
        index.seek_offset(fresh, offset);
        if (near.row() != index.row_at(offset) || fresh.row() != near.row() || near.top() != fresh.top()) {
            sought = false;
        }
    }
    require(sought, "seek_offset and seek_offset_from find row_at's row");
    index.seek_row(cursor, 100U);
    static_cast<void>(index.set_height(50U, 77.0));
    index.advance(cursor);
    require(cursor.row() == 101U && cursor.top() == index.offset_of(101U), "a stale cursor re-seeks and advances");
}

// set_heights and the stage store what set_height a row at a time stores,
// and refuse a bad batch as a whole.
void test_batches() {
    gui_forms::RowHeightIndex batched(estimate);
    gui_forms::RowHeightIndex single(estimate);
    batched.assign(10'000U, estimate);
    single.assign(10'000U, estimate);
    Sequence sequence{};
    std::vector<double> heights{};
    for (std::size_t step = 0U; step < 200U; ++step) {
        const std::size_t first = sequence.next(9'000U);
        const std::size_t length = 1U + sequence.next(400U);
        heights.clear();
        for (std::size_t row = 0U; row < length; ++row) heights.push_back(static_cast<double>(sequence.next(100U)) / 3.0);
        const std::size_t stored = first + length <= 10'000U ? length : 10'000U - first;
        const std::span<const double> batch(heights.data(), stored);
        require(batched.set_heights(first, batch), "a batch is stored");
        for (std::size_t row = 0U; row < stored; ++row) static_cast<void>(single.set_height(first + row, heights[row]));
    }
    bool same = batched.total_height() == single.total_height() && batched.measured_count() == single.measured_count();
    for (std::size_t row = 0U; row <= 10'000U; ++row) {
        if (batched.offset_of(row) != single.offset_of(row)) same = false;
    }
    require(same, "batches store what single stores store");
    require_structure(batched, "batched");
    const std::uint64_t revision = batched.revision();
    const double refused[3] = {1.0, std::nan(""), 2.0};
    require(!batched.set_heights(5U, std::span<const double>(refused, 3U)), "a batch with a height not finite is refused");
    const double past[2] = {1.0, 2.0};
    require(!batched.set_heights(9'999U, std::span<const double>(past, 2U)), "a batch past the end is refused");
    require(batched.revision() == revision && batched.offset_of(10'000U) == single.offset_of(10'000U),
            "refused batches change nothing");
    // The stage: only the first `length` staged heights are stored.
    batched.clear_staged();
    require(batched.stage_height(3.0) && batched.stage_height(4.0) && batched.stage_height(5.0) && !batched.stage_height(-1.0),
            "staging refuses a negative height");
    require(batched.staged_count() == 3U, "three staged");
    require(batched.store_staged(20U, 2U), "two of them stored");
    require(batched.staged_count() == 0U && batched.height_of(20U) == 3.0 && batched.height_of(21U) == 4.0 &&
                batched.measured(21U) && batched.height_of(22U) == single.height_of(22U),
            "the stage stores its first rows and clears");
}

} // namespace

int main() {
    try {
        test_empty_and_edges();
        test_million_rows();
        test_depth_follows_the_count();
        test_move();
        test_measured_count_forget_and_bytes();
        test_quantization();
        test_coarsen();
        test_cursors();
        test_batches();
        std::cout << "row height index tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "row height index tests failed: " << error.what() << '\n';
        return 1;
    }
}
