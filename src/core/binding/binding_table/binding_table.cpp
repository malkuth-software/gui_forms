// The built-in column-storage provider. See binding_table.hpp.

#include "gui_forms/binding/binding_table/binding_table.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace gui_forms {

BindingTable::BindingTable(BindingSchema schema) : schema_(std::move(schema)) {}

std::size_t BindingTable::add_column(BindingColumn column) {
    const std::size_t old_width = width();
    // Validated before anything moves; the new column is null in every row.
    BindingSchema grown = schema_;
    const std::size_t index = grown.append(std::move(column));
    std::vector<BindingValue> restrided;
    restrided.reserve(count() * (old_width + 1U));
    for (std::size_t row = 0U; row < count(); ++row) {
        for (std::size_t cell = 0U; cell < old_width; ++cell) {
            restrided.push_back(std::move(cells_[row * old_width + cell]));
        }
        restrided.emplace_back();
    }
    schema_ = std::move(grown);
    cells_ = std::move(restrided);
    link_.reset();
    return index;
}

std::size_t BindingTable::find_or_add_column(std::string_view name) {
    const std::optional<std::size_t> found = schema_.find(name);
    if (found) {
        return *found;
    }
    BindingColumn column;
    column.name = std::string(name);
    const std::size_t index = add_column(std::move(column));
    return index;
}

std::string BindingTable::refusal(const BindingRow& values) const {
    if (!valid_stable_id(values.stable_id)) {
        return "a row's stable ID must follow the StableId segment rule";
    }
    if (identities_.contains(values.stable_id)) {
        return "the stable ID " + values.stable_id + " is already in the table";
    }
    if (values.values.size() > width()) {
        return "the row has more values than the table has columns";
    }
    for (std::size_t column = 0U; column < values.values.size(); ++column) {
        if (!schema_.admits(column, values.values[column])) {
            return "the value for " + schema_.column(column).name + " is not of the column's kind";
        }
    }
    return {};
}

void BindingTable::place_row(std::size_t row, const BindingRow& values) {
    // Every container grows before any changes, so a failed allocation
    // leaves the table as it was.
    cells_.reserve(cells_.size() + width());
    stable_ids_.reserve(stable_ids_.size() + 1U);
    editable_.reserve(editable_.size() + 1U);
    std::vector<BindingValue> padded(width());
    for (std::size_t column = 0U; column < values.values.size(); ++column) {
        padded[column] = values.values[column];
    }
    identities_.insert(values.stable_id);
    const std::ptrdiff_t first_cell = static_cast<std::ptrdiff_t>(row * width());
    cells_.insert(cells_.begin() + first_cell, std::make_move_iterator(padded.begin()),
                  std::make_move_iterator(padded.end()));
    stable_ids_.insert(stable_ids_.begin() + static_cast<std::ptrdiff_t>(row), values.stable_id);
    editable_.insert(editable_.begin() + static_cast<std::ptrdiff_t>(row), values.editable ? 1U : 0U);
}

void BindingTable::erase_rows(std::size_t row, std::size_t count) {
    for (std::size_t index = row; index < row + count; ++index) {
        identities_.erase(stable_ids_[index]);
    }
    const std::ptrdiff_t first_cell = static_cast<std::ptrdiff_t>(row * width());
    const std::ptrdiff_t end_cell = static_cast<std::ptrdiff_t>((row + count) * width());
    cells_.erase(cells_.begin() + first_cell, cells_.begin() + end_cell);
    stable_ids_.erase(stable_ids_.begin() + static_cast<std::ptrdiff_t>(row),
                      stable_ids_.begin() + static_cast<std::ptrdiff_t>(row + count));
    editable_.erase(editable_.begin() + static_cast<std::ptrdiff_t>(row),
                    editable_.begin() + static_cast<std::ptrdiff_t>(row + count));
}

std::size_t BindingTable::append(BindingRow row) {
    const std::size_t index = insert(count(), std::move(row));
    return index;
}

std::size_t BindingTable::insert(std::size_t row, BindingRow values) {
    if (row > count()) {
        throw std::out_of_range("GUI.Forms binding table insertion is past the rows");
    }
    const std::string refused = refusal(values);
    if (!refused.empty()) {
        throw std::invalid_argument("GUI.Forms binding table: " + refused);
    }
    place_row(row, values);
    link_.inserted(row, 1U);
    return row;
}

std::size_t BindingTable::remove(std::size_t row, std::size_t count_requested) {
    if (row >= count()) {
        return 0U;
    }
    const std::size_t removed = std::min(count_requested, count() - row);
    if (removed == 0U) {
        return 0U;
    }
    erase_rows(row, removed);
    link_.removed(row, removed);
    return removed;
}

void BindingTable::move(std::size_t row, std::size_t count_requested, std::size_t target) {
    const std::size_t rows = count();
    if (count_requested == 0U || row > rows || count_requested > rows - row || target > rows - count_requested) {
        throw std::out_of_range("GUI.Forms binding table move is past the rows");
    }
    if (target == row) {
        return;
    }
    // A rotation of the range the block crosses, in every per-row array;
    // nothing allocates, so nothing fails part way.
    const std::size_t low = std::min(row, target);
    const std::size_t high = std::max(row, target) + count_requested;
    const std::size_t pivot = target < row ? row : row + count_requested;
    const std::ptrdiff_t cell_low = static_cast<std::ptrdiff_t>(low * width());
    const std::ptrdiff_t cell_pivot = static_cast<std::ptrdiff_t>(pivot * width());
    const std::ptrdiff_t cell_high = static_cast<std::ptrdiff_t>(high * width());
    std::rotate(cells_.begin() + cell_low, cells_.begin() + cell_pivot, cells_.begin() + cell_high);
    std::rotate(stable_ids_.begin() + static_cast<std::ptrdiff_t>(low),
                stable_ids_.begin() + static_cast<std::ptrdiff_t>(pivot),
                stable_ids_.begin() + static_cast<std::ptrdiff_t>(high));
    std::rotate(editable_.begin() + static_cast<std::ptrdiff_t>(low),
                editable_.begin() + static_cast<std::ptrdiff_t>(pivot),
                editable_.begin() + static_cast<std::ptrdiff_t>(high));
    link_.moved(row, count_requested, target);
}

void BindingTable::clear() {
    cells_.clear();
    stable_ids_.clear();
    editable_.clear();
    identities_.clear();
    errors_.clear();
    link_.reset();
}

void BindingTable::assign(std::vector<BindingRow> rows) {
    // Checked whole first: refusal sees the identities of the rows before
    // it in the batch through a scratch table.
    BindingTable scratch(schema_);
    for (const BindingRow& row : rows) {
        const std::string refused = scratch.refusal(row);
        if (!refused.empty()) {
            throw std::invalid_argument("GUI.Forms binding table: " + refused);
        }
        scratch.place_row(scratch.count(), row);
    }
    cells_ = std::move(scratch.cells_);
    stable_ids_ = std::move(scratch.stable_ids_);
    editable_ = std::move(scratch.editable_);
    identities_ = std::move(scratch.identities_);
    errors_.clear();
    link_.reset();
}

const BindingValue& BindingTable::value(std::size_t row, std::size_t column) const {
    if (row >= count() || column >= width()) {
        throw std::out_of_range("GUI.Forms binding table cell is past the rows or columns");
    }
    return cells_[row * width() + column];
}

bool BindingTable::set_value(std::size_t row, std::size_t column, BindingValue value) {
    if (row >= count() || column >= width()) {
        throw std::out_of_range("GUI.Forms binding table cell is past the rows or columns");
    }
    if (!schema_.admits(column, value)) {
        throw std::invalid_argument("GUI.Forms binding table value is not of the column's kind");
    }
    BindingValue& cell = cells_[row * width() + column];
    if (cell == value) {
        return false;
    }
    cell = std::move(value);
    link_.changed(row, column);
    return true;
}

void BindingTable::set_editable(std::size_t row, bool editable) {
    unsigned char& flag = editable_.at(row);
    const unsigned char wanted = editable ? 1U : 0U;
    if (flag == wanted) {
        return;
    }
    flag = wanted;
    link_.changed(row, binding_whole_row);
}

std::optional<std::size_t> BindingTable::find(std::string_view stable_id) const {
    for (std::size_t row = 0U; row < count(); ++row) {
        if (stable_ids_[row] == stable_id) {
            return row;
        }
    }
    return std::nullopt;
}

void BindingTable::set_error(std::size_t row, std::size_t column, std::string text) {
    if (row >= count() || (column != binding_whole_row && column >= width())) {
        throw std::out_of_range("GUI.Forms binding table cell is past the rows or columns");
    }
    const std::string& identity = stable_ids_[row];
    std::vector<CellError>::iterator found = errors_.begin();
    while (found != errors_.end() && ((*found).stable_id != identity || (*found).column != column)) {
        ++found;
    }
    if (text.empty()) {
        if (found == errors_.end()) {
            return;
        }
        errors_.erase(found);
    } else if (found != errors_.end()) {
        if ((*found).text == text) {
            return;
        }
        (*found).text = std::move(text);
    } else {
        errors_.push_back(CellError{identity, column, std::move(text)});
    }
    link_.changed(row, column);
}

std::string BindingTable::error(std::size_t row, std::size_t column) const {
    if (row >= count()) {
        return {};
    }
    for (const CellError& entry : errors_) {
        if (entry.stable_id == stable_ids_[row] && entry.column == column) {
            return entry.text;
        }
    }
    return {};
}

BindingSchema BindingTable::schema() const {
    return schema_;
}

BindingProviderCapabilities BindingTable::capabilities() const {
    BindingProviderCapabilities capabilities;
    capabilities.inserts = true;
    capabilities.removes = true;
    return capabilities;
}

std::size_t BindingTable::row_count() {
    return count();
}

bool BindingTable::read_cell(std::size_t row, std::size_t column, BindingValue& value) {
    if (row >= count() || column >= width()) {
        return false;
    }
    value = cells_[row * width() + column];
    return true;
}

bool BindingTable::read_stable_id(std::size_t row, std::string& stable_id) {
    if (row >= count()) {
        return false;
    }
    stable_id = stable_ids_[row];
    return true;
}

BindingWriteResult BindingTable::write_cell(std::size_t row, std::size_t column, const BindingValue& value) {
    if (row >= count() || column >= width()) {
        return BindingWriteResult{BindingWriteStatus::out_of_range, "the cell is past the table"};
    }
    if (!schema_.admits(column, value)) {
        return BindingWriteResult{BindingWriteStatus::kind_mismatch, "the value is not of the column's kind"};
    }
    if (editable_[row] == 0U || schema_.column(column).read_only) {
        return BindingWriteResult{BindingWriteStatus::read_only, "the cell is read-only"};
    }
    cells_[row * width() + column] = value;
    return BindingWriteResult{BindingWriteStatus::accepted, {}};
}

bool BindingTable::cell_read_only(std::size_t row, std::size_t column) {
    static_cast<void>(column);
    const bool read_only = row < count() && editable_[row] == 0U;
    return read_only;
}

bool BindingTable::read_error(std::size_t row, std::size_t column, std::string& text) {
    text = error(row, column);
    return !text.empty();
}

BindingWriteResult BindingTable::insert_row(std::size_t row, const BindingRow& values) {
    if (row > count()) {
        return BindingWriteResult{BindingWriteStatus::out_of_range, "the insertion is past the rows"};
    }
    std::string refused = refusal(values);
    if (!refused.empty()) {
        return BindingWriteResult{BindingWriteStatus::refused, std::move(refused)};
    }
    place_row(row, values);
    return BindingWriteResult{BindingWriteStatus::accepted, {}};
}

BindingWriteResult BindingTable::remove_rows(std::size_t row, std::size_t count_requested) {
    if (row > count() || count_requested > count() - row) {
        return BindingWriteResult{BindingWriteStatus::out_of_range, "the removal is past the rows"};
    }
    erase_rows(row, count_requested);
    return BindingWriteResult{BindingWriteStatus::accepted, {}};
}

void BindingTable::attach(const BindingProviderLink& link) {
    link_ = link;
}

void BindingTable::detach() noexcept {
    link_ = BindingProviderLink{};
}

} // namespace gui_forms
