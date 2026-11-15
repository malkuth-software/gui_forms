#include "row_height_index.hpp"

#include "../../platform/stxlib_bridge.hpp"

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The estimate in units: finite and > 0, else one logical unit; held
// within [1 unit, the height limit].
[[nodiscard]] stxlib::i64 estimate_units(double estimated_height) noexcept {
    if (!std::isfinite(estimated_height) || estimated_height <= 0.0) {
        const stxlib::i64 one = static_cast<stxlib::i64>(RowHeightIndex::units_per_logical);
        return one;
    }
    const double held = std::min(estimated_height, RowHeightIndex::maximum_height);
    const stxlib::i64 units = std::llround(held * RowHeightIndex::units_per_logical);
    const stxlib::i64 at_least_one = std::max<stxlib::i64>(units, 1);
    return at_least_one;
}

[[nodiscard]] double logical(stxlib::i64 units) noexcept {
    const double value = static_cast<double>(units) / RowHeightIndex::units_per_logical;
    return value;
}

[[nodiscard]] stxlib::u64 row_index(std::size_t row) noexcept {
    const stxlib::u64 index = static_cast<stxlib::u64>(row);
    return index;
}

// An edit STXlib refused, as the adapter's exceptions: out_of_memory as
// std::bad_alloc, limit_exceeded as std::length_error. invalid_argument
// cannot follow the adapter's own checks; were it to, it is reported as
// std::invalid_argument. The index is unchanged in every case.
[[noreturn]] void throw_refused_edit(const stxlib::ErrorRecord& record) {
    switch (record.kind()) {
    case stxlib::ErrorKind::out_of_memory:
        throw std::bad_alloc();
    case stxlib::ErrorKind::limit_exceeded:
        throw std::length_error("RowHeightIndex: past its row or height limits");
    case stxlib::ErrorKind::invalid_argument:
    default:
        throw std::invalid_argument("RowHeightIndex: an edit the index refused");
    }
}

void require_edit(const stxlib::Status& status) {
    if (status.failed()) throw_refused_edit(*status.error());
}

// A refusal a set_height or set_heights reports as false (a limit, or an
// argument the adapter let through); an allocation failure still throws.
[[nodiscard]] bool refused_or_throw(const stxlib::ErrorRecord& record) {
    if (record.kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
    return false;
}

// A new index estimating rows at `estimated_height`; STXlib's only failure
// here is out_of_memory, thrown as std::bad_alloc.
[[nodiscard]] stxlib::RowHeightIndex create_index(double estimated_height) {
    stxlib::Result<stxlib::RowHeightIndex> created =
        stxlib::RowHeightIndex::create(estimate_units(estimated_height));
    if (created.failed()) throw std::bad_alloc();
    stxlib::RowHeightIndex index = std::move(created).take_value();
    return index;
}

} // namespace

// -- cursor -------------------------------------------------------------------------

std::size_t RowHeightIndex::Cursor::row() const noexcept {
    const std::size_t position = static_cast<std::size_t>(cursor_.row());
    return position;
}

double RowHeightIndex::Cursor::top() const noexcept {
    return logical(cursor_.top());
}

double RowHeightIndex::Cursor::height() const noexcept {
    return logical(cursor_.height());
}

bool RowHeightIndex::Cursor::measured() const noexcept {
    const bool is_measured = !cursor_.at_end() && cursor_.state() == stxlib::RowState::measured;
    return is_measured;
}

bool RowHeightIndex::Cursor::at_end() const noexcept {
    return cursor_.at_end();
}

// -- lifetime -----------------------------------------------------------------------

RowHeightIndex::RowHeightIndex(double estimated_height) : index_(create_index(estimated_height)) {}

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

stxlib::i64 RowHeightIndex::height_units(double height) noexcept {
    const double held = std::clamp(height, 0.0, maximum_height);
    const stxlib::i64 units = std::llround(held * units_per_logical);
    return units;
}

double RowHeightIndex::stored_height(double height) noexcept {
    if (!std::isfinite(height) || height < 0.0) return 0.0;
    return logical(height_units(height));
}

double RowHeightIndex::stored_estimate(double estimated_height) noexcept {
    return logical(estimate_units(estimated_height));
}

stxlib::i64 RowHeightIndex::offset_units(double offset) const noexcept {
    if (std::isnan(offset)) return 0;
    const stxlib::i64 total = index_.total();
    const double scaled = std::floor(offset * units_per_logical);
    if (scaled < 0.0) return -1;
    if (scaled >= static_cast<double>(total)) return total;
    // 0 <= scaled < total < 2^62 and scaled is integral: exact.
    const stxlib::i64 units = static_cast<stxlib::i64>(scaled);
    return units;
}

// -- queries ------------------------------------------------------------------------

void RowHeightIndex::assign(std::size_t count, double estimated_height) {
    const stxlib::Status assigned = index_.assign(row_index(count), estimate_units(estimated_height));
    require_edit(assigned);
    ++revision_;
}

std::size_t RowHeightIndex::count() const noexcept {
    const std::size_t rows = static_cast<std::size_t>(index_.count());
    return rows;
}

double RowHeightIndex::total_height() const noexcept {
    return logical(index_.total());
}

double RowHeightIndex::estimated_height() const noexcept {
    return logical(index_.estimate());
}

double RowHeightIndex::offset_of(std::size_t row) const noexcept {
    return logical(index_.offset_of(row_index(row)));
}

std::size_t RowHeightIndex::row_at(double offset) const noexcept {
    const stxlib::u64 row = index_.row_at(offset_units(offset));
    const std::size_t found = static_cast<std::size_t>(row);
    return found;
}

double RowHeightIndex::height_of(std::size_t row) const noexcept {
    const stxlib::RowExtent extent = index_.extent_of(row_index(row));
    return logical(extent.height);
}

bool RowHeightIndex::measured(std::size_t row) const noexcept {
    if (row >= count()) return false;
    const stxlib::RowExtent extent = index_.extent_of(row_index(row));
    const bool is_measured = extent.state == stxlib::RowState::measured;
    return is_measured;
}

std::size_t RowHeightIndex::measured_count() const noexcept {
    const std::size_t rows = static_cast<std::size_t>(index_.measured_count());
    return rows;
}

// -- edits --------------------------------------------------------------------------

bool RowHeightIndex::set_height(std::size_t row, double height) {
    if (!std::isfinite(height) || height < 0.0 || row >= count()) return false;
    const stxlib::Result<bool> set = index_.set_height(row_index(row), height_units(height));
    if (set.failed()) return refused_or_throw(*set.error());
    const bool changed = *set.value();
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
        batch_.push_back(height_units(height));
    }
    const bool stored = store_staged(row, heights.size());
    return stored;
}

void RowHeightIndex::clear_staged() noexcept {
    batch_.clear();
}

bool RowHeightIndex::stage_height(double height) {
    if (!std::isfinite(height) || height < 0.0) return false;
    batch_.push_back(height_units(height));
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
        const std::span<const stxlib::i64> stage(batch_.data(), length);
        const stxlib::View<stxlib::i64> staged = platform::stxlib_view(stage);
        const stxlib::Status written = index_.set_heights(row_index(row), staged);
        if (written.failed()) {
            // The stage is cleared whether it stored or not, even when the
            // refusal throws.
            batch_.clear();
            stored = refused_or_throw(*written.error());
        } else {
            stored = true;
            ++revision_;
        }
    }
    batch_.clear();
    return stored;
}

void RowHeightIndex::insert(std::size_t row, std::size_t count) {
    if (count == 0U) return;
    const stxlib::Status inserted = index_.insert(row_index(row), row_index(count));
    require_edit(inserted);
    ++revision_;
}

void RowHeightIndex::erase(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    const stxlib::Status erased = index_.erase(row_index(row), row_index(rows));
    require_edit(erased);
    ++revision_;
}

void RowHeightIndex::forget(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    const stxlib::Status forgotten = index_.forget(row_index(row), row_index(rows));
    require_edit(forgotten);
    ++revision_;
}

void RowHeightIndex::coarsen(std::size_t row, std::size_t rows) {
    if (rows == 0U || row >= count()) return;
    const stxlib::Status coarsened = index_.coarsen(row_index(row), row_index(rows));
    require_edit(coarsened);
    ++revision_;
}

// -- cursors ------------------------------------------------------------------------

void RowHeightIndex::seek_row(Cursor& cursor, std::size_t row) const noexcept {
    index_.seek_row(cursor.cursor_, row_index(row));
}

void RowHeightIndex::seek_offset(Cursor& cursor, double offset) const noexcept {
    index_.seek_offset(cursor.cursor_, offset_units(offset));
}

void RowHeightIndex::seek_offset_from(Cursor& cursor, double offset) const noexcept {
    index_.seek_offset_from(cursor.cursor_, offset_units(offset));
}

void RowHeightIndex::advance(Cursor& cursor) const noexcept {
    index_.advance(cursor.cursor_);
}

// -- storage ------------------------------------------------------------------------

std::size_t RowHeightIndex::node_count() const noexcept {
    return index_.node_count();
}

std::size_t RowHeightIndex::run_count() const noexcept {
    return index_.run_count();
}

std::size_t RowHeightIndex::depth() const noexcept {
    return index_.depth();
}

std::size_t RowHeightIndex::storage_bytes() const noexcept {
    return index_.storage_bytes();
}

const char* RowHeightIndex::invariant_failure() const noexcept {
    const stxlib::TextView failure = index_.invariant_failure();
    if (failure.empty()) return nullptr;
    // STXlib's text has static storage and a NUL after it.
    return failure.data();
}

} // namespace gui_forms
