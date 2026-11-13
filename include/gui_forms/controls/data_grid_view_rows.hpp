#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/collection_cell/collection_cell.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/controls/data_grid_view_row_order.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

// One unbound row. `stable_id` is unique within the grid; an empty ID is
// replaced by a generated one ("row.1", "row.2", ...). `cells` holds one value
// per column in column order; a shorter list is padded with empty values.
struct GUI_FORMS_API DataGridViewRow final {
    DataGridViewRowId stable_id{};
    std::vector<BindingValue> cells{};
    std::string header_text{};
    bool read_only{};

    friend bool operator==(const DataGridViewRow& left, const DataGridViewRow& right) {
        const bool equal = left.stable_id == right.stable_id && left.cells == right.cells &&
                           left.header_text == right.header_text && left.read_only == right.read_only;
        return equal;
    }
};

// A grid's unbound rows edited in place (in-place collections design,
// owner 2026-11-07; the DataGridView pilot): `(*grid).rows.read()`,
// `rows[i]`, `rows = {...}`, `rows.insert(i, row)`, `rows.append(row)`,
// `rows.append(rows)`, `rows.erase(i, n)`, `rows.replace(i, row)` and
// `rows.clear()`. Positions are presentation positions of the unbound rows.
//
// A standard-layout struct whose only member is the cell, taken by the
// declaring class as a base. Each edit runs the declaring class's
// prepare_edit hook (it may refuse by throwing: nothing changes), the
// store, then its written hook. The declaring class takes the pending
// damage at its flush through take_dirty, and writes cell values and
// reorders through the owner operations.
template <typename Declaring>
struct DataGridViewRowsCapability {
    CollectionCell<DataGridViewRow, DataGridViewRowsCapability<Declaring>> rows;

    static void prepare_edit(DataGridViewRowsCapability& capability, const CollectionEdit& edit,
                             std::span<DataGridViewRow> incoming) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_edit(std::as_const(capability.rows), edit, incoming);
    }

    static void written(DataGridViewRowsCapability& capability, const CollectionEdit& edit) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.written(std::as_const(capability.rows), edit);
    }

protected:
    // CollectionDirty::take, for the declaring class's flush.
    [[nodiscard]] static std::optional<std::size_t> take_dirty(DataGridViewRowsCapability& capability,
                                                               std::vector<std::uint32_t>& replaced) {
        const std::optional<std::size_t> structural_from = capability.rows.take_dirty(replaced);
        return structural_from;
    }
    // The owner operations (CollectionCell): in-place field writes, marked
    // first, and the store of a reordering.
    [[nodiscard]] static std::uint64_t rows_revision(const DataGridViewRowsCapability& capability) noexcept {
        const std::uint64_t revision = capability.rows.revision();
        return revision;
    }
    [[nodiscard]] static std::span<DataGridViewRow> row_elements(DataGridViewRowsCapability& capability) noexcept {
        const std::span<DataGridViewRow> elements = capability.rows.owner_elements();
        return elements;
    }
    static void mark_row_replaced(DataGridViewRowsCapability& capability, std::size_t index) {
        capability.rows.mark_replaced(index);
    }
    static void mark_all_rows(DataGridViewRowsCapability& capability) noexcept { capability.rows.mark_all(); }
    static void store_rows(DataGridViewRowsCapability& capability, std::vector<DataGridViewRow> rows) noexcept {
        capability.rows.owner_store(std::move(rows));
    }
};

} // namespace gui_forms
