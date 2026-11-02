#include "gui_forms/controls/data_grid_view/data_grid_view_row_heights.hpp"

#include <cmath>

namespace gui_forms {

namespace {

std::size_t lowest_bit(std::size_t index) noexcept {
    const std::size_t result = index & (~index + 1U);
    return result;
}

std::size_t largest_power_of_two_not_above(std::size_t count) noexcept {
    std::size_t step = count == 0U ? 0U : 1U;
    while (step != 0U && step <= count / 2U) step *= 2U;
    return step;
}

} // namespace

void DataGridViewRowHeights::reset(std::size_t count, double default_height) {
    count_ = count;
    default_height_ = default_height;
    differences_.clear();
    differences_.shrink_to_fit();
    tree_.clear();
    tree_.shrink_to_fit();
    difference_total_ = 0.0;
    different_rows_ = 0U;
    top_step_ = largest_power_of_two_not_above(count);
}

void DataGridViewRowHeights::reset(std::size_t count, double default_height,
                                   std::span<const DataGridViewRowHeight> heights) {
    reset(count, default_height);
    if (heights.empty()) return;
    begin_differences();
    for (const DataGridViewRowHeight& given : heights) {
        differences_[given.position] = given.height - default_height_;
    }
    // The tree in one pass: each node adds its finished sum to its parent.
    double total = 0.0;
    std::size_t different = 0U;
    for (std::size_t index = 1U; index <= count_; ++index) {
        const double difference = differences_[index - 1U];
        if (difference != 0.0) ++different;
        total += difference;
        tree_[index - 1U] += difference;
        const std::size_t parent = index + lowest_bit(index);
        if (parent <= count_) tree_[parent - 1U] += tree_[index - 1U];
    }
    difference_total_ = total;
    different_rows_ = different;
    if (different_rows_ == 0U) reset(count_, default_height_);
}

void DataGridViewRowHeights::begin_differences() {
    differences_.assign(count_, 0.0);
    tree_.assign(count_, 0.0);
}

void DataGridViewRowHeights::set_height(std::size_t position, double height) {
    const double difference = height - default_height_;
    if (differences_.empty()) {
        if (difference == 0.0) return;
        begin_differences();
    }
    const double previous = differences_[position];
    if (previous == difference) return;
    differences_[position] = difference;
    if (previous == 0.0) ++different_rows_;
    if (difference == 0.0) --different_rows_;
    if (different_rows_ == 0U) {
        reset(count_, default_height_);
        return;
    }
    const double change = difference - previous;
    difference_total_ += change;
    for (std::size_t index = position + 1U; index <= count_; index += lowest_bit(index)) {
        tree_[index - 1U] += change;
    }
}

double DataGridViewRowHeights::height(std::size_t position) const noexcept {
    const double difference = differences_.empty() ? 0.0 : differences_[position];
    const double result = default_height_ + difference;
    return result;
}

double DataGridViewRowHeights::difference_before(std::size_t position) const noexcept {
    double sum = 0.0;
    if (differences_.empty()) return sum;
    for (std::size_t index = position; index > 0U; index -= lowest_bit(index)) sum += tree_[index - 1U];
    return sum;
}

double DataGridViewRowHeights::top(std::size_t position) const noexcept {
    const double rows = static_cast<double>(position) * default_height_;
    const double result = rows + difference_before(position);
    return result;
}

double DataGridViewRowHeights::total() const noexcept {
    const double result = static_cast<double>(count_) * default_height_ + difference_total_;
    return result;
}

std::size_t DataGridViewRowHeights::position_at(double offset) const noexcept {
    if (count_ == 0U || !(offset > 0.0)) return 0U;
    if (offset >= total()) return count_;
    if (differences_.empty()) {
        const double rows = std::floor(offset / default_height_);
        const std::size_t result = rows >= static_cast<double>(count_) ? count_ - 1U : static_cast<std::size_t>(rows);
        return result;
    }
    // Descend the tree: take each node whose rows end at or before the
    // offset. `position` rows are then taken, and the next row holds it.
    std::size_t position = 0U;
    double remaining = offset;
    for (std::size_t step = top_step_; step != 0U; step /= 2U) {
        const std::size_t next = position + step;
        if (next > count_) continue;
        const double span = static_cast<double>(step) * default_height_ + tree_[next - 1U];
        if (span <= remaining) {
            position = next;
            remaining -= span;
        }
    }
    const std::size_t result = position < count_ ? position : count_ - 1U;
    return result;
}

} // namespace gui_forms
