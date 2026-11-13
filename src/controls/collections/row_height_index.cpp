#include "row_height_index.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

using stx_btree::HeightUnits;
using stx_btree::RowCount;
using stx_btree::RowIndex;
using stx_btree::RowState;

// The estimate in units: finite and > 0, else one logical unit; held
// within [1 unit, the height limit].
[[nodiscard]] std::int64_t estimate_units(double estimated_height) noexcept {
    if (!std::isfinite(estimated_height) || estimated_height <= 0.0) {
        const std::int64_t one = static_cast<std::int64_t>(RowHeightIndex::units_per_logical);
        return one;
    }
    const double held = std::min(estimated_height, RowHeightIndex::maximum_height);
    const std::int64_t units = std::llround(held * RowHeightIndex::units_per_logical);
    const std::int64_t at_least_one = std::max<std::int64_t>(units, 1);
    return at_least_one;
}

[[nodiscard]] double logical(std::int64_t units) noexcept {
    const double value = static_cast<double>(units) / RowHeightIndex::units_per_logical;
    return value;
}

[[nodiscard]] RowIndex row_index(std::size_t row) noexcept {
    const RowIndex index{static_cast<std::uint64_t>(row)};
    return index;
}

void require_within_limits(bool accepted) {
    if (!accepted) throw std::length_error("RowHeightIndex: past its row or height limits");
}

} // namespace

// -- cursor -------------------------------------------------------------------------

std::size_t RowHeightIndex::Cursor::row() const noexcept {
    const std::size_t position = static_cast<std::size_t>(cursor_.row().value);
    return position;
}

double RowHeightIndex::Cursor::top() const noexcept {
    return logical(cursor_.top().value);
}

double RowHeightIndex::Cursor::height() const noexcept {
    return logical(cursor_.height().value);
}

bool RowHeightIndex::Cursor::measured() const noexcept {
    const bool is_measured = !cursor_.at_end() && cursor_.state() == RowState::measured;
    return is_measured;
}

bool RowHeightIndex::Cursor::at_end() const noexcept {
    return cursor_.at_end();
}

// -- lifetime -----------------------------------------------------------------------

RowHeightIndex::RowHeightIndex(double estimated_height)
    : index_(std::make_unique<Index>(HeightUnits{estimate_units(estimated_height)})) {}

RowHeightIndex::~RowHeightIndex() {}

RowHeightIndex::RowHeightIndex(RowHeightIndex&& other) noexcept
    : index_(std::move(other.index_)), batch_(std::move(other.batch_)), revision_(other.revision_) {
    ++other.revision_;
}

RowHeightIndex& RowHeightIndex::operator=(RowHeightIndex&& other) noexcept {
    if (this == &other) return *this;
    index_ = std::move(other.index_);
    batch_ = std::move(other.batch_);
    revision_ = std::max(revision_, other.revision_) + 1U;
    ++other.revision_;
    return *this;
}

// -- conversions --------------------------------------------------------------------

std::int64_t RowHeightIndex::height_units(double height) noexcept {
    const double held = std::clamp(height, 0.0, maximum_height);
    const std::int64_t units = std::llround(held * units_per_logical);
    return units;
}

double RowHeightIndex::stored_height(double height) noexcept {
    if (!std::isfinite(height) || height < 0.0) return 0.0;
    return logical(height_units(height));
}

double RowHeightIndex::stored_estimate(double estimated_height) noexcept {
    return logical(estimate_units(estimated_height));
}

std::int64_t RowHeightIndex::offset_units(double offset) const noexcept {
    if (std::isnan(offset)) return 0;
    const std::int64_t total = (*index_).total().value;
    const double scaled = std::floor(offset * units_per_logical);
    if (scaled < 0.0) return -1;
    if (scaled >= static_cast<double>(total)) return total;
    // 0 <= scaled < total < 2^62 and scaled is integral: exact.
    const std::int64_t units = static_cast<std::int64_t>(scaled);
    return units;
}

// -- queries ------------------------------------------------------------------------

void RowHeightIndex::assign(std::size_t count, double estimated_height) {
    const bool accepted = (*index_).assign(static_cast<RowCount>(count), HeightUnits{estimate_units(estimated_height)});
    require_within_limits(accepted);
    ++revision_;
}

std::size_t RowHeightIndex::count() const noexcept {
    const std::size_t rows = static_cast<std::size_t>((*index_).count());
    return rows;
}

double RowHeightIndex::total_height() const noexcept {
    return logical((*index_).total().value);
}

double RowHeightIndex::estimated_height() const noexcept {
    return logical((*index_).estimate().value);
}

double RowHeightIndex::offset_of(std::size_t row) const noexcept {
    return logical((*index_).offset_of(row_index(row)).value);
}

std::size_t RowHeightIndex::row_at(double offset) const noexcept {
    const RowIndex row = (*index_).row_at(HeightUnits{offset_units(offset)});
    const std::size_t found = static_cast<std::size_t>(row.value);
    return found;
}

double RowHeightIndex::height_of(std::size_t row) const noexcept {
    const stx_btree::RowExtent extent = (*index_).extent_of(row_index(row));
    return logical(extent.height.value);
}

bool RowHeightIndex::measured(std::size_t row) const noexcept {
    if (row >= count()) return false;
    const stx_btree::RowExtent extent = (*index_).extent_of(row_index(row));
    const bool is_measured = extent.state == RowState::measured;
    return is_measured;
}

std::size_t RowHeightIndex::measured_count() const noexcept {
    const std::size_t rows = static_cast<std::size_t>((*index_).measured_count());
    return rows;
}

// -- edits --------------------------------------------------------------------------

bool RowHeightIndex::set_height(std::size_t row, double height) {
    if (!std::isfinite(height) || height < 0.0 || row >= count()) return false;
    const bool changed = (*index_).set_height(row_index(row), HeightUnits{height_units(height)});
    if (changed) ++revision_;
    return changed;
}

bool RowHeightIndex::set_heights(std::size_t row, std::span<const double> heights) {
    // Validates the whole batch before staging any of it.
    for (const double height : heights) {
        if (!std::isfinite(height) || height < 0.0) return false;
    }
    clear_staged();
    batch_.reserve(heights.size());
    for (const double height : heights) {
        batch_.push_back(HeightUnits{height_units(height)});
    }
    const bool stored = store_staged(row, heights.size());
    return stored;
}

void RowHeightIndex::clear_staged() noexcept {
    batch_.clear();
}

bool RowHeightIndex::stage_height(double height) {
    if (!std::isfinite(height) || height < 0.0) return false;
    batch_.push_back(HeightUnits{height_units(height)});
    return true;
}

std::size_t RowHeightIndex::staged_count() const noexcept {
    return batch_.size();
}

bool RowHeightIndex::store_staged(std::size_t row, std::size_t length) {
    const std::size_t rows = count();
    bool stored = false;
    if (length == 0U) {
        stored = true;
    } else if (length <= batch_.size() && row < rows && length <= rows - row) {
        const std::span<const HeightUnits> staged(batch_.data(), length);
        stored = (*index_).set_heights(row_index(row), staged);
        if (stored) ++revision_;
    }
    batch_.clear();
    return stored;
}

void RowHeightIndex::insert(std::size_t row, std::size_t count) {
    if (count == 0U) return;
    const bool accepted = (*index_).insert(row_index(row), static_cast<RowCount>(count));
    require_within_limits(accepted);
    ++revision_;
}

void RowHeightIndex::erase(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    (*index_).erase(row_index(row), static_cast<RowCount>(rows));
    ++revision_;
}

void RowHeightIndex::forget(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    const bool accepted = (*index_).forget(row_index(row), static_cast<RowCount>(rows));
    require_within_limits(accepted);
    ++revision_;
}

void RowHeightIndex::coarsen(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    (*index_).coarsen(row_index(row), static_cast<RowCount>(rows));
    ++revision_;
}

// -- cursors ------------------------------------------------------------------------

void RowHeightIndex::seek_row(Cursor& cursor, std::size_t row) const noexcept {
    (*index_).seek_row(cursor.cursor_, row_index(row));
}

void RowHeightIndex::seek_offset(Cursor& cursor, double offset) const noexcept {
    (*index_).seek_offset(cursor.cursor_, HeightUnits{offset_units(offset)});
}

void RowHeightIndex::seek_offset_from(Cursor& cursor, double offset) const noexcept {
    (*index_).seek_offset_from(cursor.cursor_, HeightUnits{offset_units(offset)});
}

void RowHeightIndex::advance(Cursor& cursor) const noexcept {
    (*index_).advance(cursor.cursor_);
}

// -- storage ------------------------------------------------------------------------

std::size_t RowHeightIndex::node_count() const noexcept {
    return (*index_).node_count();
}

std::size_t RowHeightIndex::run_count() const noexcept {
    return (*index_).run_count();
}

std::size_t RowHeightIndex::depth() const noexcept {
    return (*index_).depth();
}

std::size_t RowHeightIndex::storage_bytes() const noexcept {
    const std::size_t bytes = sizeof(Index) + (*index_).page_bytes();
    return bytes;
}

const char* RowHeightIndex::invariant_failure() const noexcept {
    const stx_btree::detail::InvariantReport report = (*index_).check_invariants();
    if (report.ok) return nullptr;
    return report.failure;
}

} // namespace gui_forms
