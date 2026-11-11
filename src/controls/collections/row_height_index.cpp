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

RowHeightIndex::RowHeightIndex(double estimated_height)
    : root_(std::make_unique<Node>()), estimate_(usable_estimate(estimated_height)) {}

RowHeightIndex::~RowHeightIndex() {}

RowHeightIndex::RowHeightIndex(RowHeightIndex&& other) noexcept
    : root_(std::move(other.root_)), estimate_(other.estimate_), revision_(other.revision_) {
    other.root_ = std::make_unique<Node>();
    ++other.revision_;
}

RowHeightIndex& RowHeightIndex::operator=(RowHeightIndex&& other) noexcept {
    if (this == &other) return *this;
    root_ = std::move(other.root_);
    estimate_ = other.estimate_;
    revision_ = std::max(revision_, other.revision_) + 1U;
    other.root_ = std::make_unique<Node>();
    ++other.revision_;
    return *this;
}

void RowHeightIndex::assign(std::size_t count, double estimated_height) {
    estimate_ = usable_estimate(estimated_height);
    std::unique_ptr<Node> fresh = std::make_unique<Node>();
    if (count > 0U) {
        const Run run{count, estimate_, false};
        (*fresh).runs.push_back(run);
    }
    recompute(*fresh);
    root_ = std::move(fresh);
    ++revision_;
}

std::size_t RowHeightIndex::count() const noexcept {
    return (*root_).count;
}

double RowHeightIndex::total_height() const noexcept {
    return (*root_).height;
}

// -- queries (iterative descents) --------------------------------------------

double RowHeightIndex::offset_of(std::size_t row) const noexcept {
    if (row >= (*root_).count) return (*root_).height;
    double offset = 0.0;
    const Node* node = root_.get();
    std::size_t remaining = row;
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
            remaining -= (*node).child_counts[child];
            offset += (*node).child_heights[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    for (const Run& run : (*node).runs) {
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
    const std::size_t total = (*root_).count;
    if (total == 0U) return 0U;
    if (!(offset > 0.0)) return 0U;
    if (offset >= (*root_).height) return total - 1U;
    std::size_t row = 0U;
    double remaining = offset;
    const Node* node = root_.get();
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_heights[child]) {
            remaining -= (*node).child_heights[child];
            row += (*node).child_counts[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    const std::vector<Run>& runs = (*node).runs;
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
    if (row >= (*root_).count) return 0.0;
    const Node* node = root_.get();
    std::size_t remaining = row;
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
            remaining -= (*node).child_counts[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    for (const Run& run : (*node).runs) {
        if (remaining < run.count) return run.height;
        remaining -= run.count;
    }
    return 0.0;
}

bool RowHeightIndex::measured(std::size_t row) const noexcept {
    if (row >= (*root_).count) return false;
    const Node* node = root_.get();
    std::size_t remaining = row;
    while (!(*node).leaf) {
        std::size_t child = 0U;
        while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
            remaining -= (*node).child_counts[child];
            ++child;
        }
        node = (*node).children[child].get();
    }
    for (const Run& run : (*node).runs) {
        if (remaining < run.count) return run.measured;
        remaining -= run.count;
    }
    return false;
}

// -- node upkeep -------------------------------------------------------------------

void RowHeightIndex::recompute(Node& node) noexcept {
    std::size_t count = 0U;
    double height = 0.0;
    if (node.leaf) {
        for (const Run& run : node.runs) {
            count += run.count;
            height += static_cast<double>(run.count) * run.height;
        }
    } else {
        for (std::size_t index = 0U; index < node.children.size(); ++index) {
            count += node.child_counts[index];
            height += node.child_heights[index];
        }
    }
    node.count = count;
    node.height = height;
}

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

std::unique_ptr<RowHeightIndex::Node> RowHeightIndex::split(Node& node) {
    std::unique_ptr<Node> right = std::make_unique<Node>();
    (*right).leaf = node.leaf;
    if (node.leaf) {
        const std::size_t half = node.runs.size() / 2U;
        const std::vector<Run>::iterator middle = node.runs.begin() + static_cast<std::ptrdiff_t>(half);
        (*right).runs.assign(middle, node.runs.end());
        node.runs.erase(middle, node.runs.end());
    } else {
        const std::size_t half = node.children.size() / 2U;
        const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(half);
        for (std::size_t index = half; index < node.children.size(); ++index) {
            (*right).children.push_back(std::move(node.children[index]));
        }
        (*right).child_counts.assign(node.child_counts.begin() + at, node.child_counts.end());
        (*right).child_heights.assign(node.child_heights.begin() + at, node.child_heights.end());
        node.children.erase(node.children.begin() + at, node.children.end());
        node.child_counts.erase(node.child_counts.begin() + at, node.child_counts.end());
        node.child_heights.erase(node.child_heights.begin() + at, node.child_heights.end());
    }
    recompute(node);
    recompute(*right);
    return right;
}

void RowHeightIndex::adopt_split(Node& branch, std::size_t index, std::unique_ptr<Node> sibling) {
    const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index + 1U);
    const std::size_t sibling_count = (*sibling).count;
    const double sibling_height = (*sibling).height;
    branch.children.insert(branch.children.begin() + at, std::move(sibling));
    branch.child_counts.insert(branch.child_counts.begin() + at, sibling_count);
    branch.child_heights.insert(branch.child_heights.begin() + at, sibling_height);
}

void RowHeightIndex::grow_root(std::unique_ptr<Node> sibling) {
    std::unique_ptr<Node> branch = std::make_unique<Node>();
    (*branch).leaf = false;
    const std::size_t left_count = (*root_).count;
    const double left_height = (*root_).height;
    (*branch).children.push_back(std::move(root_));
    (*branch).child_counts.push_back(left_count);
    (*branch).child_heights.push_back(left_height);
    adopt_split(*branch, 0U, std::move(sibling));
    recompute(*branch);
    root_ = std::move(branch);
}

void RowHeightIndex::shrink_root() {
    while (!(*root_).leaf && (*root_).children.size() == 1U) {
        std::unique_ptr<Node> only = std::move((*root_).children.front());
        root_ = std::move(only);
    }
    if (!(*root_).leaf && (*root_).children.empty()) {
        root_ = std::make_unique<Node>();
    }
}

// -- edits ------------------------------------------------------------------------------

std::unique_ptr<RowHeightIndex::Node> RowHeightIndex::set_height_in(Node& node, std::size_t row, double height,
                                                                    bool& changed) {
    if (!node.leaf) {
        std::size_t child = 0U;
        std::size_t remaining = row;
        while (child + 1U < node.children.size() && remaining >= node.child_counts[child]) {
            remaining -= node.child_counts[child];
            ++child;
        }
        Node& below = *node.children[child];
        std::unique_ptr<Node> sibling = set_height_in(below, remaining, height, changed);
        node.child_counts[child] = below.count;
        node.child_heights[child] = below.height;
        if (sibling) adopt_split(node, child, std::move(sibling));
        recompute(node);
        if (node.children.size() > node_capacity) return split(node);
        return {};
    }
    std::size_t index = 0U;
    std::size_t remaining = row;
    while (index < node.runs.size() && remaining >= node.runs[index].count) {
        remaining -= node.runs[index].count;
        ++index;
    }
    if (index >= node.runs.size()) return {};
    const Run run = node.runs[index];
    if (run.measured && run.height == height) return {};
    changed = true;
    // The run becomes up to three: the rows before, the measured row, the
    // rows after.
    const std::size_t before = remaining;
    const std::size_t after = run.count - remaining - 1U;
    const Run measured_run{1U, height, true};
    std::vector<Run> pieces{};
    pieces.reserve(3U);
    if (before > 0U) pieces.push_back(Run{before, run.height, run.measured});
    pieces.push_back(measured_run);
    if (after > 0U) pieces.push_back(Run{after, run.height, run.measured});
    const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index);
    node.runs.erase(node.runs.begin() + at);
    node.runs.insert(node.runs.begin() + at, pieces.begin(), pieces.end());
    const std::size_t measured_index = index + (before > 0U ? 1U : 0U);
    merge_neighbours(node.runs, measured_index);
    recompute(node);
    if (node.runs.size() > node_capacity) return split(node);
    return {};
}

bool RowHeightIndex::set_height(std::size_t row, double height) {
    if (row >= (*root_).count || !std::isfinite(height) || height < 0.0) return false;
    bool changed = false;
    std::unique_ptr<Node> sibling = set_height_in(*root_, row, height, changed);
    if (sibling) grow_root(std::move(sibling));
    if (changed) ++revision_;
    return changed;
}

std::unique_ptr<RowHeightIndex::Node> RowHeightIndex::insert_in(Node& node, std::size_t row, std::size_t count) {
    if (!node.leaf) {
        std::size_t child = 0U;
        std::size_t remaining = row;
        // An insertion at a boundary goes to the end of the child before it.
        while (child + 1U < node.children.size() && remaining > node.child_counts[child]) {
            remaining -= node.child_counts[child];
            ++child;
        }
        Node& below = *node.children[child];
        std::unique_ptr<Node> sibling = insert_in(below, remaining, count);
        node.child_counts[child] = below.count;
        node.child_heights[child] = below.height;
        if (sibling) adopt_split(node, child, std::move(sibling));
        recompute(node);
        if (node.children.size() > node_capacity) return split(node);
        return {};
    }
    std::size_t index = 0U;
    std::size_t remaining = row;
    while (index < node.runs.size() && remaining >= node.runs[index].count) {
        remaining -= node.runs[index].count;
        ++index;
    }
    const Run fresh{count, estimate_, false};
    std::size_t placed = index;
    if (index < node.runs.size() && remaining > 0U) {
        // Inside a run: split it around the new rows.
        const Run run = node.runs[index];
        node.runs[index].count = remaining;
        const Run tail{run.count - remaining, run.height, run.measured};
        const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index + 1U);
        node.runs.insert(node.runs.begin() + at, tail);
        node.runs.insert(node.runs.begin() + at, fresh);
        placed = index + 1U;
    } else {
        node.runs.insert(node.runs.begin() + static_cast<std::ptrdiff_t>(index), fresh);
    }
    merge_neighbours(node.runs, placed);
    recompute(node);
    if (node.runs.size() > node_capacity) return split(node);
    return {};
}

void RowHeightIndex::insert(std::size_t row, std::size_t count) {
    if (count == 0U) return;
    const std::size_t at = std::min(row, (*root_).count);
    std::unique_ptr<Node> sibling = insert_in(*root_, at, count);
    if (sibling) grow_root(std::move(sibling));
    ++revision_;
}

void RowHeightIndex::erase_in(Node& node, std::size_t row, std::size_t count) {
    if (node.leaf) {
        std::size_t start = 0U;
        std::size_t index = 0U;
        std::size_t left = count;
        while (index < node.runs.size() && left > 0U) {
            const std::size_t run_count = node.runs[index].count;
            const std::size_t run_end = start + run_count;
            if (run_end <= row) {
                start = run_end;
                ++index;
                continue;
            }
            // Where the erasure starts within this run, and how much of it goes.
            const std::size_t from = std::max(row, start) - start;
            const std::size_t taken = std::min(run_count - from, left);
            node.runs[index].count = run_count - taken;
            left -= taken;
            if (node.runs[index].count == 0U) {
                node.runs.erase(node.runs.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                start += node.runs[index].count;
                ++index;
            }
        }
        // The rows on either side of the gap may now be equal runs.
        for (std::size_t join = 0U; join + 1U < node.runs.size();) {
            const Run& left_run = node.runs[join];
            const Run& right_run = node.runs[join + 1U];
            if (left_run.height == right_run.height && left_run.measured == right_run.measured) {
                node.runs[join].count += right_run.count;
                node.runs.erase(node.runs.begin() + static_cast<std::ptrdiff_t>(join + 1U));
            } else {
                ++join;
            }
        }
        recompute(node);
        return;
    }
    std::size_t start = 0U;
    std::size_t left = count;
    std::size_t index = 0U;
    while (index < node.children.size() && left > 0U) {
        const std::size_t child_count = node.child_counts[index];
        const std::size_t child_end = start + child_count;
        if (child_end <= row) {
            start = child_end;
            ++index;
            continue;
        }
        const std::size_t from = std::max(row, start) - start;
        const std::size_t taken = std::min(child_count - from, left);
        Node& below = *node.children[index];
        if (from == 0U && taken == child_count) {
            // The whole child goes, without visiting its subtree.
            const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index);
            node.children.erase(node.children.begin() + at);
            node.child_counts.erase(node.child_counts.begin() + at);
            node.child_heights.erase(node.child_heights.begin() + at);
            left -= taken;
            continue;
        }
        erase_in(below, from, taken);
        node.child_counts[index] = below.count;
        node.child_heights[index] = below.height;
        left -= taken;
        start += below.count;
        ++index;
    }
    recompute(node);
}

void RowHeightIndex::erase(std::size_t row, std::size_t count) {
    const std::size_t total = (*root_).count;
    if (count == 0U || row >= total) return;
    const std::size_t taken = std::min(count, total - row);
    erase_in(*root_, row, taken);
    shrink_root();
    ++revision_;
}

// -- storage ----------------------------------------------------------------------------

std::size_t RowHeightIndex::count_nodes(const Node& node) noexcept {
    std::size_t nodes = 1U;
    for (const std::unique_ptr<Node>& child : node.children) nodes += count_nodes(*child);
    return nodes;
}

std::size_t RowHeightIndex::count_runs(const Node& node) noexcept {
    std::size_t runs = node.runs.size();
    for (const std::unique_ptr<Node>& child : node.children) runs += count_runs(*child);
    return runs;
}

std::size_t RowHeightIndex::node_count() const noexcept {
    return count_nodes(*root_);
}

std::size_t RowHeightIndex::run_count() const noexcept {
    return count_runs(*root_);
}

std::size_t RowHeightIndex::depth() const noexcept {
    std::size_t levels = 1U;
    const Node* node = root_.get();
    while (!(*node).leaf && !(*node).children.empty()) {
        node = (*node).children.front().get();
        ++levels;
    }
    return levels;
}

} // namespace gui_forms
