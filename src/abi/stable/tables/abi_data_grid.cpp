// GF_TABLE_DATA_GRID: a data grid view's columns, rows, cells, current
// cell, selection and edit, over the C++ DataGridView. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "../values/abi_values.hpp"

#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity column_count_slot{GF_TABLE_DATA_GRID, 0U};
constexpr SlotIdentity column_at_slot{GF_TABLE_DATA_GRID, 1U};
constexpr SlotIdentity column_item_slot{GF_TABLE_DATA_GRID, 2U};
constexpr SlotIdentity insert_column_slot{GF_TABLE_DATA_GRID, 3U};
constexpr SlotIdentity remove_column_slot{GF_TABLE_DATA_GRID, 4U};
constexpr SlotIdentity column_index_slot{GF_TABLE_DATA_GRID, 5U};
constexpr SlotIdentity set_column_header_text_slot{GF_TABLE_DATA_GRID, 6U};
constexpr SlotIdentity set_column_width_slot{GF_TABLE_DATA_GRID, 7U};
constexpr SlotIdentity set_column_read_only_slot{GF_TABLE_DATA_GRID, 8U};
constexpr SlotIdentity set_column_hidden_slot{GF_TABLE_DATA_GRID, 9U};
constexpr SlotIdentity row_count_slot{GF_TABLE_DATA_GRID, 10U};
constexpr SlotIdentity insert_rows_slot{GF_TABLE_DATA_GRID, 11U};
constexpr SlotIdentity append_rows_slot{GF_TABLE_DATA_GRID, 12U};
constexpr SlotIdentity erase_rows_slot{GF_TABLE_DATA_GRID, 13U};
constexpr SlotIdentity replace_row_slot{GF_TABLE_DATA_GRID, 14U};
constexpr SlotIdentity clear_rows_slot{GF_TABLE_DATA_GRID, 15U};
constexpr SlotIdentity row_id_slot{GF_TABLE_DATA_GRID, 16U};
constexpr SlotIdentity row_position_slot{GF_TABLE_DATA_GRID, 17U};
constexpr SlotIdentity cell_value_slot{GF_TABLE_DATA_GRID, 18U};
constexpr SlotIdentity set_cell_value_slot{GF_TABLE_DATA_GRID, 19U};
constexpr SlotIdentity cell_text_slot{GF_TABLE_DATA_GRID, 20U};
constexpr SlotIdentity read_cells_slot{GF_TABLE_DATA_GRID, 21U};
constexpr SlotIdentity current_cell_slot{GF_TABLE_DATA_GRID, 22U};
constexpr SlotIdentity set_current_cell_slot{GF_TABLE_DATA_GRID, 23U};
constexpr SlotIdentity selected_rows_slot{GF_TABLE_DATA_GRID, 24U};
constexpr SlotIdentity selected_cells_slot{GF_TABLE_DATA_GRID, 25U};
constexpr SlotIdentity set_row_selected_slot{GF_TABLE_DATA_GRID, 26U};
constexpr SlotIdentity set_cell_selected_slot{GF_TABLE_DATA_GRID, 27U};
constexpr SlotIdentity clear_selection_slot{GF_TABLE_DATA_GRID, 28U};
constexpr SlotIdentity select_all_slot{GF_TABLE_DATA_GRID, 29U};
constexpr SlotIdentity begin_edit_slot{GF_TABLE_DATA_GRID, 30U};
constexpr SlotIdentity end_edit_slot{GF_TABLE_DATA_GRID, 31U};
constexpr SlotIdentity cancel_edit_slot{GF_TABLE_DATA_GRID, 32U};

constexpr const char* not_data_grid = "the control is not a data grid view";

// The 1.1 sizes of the records this table reads and writes.
constexpr std::uint32_t column_size_1_1 = 104U;
constexpr std::uint32_t row_size_1_1 = 56U;
static_assert(sizeof(gf_data_grid_column) == column_size_1_1);
static_assert(sizeof(gf_data_grid_row) == row_size_1_1);
static_assert(sizeof(gf_data_grid_cell) == 16U);

// A column's defaults for a zero width or minimum width (DataGridViewColumn's).
constexpr double default_column_width = 100.0;
constexpr double default_minimum_width = 20.0;

using GridPtr = std::shared_ptr<DataGridView>;

// -- caller arrays -------------------------------------------------------------

// Element index of a caller's array whose stride is its first element's
// struct_size. The stride and index are bounded by the callers (a u32 times
// at most GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL or a column count), so the
// offset cannot overflow.
template <typename Record>
const Record& element_at(const Record* first, std::uint32_t stride, std::uint64_t index) noexcept {
    const unsigned char* base = reinterpret_cast<const unsigned char*>(first);
    const std::uint64_t offset = static_cast<std::uint64_t>(stride) * index;
    const Record* element = reinterpret_cast<const Record*>(base + offset);
    return *element;
}

template <typename Record>
Record& mutable_element_at(Record* first, std::uint32_t stride, std::uint64_t index) noexcept {
    unsigned char* base = reinterpret_cast<unsigned char*>(first);
    const std::uint64_t offset = static_cast<std::uint64_t>(stride) * index;
    Record* element = reinterpret_cast<Record*>(base + offset);
    return *element;
}

// Checks a caller's array of count records, validated whole before any is
// read: first may be null only for none, and every element repeats the
// first one's struct_size, at least minimum and a multiple of 8. stride
// receives it.
template <typename Record>
gf_status check_array(const Record* first, std::uint64_t count, std::uint32_t minimum, SlotIdentity slot,
                      std::uint32_t& stride) {
    if (count == 0U) {
        stride = minimum;
        return GF_STATUS_OK;
    }
    if (first == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an array with elements needs its first element");
    }
    const std::uint32_t declared = (*first).struct_size;
    if (declared < minimum || declared % 8U != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "an array's stride is its first element's struct_size, at least the record's size");
    }
    for (std::uint64_t index = 1U; index < count; ++index) {
        if (element_at(first, declared, index).struct_size != declared) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "every element of an array repeats its struct_size");
        }
    }
    stride = declared;
    return GF_STATUS_OK;
}

// -- records ---------------------------------------------------------------------

// The column a caller's record describes, every field checked before the
// grid is touched (the grid checks the name and widths again).
gf_status read_column(const gf_data_grid_column* record, SlotIdentity slot, DataGridViewColumn& column) {
    if (record == nullptr || (*record).struct_size < column_size_1_1 || (*record).reserved != 0U ||
        (*record).kind > GF_DATA_GRID_COLUMN_KIND_LINK || (*record).read_only > 1U || (*record).hidden > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a column has at least the 1.1 size, reserved zero, a known kind and read_only and hidden 0 or 1");
    }
    const gf_value_kind value_kind = (*record).value_kind;
    if (value_kind != GF_VALUE_KIND_NULL && (value_kind < GF_VALUE_KIND_BOOLEAN || value_kind > GF_VALUE_KIND_TEXT)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a column's value kind is BOOLEAN, SIGNED, UNSIGNED, NUMBER, TEXT or NULL (TEXT)");
    }
    if (!std::isfinite((*record).width) || !std::isfinite((*record).minimum_width)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a column's widths are finite");
    }
    if ((*record).item_count > GF_DATA_GRID_MAXIMUM_COLUMN_ITEMS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a combo box column has at most 4096 choices");
    }
    if ((*record).items == nullptr && (*record).item_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a column with choices needs its items array");
    }
    std::string_view name;
    std::string_view header_text;
    std::string_view data_property_name;
    gf_status read = read_text((*record).name, slot, name);
    if (read == GF_STATUS_OK) read = read_text((*record).header_text, slot, header_text);
    if (read == GF_STATUS_OK) read = read_text((*record).data_property_name, slot, data_property_name);
    if (read != GF_STATUS_OK) {
        return read;
    }
    DataGridViewColumn made{};
    made.name = std::string(name);
    made.header_text = std::string(header_text);
    made.data_property_name = std::string(data_property_name);
    made.kind = static_cast<DataGridViewColumnKind>((*record).kind);
    // gf_value_kind and BindingValueKind number their kinds alike.
    made.value_kind = value_kind == GF_VALUE_KIND_NULL ? BindingValueKind::text
                                                        : static_cast<BindingValueKind>(value_kind);
    made.read_only = (*record).read_only == 1U;
    made.visible = (*record).hidden == 0U;
    made.width = (*record).width == 0.0 ? default_column_width : (*record).width;
    made.minimum_width = (*record).minimum_width == 0.0 ? default_minimum_width : (*record).minimum_width;
    made.items.reserve(static_cast<std::size_t>((*record).item_count));
    for (std::uint64_t index = 0U; index < (*record).item_count; ++index) {
        std::string_view item;
        const gf_status item_read = read_text((*record).items[index], slot, item);
        if (item_read != GF_STATUS_OK) {
            return item_read;
        }
        made.items.emplace_back(item);
    }
    column = std::move(made);
    return GF_STATUS_OK;
}

// The row a caller's record describes; its values are copied.
gf_status read_row(const gf_data_grid_row& record, std::size_t column_count, SlotIdentity slot, DataGridViewRow& row) {
    if (record.read_only > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a row's read_only is 0 or 1");
    }
    if (record.cell_count > column_count) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a row has at most one value per column");
    }
    std::uint32_t stride = 0U;
    const gf_status checked = check_array(record.cells, record.cell_count, value_size_1_0, slot, stride);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    std::string_view stable_id;
    std::string_view header_text;
    gf_status read = read_text(record.stable_id, slot, stable_id);
    if (read == GF_STATUS_OK) read = read_text(record.header_text, slot, header_text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    DataGridViewRow made{};
    made.stable_id.value = std::string(stable_id);
    made.header_text = std::string(header_text);
    made.read_only = record.read_only == 1U;
    made.cells.reserve(static_cast<std::size_t>(record.cell_count));
    for (std::uint64_t index = 0U; index < record.cell_count; ++index) {
        BindingValue value;
        const gf_status converted = read_value(&element_at(record.cells, stride, index), slot, value);
        if (converted != GF_STATUS_OK) {
            return converted;
        }
        made.cells.push_back(std::move(value));
    }
    row = std::move(made);
    return GF_STATUS_OK;
}

// count rows of a caller's array, read whole before the grid changes.
gf_status read_rows(const gf_data_grid_row* rows, std::uint64_t count, std::size_t column_count, SlotIdentity slot,
                    std::vector<DataGridViewRow>& read) {
    if (count > GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "one call adds at most 65536 rows");
    }
    std::uint32_t stride = 0U;
    const gf_status checked = check_array(rows, count, row_size_1_1, slot, stride);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    std::vector<DataGridViewRow> made;
    made.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        DataGridViewRow row;
        const gf_status converted = read_row(element_at(rows, stride, index), column_count, slot, row);
        if (converted != GF_STATUS_OK) {
            return converted;
        }
        made.push_back(std::move(row));
    }
    read = std::move(made);
    return GF_STATUS_OK;
}

// -- the grid ----------------------------------------------------------------------

gf_status require_column(const DataGridView& grid, std::uint64_t column, SlotIdentity slot) {
    if (column >= grid.columns().size()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the column is past the grid's columns");
    }
    return GF_STATUS_OK;
}

// The position of the row a caller names by ID.
gf_status find_row(const DataGridView& grid, gf_utf8_view row_id, SlotIdentity slot,
                   DataGridViewRowPosition& position) {
    std::string_view text;
    const gf_status read = read_text(row_id, slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    const DataGridViewRowId identity{std::string(text)};
    const std::optional<DataGridViewRowPosition> found = grid.row_position(identity);
    if (!found) {
        return fail(GF_STATUS_NOT_FOUND, slot, "no row of the grid has that row ID");
    }
    position = *found;
    return GF_STATUS_OK;
}

// The grid, the position of the row named row_id and a column of it.
gf_status enter_cell(gf_object grid, gf_utf8_view row_id, std::uint64_t column, SlotIdentity slot, GridPtr& typed,
                     DataGridViewRowPosition& position) {
    const gf_status entered = enter_typed_control(grid, slot, not_data_grid, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status found = find_row(*typed, row_id, slot, position);
    if (found != GF_STATUS_OK) {
        return found;
    }
    return require_column(*typed, column, slot);
}

// A cell's value for read_cells: the grid's own row's value in place, or
// another source's value copied into scratch.
const BindingValue& cell_reference(const DataGridView& grid, std::span<const DataGridViewRow> own, bool in_place,
                                   std::size_t position, std::size_t column, BindingValue& scratch) {
    if (in_place && position < own.size() && column < own[position].cells.size()) {
        return own[position].cells[column];
    }
    scratch = grid.cell_value(DataGridViewRowPosition{position}, column);
    return scratch;
}

// Writes complete's 1.0 fields after struct_size into a caller's gf_value,
// leaving its struct_size and any later fields.
void write_value_fields(const gf_value& complete, gf_value& destination) noexcept {
    const std::size_t skipped = sizeof(std::uint32_t);
    unsigned char* target = reinterpret_cast<unsigned char*>(&destination);
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
    std::memcpy(target + skipped, source + skipped, value_size_1_0 - skipped);
}

// -- columns -----------------------------------------------------------------------

gf_status GF_CALL data_grid_column_count(gf_object grid, std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(column_count_slot.table_id, column_count_slot.slot_index);
        if (count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, column_count_slot, "column_count needs a count output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, column_count_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *count = (*typed).columns().size();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(column_count_slot);
    }
}

gf_status GF_CALL data_grid_column_at(gf_object grid, std::uint64_t index, gf_data_grid_column* column,
                                      char* text_buffer, std::uint64_t text_capacity,
                                      std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(column_at_slot.table_id, column_at_slot.slot_index);
        if (column == nullptr || (*column).struct_size < column_size_1_1 || text_required == nullptr ||
            (text_buffer == nullptr && text_capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, column_at_slot,
                        "column_at needs a column record of at least the 1.1 size, a text size output, and a "
                        "buffer unless its capacity is zero");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, column_at_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, index, column_at_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        const DataGridViewColumn& held = (*typed).columns()[static_cast<std::size_t>(index)];
        const std::uint64_t total = held.name.size() + held.header_text.size() + held.data_property_name.size();
        *text_required = total;
        if (text_capacity < total) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, column_at_slot, "the text buffer is smaller than the column's text");
        }
        // The three texts one after another, each view into the buffer.
        gf_data_grid_column complete{};
        complete.struct_size = column_size_1_1;
        std::uint64_t offset = 0U;
        const std::string_view texts[3] = {held.name, held.header_text, held.data_property_name};
        gf_utf8_view views[3] = {};
        for (std::size_t text = 0U; text < 3U; ++text) {
            if (!texts[text].empty()) {
                std::memcpy(text_buffer + offset, texts[text].data(), texts[text].size());
                views[text].data = text_buffer + offset;
                views[text].size = texts[text].size();
            }
            offset += texts[text].size();
        }
        complete.name = views[0];
        complete.header_text = views[1];
        complete.data_property_name = views[2];
        complete.kind = static_cast<gf_data_grid_column_kind>(held.kind);
        complete.value_kind = static_cast<gf_value_kind>(held.value_kind);
        complete.read_only = held.read_only ? 1U : 0U;
        complete.hidden = held.visible ? 0U : 1U;
        complete.width = held.width;
        complete.minimum_width = held.minimum_width;
        complete.items = nullptr;
        complete.item_count = held.items.size();
        // The 1.1 fields after struct_size; a larger caller record keeps its
        // later bytes.
        const std::size_t skipped = sizeof(std::uint32_t);
        unsigned char* destination = reinterpret_cast<unsigned char*>(column);
        const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
        std::memcpy(destination + skipped, source + skipped, column_size_1_1 - skipped);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(column_at_slot);
    }
}

gf_status GF_CALL data_grid_column_item(gf_object grid, std::uint64_t column, std::uint64_t item, char* buffer,
                                        std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(column_item_slot.table_id, column_item_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, column_item_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, column_item_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        const std::vector<std::string>& items = (*typed).columns()[static_cast<std::size_t>(column)].items;
        if (item >= items.size()) {
            return fail(GF_STATUS_NOT_FOUND, column_item_slot, "the choice is past the column's choices");
        }
        return write_text(items[static_cast<std::size_t>(item)], buffer, capacity, required, column_item_slot);
    } catch (...) {
        return fail_with_current_exception(column_item_slot);
    }
}

gf_status GF_CALL data_grid_insert_column(gf_object grid, std::uint64_t index,
                                          const gf_data_grid_column* column) noexcept {
    try {
        const ShadowFrameScope shadow_frame(insert_column_slot.table_id, insert_column_slot.slot_index);
        DataGridViewColumn inserted;
        const gf_status read = read_column(column, insert_column_slot, inserted);
        if (read != GF_STATUS_OK) {
            return read;
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, insert_column_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (index > (*typed).columns().size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, insert_column_slot, "the insertion is past the grid's columns");
        }
        (*typed).insert_column(static_cast<std::size_t>(index), std::move(inserted));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(insert_column_slot);
    }
}

gf_status GF_CALL data_grid_remove_column(gf_object grid, std::uint64_t index) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_column_slot.table_id, remove_column_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, remove_column_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, index, remove_column_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        (*typed).remove_column_at(static_cast<std::size_t>(index));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_column_slot);
    }
}

gf_status GF_CALL data_grid_column_index(gf_object grid, gf_utf8_view name, std::uint64_t* index) noexcept {
    try {
        const ShadowFrameScope shadow_frame(column_index_slot.table_id, column_index_slot.slot_index);
        if (index == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, column_index_slot, "column_index needs an index output");
        }
        std::string_view text;
        const gf_status read = read_text(name, column_index_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, column_index_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<std::size_t> found = (*typed).column_index(text);
        if (!found) {
            return fail(GF_STATUS_NOT_FOUND, column_index_slot, "no column of the grid has that name");
        }
        *index = *found;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(column_index_slot);
    }
}

gf_status GF_CALL data_grid_set_column_header_text(gf_object grid, std::uint64_t column, gf_utf8_view text) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_column_header_text_slot.table_id,
                                            set_column_header_text_slot.slot_index);
        std::string_view header;
        const gf_status read = read_text(text, set_column_header_text_slot, header);
        if (read != GF_STATUS_OK) {
            return read;
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, set_column_header_text_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, set_column_header_text_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        (*typed).set_column_header_text(static_cast<std::size_t>(column), std::string(header));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_column_header_text_slot);
    }
}

gf_status GF_CALL data_grid_set_column_width(gf_object grid, std::uint64_t column, double width) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_column_width_slot.table_id, set_column_width_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, set_column_width_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, set_column_width_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        (*typed).set_column_width(static_cast<std::size_t>(column), width);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_column_width_slot);
    }
}

gf_status GF_CALL data_grid_set_column_read_only(gf_object grid, std::uint64_t column,
                                                 std::uint32_t read_only) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_column_read_only_slot.table_id, set_column_read_only_slot.slot_index);
        if (read_only > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_column_read_only_slot, "read_only is 0 or 1");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, set_column_read_only_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, set_column_read_only_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        (*typed).set_column_read_only(static_cast<std::size_t>(column), read_only == 1U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_column_read_only_slot);
    }
}

gf_status GF_CALL data_grid_set_column_hidden(gf_object grid, std::uint64_t column, std::uint32_t hidden) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_column_hidden_slot.table_id, set_column_hidden_slot.slot_index);
        if (hidden > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_column_hidden_slot, "hidden is 0 or 1");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, set_column_hidden_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, set_column_hidden_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        (*typed).set_column_visible(static_cast<std::size_t>(column), hidden == 0U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_column_hidden_slot);
    }
}

// -- rows ----------------------------------------------------------------------------

gf_status GF_CALL data_grid_row_count(gf_object grid, std::uint64_t* count, std::int64_t* new_row) noexcept {
    try {
        const ShadowFrameScope shadow_frame(row_count_slot.table_id, row_count_slot.slot_index);
        if (count == nullptr || new_row == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, row_count_slot, "row_count needs a count and a new row output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, row_count_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<DataGridViewRowPosition> shown_new = (*typed).new_row_index();
        *count = (*typed).row_count();
        *new_row = shown_new ? static_cast<std::int64_t>((*shown_new).value) : -1;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(row_count_slot);
    }
}

gf_status GF_CALL data_grid_insert_rows(gf_object grid, std::uint64_t index, const gf_data_grid_row* rows,
                                        std::uint64_t count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(insert_rows_slot.table_id, insert_rows_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, insert_rows_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::vector<DataGridViewRow> inserted;
        const gf_status read = read_rows(rows, count, (*typed).columns().size(), insert_rows_slot, inserted);
        if (read != GF_STATUS_OK) {
            return read;
        }
        // One insertion edit: the grid refuses all of it or takes all of it.
        (*typed).rows.insert(static_cast<std::size_t>(index), std::move(inserted));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(insert_rows_slot);
    }
}

gf_status GF_CALL data_grid_append_rows(gf_object grid, const gf_data_grid_row* rows, std::uint64_t count,
                                        std::uint64_t* first) noexcept {
    try {
        const ShadowFrameScope shadow_frame(append_rows_slot.table_id, append_rows_slot.slot_index);
        if (first == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, append_rows_slot, "append_rows needs a first output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, append_rows_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::vector<DataGridViewRow> appended;
        const gf_status read = read_rows(rows, count, (*typed).columns().size(), append_rows_slot, appended);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const std::size_t position = (*typed).rows.read().size();
        (*typed).rows.append(std::move(appended));
        *first = position;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(append_rows_slot);
    }
}

gf_status GF_CALL data_grid_erase_rows(gf_object grid, std::uint64_t index, std::uint64_t count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(erase_rows_slot.table_id, erase_rows_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, erase_rows_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).rows.erase(static_cast<std::size_t>(index), static_cast<std::size_t>(count));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(erase_rows_slot);
    }
}

gf_status GF_CALL data_grid_replace_row(gf_object grid, std::uint64_t index, const gf_data_grid_row* row) noexcept {
    try {
        const ShadowFrameScope shadow_frame(replace_row_slot.table_id, replace_row_slot.slot_index);
        if (row == nullptr || (*row).struct_size < row_size_1_1) {
            return fail(GF_STATUS_INVALID_ARGUMENT, replace_row_slot, "replace_row needs a row of at least the 1.1 size");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, replace_row_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DataGridViewRow replacement;
        const gf_status read = read_row(*row, (*typed).columns().size(), replace_row_slot, replacement);
        if (read != GF_STATUS_OK) {
            return read;
        }
        (*typed).rows.replace(static_cast<std::size_t>(index), std::move(replacement));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(replace_row_slot);
    }
}

gf_status GF_CALL data_grid_clear_rows(gf_object grid) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_rows_slot.table_id, clear_rows_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, clear_rows_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).clear_rows();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_rows_slot);
    }
}

gf_status GF_CALL data_grid_row_id(gf_object grid, std::uint64_t position, char* buffer, std::uint64_t capacity,
                                   std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(row_id_slot.table_id, row_id_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, row_id_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (position >= (*typed).row_count()) {
            return fail(GF_STATUS_NOT_FOUND, row_id_slot, "the position is past the grid's rows");
        }
        const DataGridViewRowId identity = (*typed).row_id(DataGridViewRowPosition{static_cast<std::size_t>(position)});
        return write_text(identity.value, buffer, capacity, required, row_id_slot);
    } catch (...) {
        return fail_with_current_exception(row_id_slot);
    }
}

gf_status GF_CALL data_grid_row_position(gf_object grid, gf_utf8_view row_id, std::uint64_t* position) noexcept {
    try {
        const ShadowFrameScope shadow_frame(row_position_slot.table_id, row_position_slot.slot_index);
        if (position == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, row_position_slot, "row_position needs a position output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, row_position_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DataGridViewRowPosition found{};
        const gf_status located = find_row(*typed, row_id, row_position_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        *position = found.value;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(row_position_slot);
    }
}

// -- cells ---------------------------------------------------------------------------

gf_status GF_CALL data_grid_cell_value(gf_object grid, gf_utf8_view row_id, std::uint64_t column,
                                       gf_value_kind as_kind, gf_value* value, char* text_buffer,
                                       std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(cell_value_slot.table_id, cell_value_slot.slot_index);
        const gf_status output = check_value_output(value, text_buffer, text_capacity, text_required, cell_value_slot);
        if (output != GF_STATUS_OK) {
            return output;
        }
        if (as_kind > GF_VALUE_KIND_COLLECTION) {
            return fail(GF_STATUS_INVALID_ARGUMENT, cell_value_slot, "as_kind is a gf_value kind");
        }
        GridPtr typed;
        DataGridViewRowPosition position{};
        const gf_status entered = enter_cell(grid, row_id, column, cell_value_slot, typed, position);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const BindingValue held = (*typed).cell_value(position, static_cast<std::size_t>(column));
        if (as_kind == GF_VALUE_KIND_NULL || binding_value_kind(held) == BindingValueKind::null) {
            return write_value_output(held, value, text_buffer, text_capacity, text_required, cell_value_slot);
        }
        const std::optional<BindingValue> converted =
            convert_binding_value(held, static_cast<BindingValueKind>(as_kind));
        if (!converted) {
            return fail(GF_STATUS_INVALID_ARGUMENT, cell_value_slot, "the cell's value has no form of that kind");
        }
        return write_value_output(*converted, value, text_buffer, text_capacity, text_required, cell_value_slot);
    } catch (...) {
        return fail_with_current_exception(cell_value_slot);
    }
}

gf_status GF_CALL data_grid_set_cell_value(gf_object grid, gf_utf8_view row_id, std::uint64_t column,
                                           const gf_value* value, std::uint32_t* accepted) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_cell_value_slot.table_id, set_cell_value_slot.slot_index);
        if (accepted == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_cell_value_slot, "set_cell_value needs an accepted output");
        }
        BindingValue stored;
        const gf_status read = read_value(value, set_cell_value_slot, stored);
        if (read != GF_STATUS_OK) {
            return read;
        }
        GridPtr typed;
        DataGridViewRowPosition position{};
        const gf_status entered = enter_cell(grid, row_id, column, set_cell_value_slot, typed, position);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool set = (*typed).set_cell_value(position, static_cast<std::size_t>(column), std::move(stored));
        *accepted = set ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_cell_value_slot);
    }
}

gf_status GF_CALL data_grid_cell_text(gf_object grid, gf_utf8_view row_id, std::uint64_t column, char* buffer,
                                      std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(cell_text_slot.table_id, cell_text_slot.slot_index);
        GridPtr typed;
        DataGridViewRowPosition position{};
        const gf_status entered = enter_cell(grid, row_id, column, cell_text_slot, typed, position);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::string text = (*typed).cell_text(position, static_cast<std::size_t>(column));
        return write_text(text, buffer, capacity, required, cell_text_slot);
    } catch (...) {
        return fail_with_current_exception(cell_text_slot);
    }
}

gf_status GF_CALL data_grid_read_cells(gf_object grid, std::uint64_t column, std::uint64_t first_row,
                                       std::uint64_t row_count, gf_value* values, char* text_buffer,
                                       std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(read_cells_slot.table_id, read_cells_slot.slot_index);
        if (text_required == nullptr || (text_buffer == nullptr && text_capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, read_cells_slot,
                        "read_cells needs a text size output, and a buffer unless its capacity is zero");
        }
        if (row_count > GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, read_cells_slot, "one call reads at most 65536 rows");
        }
        std::uint32_t stride = 0U;
        const gf_status checked = check_array(values, row_count, value_size_1_0, read_cells_slot, stride);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, read_cells_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status present = require_column(*typed, column, read_cells_slot);
        if (present != GF_STATUS_OK) {
            return present;
        }
        const std::size_t shown = (*typed).row_count();
        if (first_row > shown || row_count > shown - first_row) {
            return fail(GF_STATUS_NOT_FOUND, read_cells_slot, "the range is past the grid's rows");
        }
        const std::size_t index = static_cast<std::size_t>(column);
        const bool in_place = (*typed).row_source() == DataGridViewRowSource::unbound;
        const std::span<const DataGridViewRow> own = (*typed).rows.read();
        BindingValue scratch;
        // First pass: every value has a form and the text fits.
        std::uint64_t total = 0U;
        for (std::uint64_t offset = 0U; offset < row_count; ++offset) {
            const std::size_t position = static_cast<std::size_t>(first_row + offset);
            const BindingValue& held = cell_reference(*typed, own, in_place, position, index, scratch);
            gf_value probe{};
            std::string_view text;
            if (!record_binding_value(held, probe, text)) {
                return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, read_cells_slot,
                            "a cell's value kind has no gf_value form in this library");
            }
            total += text.size();
        }
        *text_required = total;
        if (text_capacity < total) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, read_cells_slot, "the text buffer is smaller than the cells' text");
        }
        // Second pass: the values, their text one after another.
        std::uint64_t written = 0U;
        for (std::uint64_t offset = 0U; offset < row_count; ++offset) {
            const std::size_t position = static_cast<std::size_t>(first_row + offset);
            const BindingValue& held = cell_reference(*typed, own, in_place, position, index, scratch);
            gf_value complete{};
            complete.struct_size = value_size_1_0;
            std::string_view text;
            static_cast<void>(record_binding_value(held, complete, text));
            // A provider that changed its answer between the passes cannot
            // overrun the buffer.
            if (text.size() > total - written) {
                return fail(GF_STATUS_INTERNAL, read_cells_slot, "a cell's text changed while the cells were read");
            }
            if (!text.empty()) {
                std::memcpy(text_buffer + written, text.data(), text.size());
                complete.text.data = text_buffer + written;
                complete.text.size = text.size();
                written += text.size();
            }
            write_value_fields(complete, mutable_element_at(values, stride, offset));
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(read_cells_slot);
    }
}

// -- current cell, selection and edit -------------------------------------------------

gf_status GF_CALL data_grid_current_cell(gf_object grid, std::uint32_t* present, std::uint64_t* row,
                                         std::uint64_t* column) noexcept {
    try {
        const ShadowFrameScope shadow_frame(current_cell_slot.table_id, current_cell_slot.slot_index);
        if (present == nullptr || row == nullptr || column == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, current_cell_slot,
                        "current_cell needs present, row and column outputs");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, current_cell_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<DataGridViewCellAddress> current = (*typed).current_cell();
        if (!current) {
            *present = 0U;
            return GF_STATUS_OK;
        }
        *present = 1U;
        *row = (*current).row.value;
        *column = (*current).column;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(current_cell_slot);
    }
}

gf_status GF_CALL data_grid_set_current_cell(gf_object grid, gf_utf8_view row_id, std::uint64_t column,
                                             std::uint32_t* moved) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_current_cell_slot.table_id, set_current_cell_slot.slot_index);
        if (moved == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_current_cell_slot, "set_current_cell needs a moved output");
        }
        std::string_view text;
        const gf_status read = read_text(row_id, set_current_cell_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        GridPtr typed;
        std::optional<DataGridViewCellAddress> target;
        if (text.empty()) {
            const gf_status entered = enter_typed_control(grid, set_current_cell_slot, not_data_grid, typed);
            if (entered != GF_STATUS_OK) {
                return entered;
            }
        } else {
            DataGridViewRowPosition position{};
            const gf_status entered = enter_cell(grid, row_id, column, set_current_cell_slot, typed, position);
            if (entered != GF_STATUS_OK) {
                return entered;
            }
            target = DataGridViewCellAddress{position, static_cast<std::size_t>(column)};
        }
        const bool set = (*typed).set_current_cell(target);
        *moved = set ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_current_cell_slot);
    }
}

gf_status GF_CALL data_grid_selected_rows(gf_object grid, std::uint64_t* positions, std::uint64_t capacity,
                                          std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(selected_rows_slot.table_id, selected_rows_slot.slot_index);
        if (count == nullptr || (positions == nullptr && capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, selected_rows_slot,
                        "selected_rows needs a count output, and positions unless the capacity is zero");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, selected_rows_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::vector<DataGridViewRowPosition> selected = (*typed).selected_rows();
        *count = selected.size();
        if (capacity < selected.size()) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, selected_rows_slot, "the array is smaller than the selected rows");
        }
        for (std::size_t index = 0U; index < selected.size(); ++index) {
            positions[index] = selected[index].value;
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(selected_rows_slot);
    }
}

gf_status GF_CALL data_grid_selected_cells(gf_object grid, gf_data_grid_cell* cells, std::uint64_t capacity,
                                           std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(selected_cells_slot.table_id, selected_cells_slot.slot_index);
        if (count == nullptr || (cells == nullptr && capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, selected_cells_slot,
                        "selected_cells needs a count output, and cells unless the capacity is zero");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, selected_cells_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::vector<DataGridViewCellAddress> selected = (*typed).selected_cells();
        *count = selected.size();
        if (capacity < selected.size()) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, selected_cells_slot, "the array is smaller than the selected cells");
        }
        for (std::size_t index = 0U; index < selected.size(); ++index) {
            cells[index].row = selected[index].row.value;
            cells[index].column = selected[index].column;
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(selected_cells_slot);
    }
}

gf_status GF_CALL data_grid_set_row_selected(gf_object grid, gf_utf8_view row_id, std::uint32_t selected) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_row_selected_slot.table_id, set_row_selected_slot.slot_index);
        if (selected > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_row_selected_slot, "selected is 0 or 1");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, set_row_selected_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        DataGridViewRowPosition position{};
        const gf_status found = find_row(*typed, row_id, set_row_selected_slot, position);
        if (found != GF_STATUS_OK) {
            return found;
        }
        (*typed).set_row_selected(position, selected == 1U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_row_selected_slot);
    }
}

gf_status GF_CALL data_grid_set_cell_selected(gf_object grid, gf_utf8_view row_id, std::uint64_t column,
                                              std::uint32_t selected) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_cell_selected_slot.table_id, set_cell_selected_slot.slot_index);
        if (selected > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_cell_selected_slot, "selected is 0 or 1");
        }
        GridPtr typed;
        DataGridViewRowPosition position{};
        const gf_status entered = enter_cell(grid, row_id, column, set_cell_selected_slot, typed, position);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).set_cell_selected(position, static_cast<std::size_t>(column), selected == 1U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_cell_selected_slot);
    }
}

gf_status GF_CALL data_grid_clear_selection(gf_object grid) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_selection_slot.table_id, clear_selection_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, clear_selection_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).clear_selection();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_selection_slot);
    }
}

gf_status GF_CALL data_grid_select_all(gf_object grid) noexcept {
    try {
        const ShadowFrameScope shadow_frame(select_all_slot.table_id, select_all_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, select_all_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).select_all();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(select_all_slot);
    }
}

gf_status GF_CALL data_grid_begin_edit(gf_object grid, std::uint32_t select_all, std::uint32_t* began) noexcept {
    try {
        const ShadowFrameScope shadow_frame(begin_edit_slot.table_id, begin_edit_slot.slot_index);
        if (select_all > 1U || began == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, begin_edit_slot, "begin_edit takes select_all 0 or 1 and a began output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, begin_edit_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool editing = (*typed).begin_edit(select_all == 1U);
        *began = editing ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(begin_edit_slot);
    }
}

gf_status GF_CALL data_grid_end_edit(gf_object grid, std::uint32_t* committed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(end_edit_slot.table_id, end_edit_slot.slot_index);
        if (committed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, end_edit_slot, "end_edit needs a committed output");
        }
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, end_edit_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool done = (*typed).end_edit();
        *committed = done ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(end_edit_slot);
    }
}

gf_status GF_CALL data_grid_cancel_edit(gf_object grid) noexcept {
    try {
        const ShadowFrameScope shadow_frame(cancel_edit_slot.table_id, cancel_edit_slot.slot_index);
        GridPtr typed;
        const gf_status entered = enter_typed_control(grid, cancel_edit_slot, not_data_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).cancel_edit();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(cancel_edit_slot);
    }
}

constexpr gf_data_grid_table table{
    .struct_size = sizeof(gf_data_grid_table),
    .abi_minor = GF_ABI_MINOR,
    .column_count = &data_grid_column_count,
    .column_at = &data_grid_column_at,
    .column_item = &data_grid_column_item,
    .insert_column = &data_grid_insert_column,
    .remove_column = &data_grid_remove_column,
    .column_index = &data_grid_column_index,
    .set_column_header_text = &data_grid_set_column_header_text,
    .set_column_width = &data_grid_set_column_width,
    .set_column_read_only = &data_grid_set_column_read_only,
    .set_column_hidden = &data_grid_set_column_hidden,
    .row_count = &data_grid_row_count,
    .insert_rows = &data_grid_insert_rows,
    .append_rows = &data_grid_append_rows,
    .erase_rows = &data_grid_erase_rows,
    .replace_row = &data_grid_replace_row,
    .clear_rows = &data_grid_clear_rows,
    .row_id = &data_grid_row_id,
    .row_position = &data_grid_row_position,
    .cell_value = &data_grid_cell_value,
    .set_cell_value = &data_grid_set_cell_value,
    .cell_text = &data_grid_cell_text,
    .read_cells = &data_grid_read_cells,
    .current_cell = &data_grid_current_cell,
    .set_current_cell = &data_grid_set_current_cell,
    .selected_rows = &data_grid_selected_rows,
    .selected_cells = &data_grid_selected_cells,
    .set_row_selected = &data_grid_set_row_selected,
    .set_cell_selected = &data_grid_set_cell_selected,
    .clear_selection = &data_grid_clear_selection,
    .select_all = &data_grid_select_all,
    .begin_edit = &data_grid_begin_edit,
    .end_edit = &data_grid_end_edit,
    .cancel_edit = &data_grid_cancel_edit,
};

} // namespace

const gf_data_grid_table& data_grid_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
