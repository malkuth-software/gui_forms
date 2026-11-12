#pragma once

#include "gui_forms/binding/binding_provider/binding_provider.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gui_forms {

// The built-in provider: an in-memory table in column storage. Every row is
// an array of values against the schema, held in one row-major vector
// (width = the schema's column count), so a field name is stored once for
// the table, not once per row. A BindingSource starts with an empty one.
//
// The table is used on its source's UI thread. Its own mutators (append,
// insert, remove, set_value, ...) report through the link the source gave
// it; the provider functions the source calls (write_cell, insert_row,
// remove_rows) do not, since the source applies those itself.
//
// Validation: a row's stable ID follows the StableId segment rule and is
// unique in the table; each value is one the column admits. A mutator given
// a row that breaks either throws std::invalid_argument and changes
// nothing; a provider function refuses instead. Inserting or removing in
// the middle moves the rows after it (O(rows * width)).
class BindingTable final : public BindingProvider {
public:
    explicit BindingTable(BindingSchema schema = {});

    [[nodiscard]] const BindingSchema& table_schema() const noexcept { return schema_; }
    [[nodiscard]] std::size_t count() const noexcept { return stable_ids_.size(); }

    // Appends a column (untyped columns take any kind); every row gets null
    // in it. Reports a reset, so the source re-reads the schema.
    std::size_t add_column(BindingColumn column);
    // The column named name, appended untyped when the table has none.
    std::size_t find_or_add_column(std::string_view name);

    std::size_t append(BindingRow row);
    std::size_t insert(std::size_t row, BindingRow values);
    // Removes rows [row, row + count) clamped to the end; returns how many.
    std::size_t remove(std::size_t row, std::size_t count);
    // Moves rows [row, row + count) so the first stands at target afterwards
    // (target <= count() - count); reports moved. Throws std::out_of_range,
    // changing nothing, for a block or target past the rows.
    void move(std::size_t row, std::size_t count, std::size_t target);
    void clear();
    // Replaces every row, checked whole first.
    void assign(std::vector<BindingRow> rows);

    [[nodiscard]] const BindingValue& value(std::size_t row, std::size_t column) const;
    // Writes a cell; false when it already held value.
    bool set_value(std::size_t row, std::size_t column, BindingValue value);
    [[nodiscard]] const std::string& stable_id(std::size_t row) const { return stable_ids_.at(row); }
    [[nodiscard]] bool editable(std::size_t row) const { return editable_.at(row) != 0U; }
    void set_editable(std::size_t row, bool editable);
    // The row's index, found by a linear search.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view stable_id) const;

    // An error text for a cell, or for the row with binding_whole_row; an
    // empty text clears it. Kept by stable ID, so it follows the row.
    void set_error(std::size_t row, std::size_t column, std::string text);
    [[nodiscard]] std::string error(std::size_t row, std::size_t column) const;

    // BindingProvider.
    [[nodiscard]] BindingSchema schema() const override;
    [[nodiscard]] BindingProviderCapabilities capabilities() const override;
    [[nodiscard]] std::size_t row_count() override;
    bool read_cell(std::size_t row, std::size_t column, BindingValue& value) override;
    bool read_stable_id(std::size_t row, std::string& stable_id) override;
    BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) override;
    [[nodiscard]] bool cell_read_only(std::size_t row, std::size_t column) override;
    bool read_error(std::size_t row, std::size_t column, std::string& text) override;
    BindingWriteResult insert_row(std::size_t row, const BindingRow& values) override;
    BindingWriteResult remove_rows(std::size_t row, std::size_t count) override;
    void attach(const BindingProviderLink& link) override;
    void detach() noexcept override;

private:
    struct CellError final {
        std::string stable_id;
        std::size_t column{};
        std::string text;
    };

    // Why values cannot become a row, or empty when they can.
    [[nodiscard]] std::string refusal(const BindingRow& values) const;
    void place_row(std::size_t row, const BindingRow& values);
    void erase_rows(std::size_t row, std::size_t count);
    [[nodiscard]] std::size_t width() const noexcept { return schema_.size(); }

    BindingSchema schema_;
    // Row r's cells are cells_[r * width(), (r + 1) * width()).
    std::vector<BindingValue> cells_;
    std::vector<std::string> stable_ids_;
    std::vector<unsigned char> editable_;
    std::unordered_set<std::string> identities_;
    // Sparse: errors are rare, and searched linearly.
    std::vector<CellError> errors_;
    BindingProviderLink link_;
};

} // namespace gui_forms
