#pragma once

// Rows by field name for binding tests. A source's built-in BindingTable
// holds rows by column position; these tests describe their rows by field
// name, so each name is looked up (or grown, untyped) as a column of the
// table first. Test support only: the library itself places values by
// column (BindingTable, BindingRow).

#include "gui_forms/binding.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::test_rows {

// One row: its stable ID, its fields by name, whether it may be edited, and
// error texts by field name (an empty name for the row's own).
struct NamedRow final {
    std::string stable_id;
    std::vector<std::pair<std::string, BindingValue>> fields;
    bool editable{true};
    std::vector<std::pair<std::string, std::string>> errors;
};

// The table behind source; a test that needs one fails without it.
inline BindingTable& table_of(BindingSource& source) {
    const std::shared_ptr<BindingTable> table = source.table();
    if (!table) {
        throw std::logic_error("the binding test expects the source's built-in table");
    }
    return *table;
}

// named as a positional row of source's table, growing missing columns.
inline BindingRow row_of(BindingSource& source, const NamedRow& named) {
    BindingTable& table = table_of(source);
    for (const std::pair<std::string, BindingValue>& field : named.fields) {
        static_cast<void>(table.find_or_add_column(field.first));
    }
    BindingRow row;
    row.stable_id = named.stable_id;
    row.editable = named.editable;
    row.values.resize(table.table_schema().size());
    for (const std::pair<std::string, BindingValue>& field : named.fields) {
        const std::optional<std::size_t> column = table.table_schema().find(field.first);
        row.values[*column] = field.second;
    }
    return row;
}

// Replaces source's rows with rows (BindingTable::assign), then sets each
// row's error texts.
inline void fill_source(BindingSource& source, const std::vector<NamedRow>& rows) {
    BindingTable& table = table_of(source);
    std::vector<BindingRow> placed;
    placed.reserve(rows.size());
    for (const NamedRow& named : rows) {
        placed.push_back(row_of(source, named));
    }
    table.assign(std::move(placed));
    for (std::size_t row = 0U; row < rows.size(); ++row) {
        for (const std::pair<std::string, std::string>& error : rows[row].errors) {
            std::size_t column = binding_whole_row;
            if (!error.first.empty()) {
                column = table.find_or_add_column(error.first);
            }
            table.set_error(row, column, error.second);
        }
    }
}

} // namespace gui_forms::test_rows
