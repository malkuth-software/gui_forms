#include "row_height_index.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gui_forms {

namespace {

[[nodiscard]] double usable_estimate(double estimated_height) noexcept {
    if (!std::isfinite(estimated_height) || estimated_height <= 0.0) return 1.0;
    return estimated_height;
}

} // namespace

RowHeightIndex::RowHeightIndex(double estimated_height) : tree_(), estimate_(usable_estimate(estimated_height)) {}

RowHeightIndex::~RowHeightIndex() {}

RowHeightIndex::RowHeightIndex(RowHeightIndex&& other) noexcept
    : tree_(std::move(other.tree_)), estimate_(other.estimate_), revision_(other.revision_) {
    ++other.revision_;
}

RowHeightIndex& RowHeightIndex::operator=(RowHeightIndex&& other) noexcept {
    if (this == &other) return *this;
    tree_ = std::move(other.tree_);
    estimate_ = other.estimate_;
    revision_ = std::max(revision_, other.revision_) + 1U;
    ++other.revision_;
    return *this;
}

void RowHeightIndex::assign(std::size_t count, double estimated_height) {
    estimate_ = usable_estimate(estimated_height);
    if (count > 0U) {
        const Run run{count, estimate_, false};
        tree_.assign(std::span<const Run>(&run, 1U), 0U);
    } else {
        tree_.clear();
    }
    ++revision_;
}

std::size_t RowHeightIndex::count() const noexcept {
    return tree_.count();
}

double RowHeightIndex::total_height() const noexcept {
    return tree_.summary();
}

// -- queries (iterative descents) --------------------------------------------

double RowHeightIndex::offset_of(std::size_t row) const noexcept {
    const Node* root = tree_.root();
    if (row >= (*root).count) return (*root).summary;
    double offset = 0.0;
    const Node* node = root;
    std::size_t remaining = row;
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
            remaining -= (*node).child_counts[child];
            offset += (*node).child_summaries[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    for (const Run& run : (*node).entries) {
        if (remaining < run.count) {
            offset += static_cast<double>(remaining) * run.height;
            return offset;
        }
        remaining -= run.count;
        offset += static_cast<double>(run.count) * run.height;
    }
    return offset;
}

std::size_t RowHeightIndex::row_at(double offset) const noexcept {
    const Node* root = tree_.root();
    const std::size_t total = (*root).count;
    if (total == 0U) return 0U;
    if (!(offset > 0.0)) return 0U;
    if (offset >= (*root).summary) return total - 1U;
    std::size_t row = 0U;
    double remaining = offset;
    const Node* node = root;
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_summaries[child]) {
            remaining -= (*node).child_summaries[child];
            row += (*node).child_counts[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    const std::vector<Run>& runs = (*node).entries;
    for (std::size_t index = 0U; index < runs.size(); ++index) {
        const Run& run = runs[index];
        const double extent = static_cast<double>(run.count) * run.height;
        const bool last = index + 1U == runs.size();
        if (remaining < extent || last) {
            std::size_t within = 0U;
            if (run.height > 0.0) {
                const double steps = std::floor(remaining / run.height);
                within = steps > 0.0 ? static_cast<std::size_t>(steps) : 0U;
            }
            within = std::min(within, run.count - 1U);
            const std::size_t found = std::min(row + within, total - 1U);
            return found;
        }
        remaining -= extent;
        row += run.count;
    }
    return total - 1U;
}

double RowHeightIndex::height_of(std::size_t row) const noexcept {
    if (row >= tree_.count()) return 0.0;
    const Tree::Place place = tree_.find(row);
    return (*place.leaf).entries[place.entry].height;
}

bool RowHeightIndex::measured(std::size_t row) const noexcept {
    if (row >= tree_.count()) return false;
    const Tree::Place place = tree_.find(row);
    return (*place.leaf).entries[place.entry].measured;
}

// -- run upkeep --------------------------------------------------------------------

// Joins runs[index] with an equal neighbour on either side (same height and
// state); empty runs go.
void RowHeightIndex::merge_neighbours(std::vector<Run>& runs, std::size_t index) {
    if (index >= runs.size()) return;
    if (index + 1U < runs.size() && runs[index + 1U].height == runs[index].height &&
        runs[index + 1U].measured == runs[index].measured) {
        runs[index].count += runs[index + 1U].count;
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(index + 1U));
    }
    if (index > 0U && runs[index - 1U].height == runs[index].height &&
        runs[index - 1U].measured == runs[index].measured) {
        runs[index - 1U].count += runs[index].count;
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

std::size_t RowHeightIndex::erase_from_leaf(std::vector<Run>& runs, std::size_t row, std::size_t count) {
    std::size_t start = 0U;
    std::size_t index = 0U;
    std::size_t left = count;
    while (index < runs.size() && left > 0U) {
        const std::size_t run_count = runs[index].count;
        const std::size_t run_end = start + run_count;
        if (run_end <= row) {
            start = run_end;
            ++index;
            continue;
        }
        // Where the erasure starts within this run, and how much of it goes.
        const std::size_t from = std::max(row, start) - start;
        const std::size_t taken = std::min(run_count - from, left);
        runs[index].count = run_count - taken;
        left -= taken;
        if (runs[index].count == 0U) {
            runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            start += runs[index].count;
            ++index;
        }
    }
    // The rows on either side of the gap may now be equal runs.
    for (std::size_t join = 0U; join + 1U < runs.size();) {
        const Run& left_run = runs[join];
        const Run& right_run = runs[join + 1U];
        if (left_run.height == right_run.height && left_run.measured == right_run.measured) {
            runs[join].count += right_run.count;
            runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(join + 1U));
        } else {
            ++join;
        }
    }
    const std::size_t erased = count - left;
    return erased;
}

// -- edits ------------------------------------------------------------------------------

bool RowHeightIndex::set_height(std::size_t row, double height) {
    if (row >= tree_.count() || !std::isfinite(height) || height < 0.0) return false;
    const Tree::Place place = tree_.find(row);
    std::vector<Run>& runs = (*place.leaf).entries;
    const std::size_t index = place.entry;
    const Run run = runs[index];
    if (run.measured && run.height == height) return false;
    // The run becomes up to three: the rows before, the measured row, the
    // rows after.
    const std::size_t before = place.within;
    const std::size_t after = run.count - place.within - 1U;
    const Run measured_run{1U, height, true};
    std::vector<Run> pieces{};
    pieces.reserve(3U);
    if (before > 0U) pieces.push_back(Run{before, run.height, run.measured});
    pieces.push_back(measured_run);
    if (after > 0U) pieces.push_back(Run{after, run.height, run.measured});
    const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index);
    runs.erase(runs.begin() + at);
    runs.insert(runs.begin() + at, pieces.begin(), pieces.end());
    const std::size_t measured_index = index + (before > 0U ? 1U : 0U);
    merge_neighbours(runs, measured_index);
    tree_.settle(place.leaf);
    ++revision_;
    return true;
}

void RowHeightIndex::insert(std::size_t row, std::size_t count) {
    if (count == 0U) return;
    const std::size_t at = std::min(row, tree_.count());
    const Tree::Place place = tree_.find_insertion(at);
    std::vector<Run>& runs = (*place.leaf).entries;
    const std::size_t index = place.entry;
    const Run fresh{count, estimate_, false};
    std::size_t placed = index;
    if (index < runs.size() && place.within > 0U) {
        // Inside a run: split it around the new rows.
        const Run run = runs[index];
        runs[index].count = place.within;
        const Run tail{run.count - place.within, run.height, run.measured};
        const std::ptrdiff_t after = static_cast<std::ptrdiff_t>(index + 1U);
        runs.insert(runs.begin() + after, tail);
        runs.insert(runs.begin() + after, fresh);
        placed = index + 1U;
    } else {
        runs.insert(runs.begin() + static_cast<std::ptrdiff_t>(index), fresh);
    }
    merge_neighbours(runs, placed);
    tree_.settle(place.leaf);
    ++revision_;
}

void RowHeightIndex::erase(std::size_t row, std::size_t count) {
    const std::size_t total = tree_.count();
    if (count == 0U || row >= total) return;
    const std::size_t taken = std::min(count, total - row);
    if (row == 0U && taken == total) {
        tree_.clear();
        ++revision_;
        return;
    }
    // Each pass descends from the root to the first row still to go and
    // either detaches a child that the rest of the range covers whole (its
    // subtree is not visited) or erases from one leaf. The rows left never
    // pass the rows after `row`, so each pass removes at least one row, and
    // the passes are bounded by the boundary nodes of the range.
    std::size_t left = taken;
    while (left > 0U) {
        Node* node = tree_.root();
        std::size_t remaining = row;
        bool detached = false;
        while (!(*node).leaf) {
            std::size_t child = 0U;
            while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
                remaining -= (*node).child_counts[child];
                ++child;
            }
            const std::size_t child_count = (*node).child_counts[child];
            if (remaining == 0U && child_count <= left) {
                left -= child_count;
                tree_.detach_child(node, child);
                detached = true;
                break;
            }
            node = (*node).children[child].get();
        }
        if (detached) continue;
        left -= erase_from_leaf((*node).entries, remaining, left);
        tree_.settle(node);
    }
    ++revision_;
}

// -- storage ----------------------------------------------------------------------------

std::size_t RowHeightIndex::node_count() const noexcept {
    return tree_.node_count();
}

std::size_t RowHeightIndex::run_count() const noexcept {
    return tree_.entry_total();
}

std::size_t RowHeightIndex::depth() const noexcept {
    return tree_.depth();
}

} // namespace gui_forms
