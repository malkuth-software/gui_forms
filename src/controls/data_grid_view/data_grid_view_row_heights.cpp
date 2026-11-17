#include "gui_forms/controls/data_grid_view_row_heights.hpp"

#include "../collections/row_height_index.hpp"

#include <cmath>
#include <memory>

namespace gui_forms {

// The first estimate is replaced by the first reset; 22 is the grid's
// default row height.
DataGridViewRowHeights::DataGridViewRowHeights() : index_(std::make_unique<RowHeightIndex>(22.0)), default_height_(22.0) {}

DataGridViewRowHeights::~DataGridViewRowHeights() {}

void DataGridViewRowHeights::reset(std::size_t count, double default_height) {
    // Every row estimated: one run, whatever the count.
    (*index_).assign(count, default_height);
    default_height_ = default_height;
}

void DataGridViewRowHeights::reset(std::size_t count, double default_height,
                                   std::span<const DataGridViewRowHeight> heights) {
    reset(count, default_height);
    if (heights.empty()) return;
    RowHeightIndex& index = *index_;
    const double estimate = index.estimated_height();
    // Runs of consecutive positions with heights other than the default are
    // staged and stored in one leaf pass; a default height, or a break in
    // the run, stores what is staged first. A later height for a position
    // wins because it is stored later.
    index.clear_staged();
    std::size_t run_first = 0U;
    std::size_t run_length = 0U;
    for (const DataGridViewRowHeight& given : heights) {
        const bool default_row = RowHeightIndex::stored_height(given.height) == estimate;
        const bool continues = run_length != 0U && given.position == run_first + run_length;
        if (run_length != 0U && (default_row || !continues)) {
            static_cast<void>(index.store_staged(run_first, run_length));
            run_length = 0U;
        }
        if (default_row) {
            index.forget(given.position, 1U);
            continue;
        }
        if (run_length == 0U) run_first = given.position;
        if (index.stage_height(given.height)) ++run_length;
    }
    if (run_length != 0U) static_cast<void>(index.store_staged(run_first, run_length));
    index.clear_staged();
    // Rows back at the default leave no measured row: one run again.
    if (index.measured_count() == 0U) index.assign(count, default_height);
}

void DataGridViewRowHeights::set_height(std::size_t position, double height) {
    store(position, height);
    // The last row with its own height gone: one run again.
    RowHeightIndex& index = *index_;
    if (index.measured_count() == 0U) index.assign(index.count(), default_height_);
}

void DataGridViewRowHeights::store(std::size_t position, double height) {
    RowHeightIndex& index = *index_;
    if (RowHeightIndex::stored_height(height) == index.estimated_height()) {
        if (index.measured(position)) index.forget(position, 1U);
        return;
    }
    static_cast<void>(index.set_height(position, height));
}

std::size_t DataGridViewRowHeights::count() const noexcept {
    return (*index_).count();
}

bool DataGridViewRowHeights::uniform() const noexcept {
    const bool result = (*index_).measured_count() == 0U;
    return result;
}

double DataGridViewRowHeights::height(std::size_t position) const noexcept {
    return (*index_).height_of(position);
}

double DataGridViewRowHeights::top(std::size_t position) const noexcept {
    return (*index_).offset_of(position);
}

double DataGridViewRowHeights::total() const noexcept {
    return (*index_).total_height();
}

std::size_t DataGridViewRowHeights::position_at(double offset) const noexcept {
    const RowHeightIndex& index = *index_;
    const std::size_t count = index.count();
    if (count == 0U || !(offset > 0.0)) return 0U;
    if (offset >= index.total_height()) return count;
    const std::size_t result = index.row_at(offset);
    return result;
}

double DataGridViewRowHeights::stored_height(double height) noexcept {
    return RowHeightIndex::stored_height(height);
}

std::size_t DataGridViewRowHeights::storage_bytes() const noexcept {
    return (*index_).storage_bytes();
}

} // namespace gui_forms
