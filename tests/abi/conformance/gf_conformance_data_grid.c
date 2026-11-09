/* Kit part: a data grid view's columns, rows, cells, current cell,
 * selection and edit (GF_TABLE_DATA_GRID, since 1.1), on a created window's
 * root: the calls a C or C# program fills and reads a grid with, their
 * refusals, the bulk read into caller storage, and the change events the
 * grid raises through events.subscribe. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_value blank(void) {
    gf_value made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    return made;
}

static gf_value text_cell(const char* text) {
    gf_value made = blank();
    made.kind = GF_VALUE_KIND_TEXT;
    made.text = gf_kit_text(text);
    return made;
}

static gf_value number_cell(double number) {
    gf_value made = blank();
    made.kind = GF_VALUE_KIND_NUMBER;
    made.number = number;
    return made;
}

static gf_value boolean_cell(uint32_t value) {
    gf_value made = blank();
    made.kind = GF_VALUE_KIND_BOOLEAN;
    made.boolean = value;
    return made;
}

static gf_data_grid_column column_record(const char* name, const char* header, gf_data_grid_column_kind kind) {
    gf_data_grid_column made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = kind;
    made.name = gf_kit_text(name);
    made.header_text = gf_kit_text(header);
    return made;
}

static gf_data_grid_row row_record(const char* stable_id, const gf_value* cells, uint64_t cell_count) {
    gf_data_grid_row made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.stable_id = gf_kit_text(stable_id);
    made.cells = cells;
    made.cell_count = cell_count;
    return made;
}

/* Whether the cell's value, as stored, is the text expected. */
static int cell_text_is(gf_kit* kit, gf_object grid, const char* row_id, uint64_t column, const char* expected) {
    gf_value value = blank();
    char text[64];
    uint64_t required = 0u;
    if ((*kit).data_grid.cell_value(grid, gf_kit_text(row_id), column, GF_VALUE_KIND_NULL, &value, text,
                                    sizeof(text), &required) != GF_STATUS_OK) {
        return 0;
    }
    return value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, expected);
}

/* Whether the row at position has the ID expected. */
static int row_id_is(gf_kit* kit, gf_object grid, uint64_t position, const char* expected) {
    char text[32];
    uint64_t required = 0u;
    if ((*kit).data_grid.row_id(grid, position, text, sizeof(text), &required) != GF_STATUS_OK) {
        return 0;
    }
    return required == strlen(expected) && memcmp(text, expected, required) == 0;
}

static void columns(gf_kit* kit, gf_object grid) {
    gf_data_grid_column name = column_record("name", "Name", GF_DATA_GRID_COLUMN_KIND_TEXT);
    gf_data_grid_column frequency = column_record("frequency", "Frequency", GF_DATA_GRID_COLUMN_KIND_TEXT);
    frequency.value_kind = GF_VALUE_KIND_NUMBER;
    frequency.width = 120.0;
    gf_data_grid_column active = column_record("active", "On", GF_DATA_GRID_COLUMN_KIND_CHECK_BOX);
    const gf_utf8_view modes[3] = {gf_kit_text("AM"), gf_kit_text("FM"), gf_kit_text("USB")};
    gf_data_grid_column mode = column_record("mode", "Mode", GF_DATA_GRID_COLUMN_KIND_COMBO_BOX);
    mode.items = modes;
    mode.item_count = 3u;

    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 0u, &name), GF_STATUS_OK, "insert the first column");
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 1u, &active), GF_STATUS_OK, "append a column");
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 1u, &frequency), GF_STATUS_OK,
                  "insert a column between two");
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 3u, &mode), GF_STATUS_OK, "append a combo box column");
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 9u, &name), GF_STATUS_INVALID_ARGUMENT,
                  "an insertion past the columns is refused");
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 0u, &name), GF_STATUS_INVALID_ARGUMENT,
                  "a repeated column name is refused");
    gf_data_grid_column hidden = column_record("hidden", "Hidden", GF_DATA_GRID_COLUMN_KIND_TEXT);
    hidden.hidden = 1u;
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 0u, &hidden), GF_STATUS_UNSUPPORTED_CAPABILITY,
                  "a hidden column is not supported yet");
    gf_data_grid_column wrong = column_record("wrong", "Wrong", 9u);
    gf_kit_status(kit, (*kit).data_grid.insert_column(grid, 0u, &wrong), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown column kind is refused");

    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).data_grid.column_count(grid, &count), GF_STATUS_OK, "count the columns");
    gf_kit_check(kit, count == 4u, "four columns");
    uint64_t index = 9u;
    gf_kit_status(kit, (*kit).data_grid.column_index(grid, gf_kit_text("active"), &index), GF_STATUS_OK,
                  "find a column by name");
    gf_kit_check(kit, index == 2u, "the inserted column moved the later one up");
    gf_kit_status(kit, (*kit).data_grid.column_index(grid, gf_kit_text("none"), &index), GF_STATUS_NOT_FOUND,
                  "an unknown name is not found");

    gf_data_grid_column read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).data_grid.column_at(grid, 1u, &read, text, 4u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a short buffer is refused");
    gf_kit_check(kit, required == 18u && read.name.size == 0u, "the size is given and the record unwritten");
    gf_kit_status(kit, (*kit).data_grid.column_at(grid, 1u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a column");
    gf_kit_check(kit, gf_kit_view_equals(read.name, "frequency") && gf_kit_view_equals(read.header_text, "Frequency") &&
                          read.value_kind == GF_VALUE_KIND_NUMBER && read.width == 120.0 &&
                          read.minimum_width == 20.0 && read.hidden == 0u,
                 "the column reads back as inserted, with the default minimum");
    gf_kit_status(kit, (*kit).data_grid.column_at(grid, 3u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the combo box column");
    gf_kit_check(kit, read.kind == GF_DATA_GRID_COLUMN_KIND_COMBO_BOX && read.item_count == 3u && read.items == NULL,
                 "a combo box column counts its choices");
    gf_kit_status(kit, (*kit).data_grid.column_item(grid, 3u, 1u, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a choice");
    gf_kit_check(kit, required == 2u && memcmp(text, "FM", 2u) == 0, "the second choice is FM");
    gf_kit_status(kit, (*kit).data_grid.column_item(grid, 3u, 3u, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a choice past the choices is not found");
    gf_kit_status(kit, (*kit).data_grid.column_at(grid, 4u, &read, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a column past the columns is not found");

    gf_kit_status(kit, (*kit).data_grid.set_column_header_text(grid, 1u, gf_kit_text("MHz")), GF_STATUS_OK,
                  "set a header text");
    gf_kit_status(kit, (*kit).data_grid.set_column_width(grid, 1u, 90.0), GF_STATUS_OK, "set a width");
    gf_kit_status(kit, (*kit).data_grid.set_column_width(grid, 1u, 1.0), GF_STATUS_INVALID_ARGUMENT,
                  "a width below the minimum is refused");
    gf_kit_status(kit, (*kit).data_grid.set_column_read_only(grid, 1u, 1u), GF_STATUS_OK, "make a column read-only");
    gf_kit_status(kit, (*kit).data_grid.set_column_read_only(grid, 1u, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "read_only is 0 or 1");
    gf_kit_status(kit, (*kit).data_grid.set_column_hidden(grid, 1u, 0u), GF_STATUS_OK, "a shown column stays shown");
    gf_kit_status(kit, (*kit).data_grid.set_column_hidden(grid, 1u, 1u), GF_STATUS_UNSUPPORTED_CAPABILITY,
                  "hiding a column is not supported yet");
    gf_kit_status(kit, (*kit).data_grid.set_column_width(grid, 7u, 90.0), GF_STATUS_NOT_FOUND,
                  "a setting of a column past the columns is not found");
    gf_kit_status(kit, (*kit).data_grid.column_at(grid, 1u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the changed column");
    gf_kit_check(kit, gf_kit_view_equals(read.header_text, "MHz") && read.width == 90.0 && read.read_only == 1u,
                 "the settings read back");
}

static void rows(gf_kit* kit, gf_object grid) {
    const gf_value alpha_cells[3] = {text_cell("Alpha"), number_cell(7.1), boolean_cell(1u)};
    const gf_value bravo_cells[2] = {text_cell("Bravo"), number_cell(14.2)};
    gf_data_grid_row batch[2] = {row_record("alpha", alpha_cells, 3u), row_record("bravo", bravo_cells, 2u)};
    uint64_t first = 9u;
    gf_kit_status(kit, (*kit).data_grid.append_rows(grid, batch, 2u, &first), GF_STATUS_OK, "append two rows");
    gf_kit_check(kit, first == 0u, "the first appended row is at 0");

    const gf_value charlie_cells[1] = {text_cell("Charlie")};
    gf_data_grid_row charlie = row_record("charlie", charlie_cells, 1u);
    gf_kit_status(kit, (*kit).data_grid.insert_rows(grid, 1u, &charlie, 1u), GF_STATUS_OK, "insert a row");
    gf_kit_check(kit, row_id_is(kit, grid, 1u, "charlie") && row_id_is(kit, grid, 2u, "bravo"),
                 "the insertion moves the later rows");

    gf_data_grid_row repeated[2] = {row_record("delta", charlie_cells, 1u), row_record("alpha", charlie_cells, 1u)};
    gf_kit_status(kit, (*kit).data_grid.insert_rows(grid, 0u, repeated, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "rows with a present ID are refused");
    uint64_t position = 9u;
    gf_kit_status(kit, (*kit).data_grid.row_position(grid, gf_kit_text("delta"), &position), GF_STATUS_NOT_FOUND,
                  "a refused insertion adds none of its rows");
    const gf_value too_many[5] = {text_cell("a"), text_cell("b"), text_cell("c"), text_cell("d"), text_cell("e")};
    gf_data_grid_row wide = row_record("wide", too_many, 5u);
    gf_kit_status(kit, (*kit).data_grid.append_rows(grid, &wide, 1u, &first), GF_STATUS_INVALID_ARGUMENT,
                  "more values than columns are refused");
    gf_kit_status(kit, (*kit).data_grid.insert_rows(grid, 9u, &charlie, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "an insertion past the rows is refused");

    uint64_t count = 0u;
    int64_t new_row = -2;
    gf_kit_status(kit, (*kit).data_grid.row_count(grid, &count, &new_row), GF_STATUS_OK, "count the rows");
    gf_kit_check(kit, (new_row == -1 && count == 3u) || (new_row == 3 && count == 4u),
                 "three rows, and the new row after them when it is shown");
    gf_kit_status(kit, (*kit).data_grid.row_position(grid, gf_kit_text("bravo"), &position), GF_STATUS_OK,
                  "find a row by ID");
    gf_kit_check(kit, position == 2u, "bravo is at 2");
    char text[16];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).data_grid.row_id(grid, 40u, text, sizeof(text), &required), GF_STATUS_NOT_FOUND,
                  "a position past the rows is not found");

    const gf_value echo_cells[1] = {text_cell("Echo")};
    gf_data_grid_row echo = row_record("echo", echo_cells, 1u);
    gf_kit_status(kit, (*kit).data_grid.replace_row(grid, 1u, &echo), GF_STATUS_OK, "replace a row");
    gf_kit_check(kit, row_id_is(kit, grid, 1u, "echo") && cell_text_is(kit, grid, "echo", 0u, "Echo"),
                 "the replacement takes the row's place and ID");
    gf_kit_status(kit, (*kit).data_grid.erase_rows(grid, 1u, 1u), GF_STATUS_OK, "erase a row");
    gf_kit_status(kit, (*kit).data_grid.erase_rows(grid, 1u, 5u), GF_STATUS_INVALID_ARGUMENT,
                  "an erasure past the rows is refused");
    gf_kit_check(kit, row_id_is(kit, grid, 1u, "bravo"), "the erasure moves the later rows up");
}

static void cells(gf_kit* kit, gf_object grid) {
    gf_kit_recorder changed;
    memset(&changed, 0, sizeof(changed));
    changed.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&changed);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(grid, gf_kit_text("cell_value_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to cell value changes");

    gf_kit_check(kit, cell_text_is(kit, grid, "alpha", 0u, "Alpha"), "a cell reads its value");
    gf_value value = blank();
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).data_grid.cell_value(grid, gf_kit_text("alpha"), 1u, GF_VALUE_KIND_TEXT, &value, text,
                                                   sizeof(text), &required),
                  GF_STATUS_OK, "read a number cell as text");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_TEXT && required > 0u, "the number converts to text");
    gf_kit_status(kit, (*kit).data_grid.cell_value(grid, gf_kit_text("alpha"), 2u, GF_VALUE_KIND_POINT, &value, text,
                                                   sizeof(text), &required),
                  GF_STATUS_INVALID_ARGUMENT, "a value with no form of the kind is refused");
    gf_kit_status(kit, (*kit).data_grid.cell_value(grid, gf_kit_text("nobody"), 0u, GF_VALUE_KIND_NULL, &value, text,
                                                   sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "an unknown row ID is not found");
    gf_kit_status(kit, (*kit).data_grid.cell_value(grid, gf_kit_text("alpha"), 8u, GF_VALUE_KIND_NULL, &value, text,
                                                   sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a column past the columns is not found");

    const gf_value renamed = text_cell("Alpha one");
    uint32_t accepted = 9u;
    gf_kit_status(kit, (*kit).data_grid.set_cell_value(grid, gf_kit_text("alpha"), 0u, &renamed, &accepted),
                  GF_STATUS_OK, "set a cell");
    gf_kit_check(kit, accepted == 1u && cell_text_is(kit, grid, "alpha", 0u, "Alpha one"), "the cell holds the value");
    gf_kit_check(kit, changed.invoked == 1u, "cell_value_changed is raised once");
    gf_kit_status(kit, (*kit).data_grid.cell_text(grid, gf_kit_text("alpha"), 0u, text, sizeof(text), &required),
                  GF_STATUS_OK, "read a cell's text");
    gf_kit_check(kit, required == 9u && memcmp(text, "Alpha one", 9u) == 0, "the cell shows its text");

    /* The bulk read: column 0 of the two rows, into caller storage. */
    gf_value values[2] = {blank(), blank()};
    gf_kit_status(kit, (*kit).data_grid.read_cells(grid, 0u, 0u, 2u, values, text, 4u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a short buffer is refused");
    gf_kit_check(kit, required == 14u && values[0].kind == GF_VALUE_KIND_NULL, "the size is given, no value written");
    gf_kit_status(kit, (*kit).data_grid.read_cells(grid, 0u, 0u, 2u, values, text, sizeof(text), &required),
                  GF_STATUS_OK, "read a column's cells");
    gf_kit_check(kit, gf_kit_view_equals(values[0].text, "Alpha one") && gf_kit_view_equals(values[1].text, "Bravo"),
                 "each value's text is in the caller's buffer");
    gf_kit_status(kit, (*kit).data_grid.read_cells(grid, 0u, 1u, 9u, values, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a range past the rows is not found");
    values[1].struct_size = 8u;
    gf_kit_status(kit, (*kit).data_grid.read_cells(grid, 0u, 0u, 2u, values, text, sizeof(text), &required),
                  GF_STATUS_INVALID_ARGUMENT, "every element repeats the array's stride");

    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "end the subscription");
}

static void selection_and_edit(gf_kit* kit, gf_object grid) {
    gf_kit_recorder current;
    memset(&current, 0, sizeof(current));
    current.kit = kit;
    gf_kit_recorder selection;
    memset(&selection, 0, sizeof(selection));
    selection.kit = kit;
    gf_kit_recorder begun;
    memset(&begun, 0, sizeof(begun));
    begun.kit = kit;
    const gf_callback current_callback = gf_kit_recording_callback(&current);
    const gf_callback selection_callback = gf_kit_recording_callback(&selection);
    const gf_callback begun_callback = gf_kit_recording_callback(&begun);
    gf_subscription subscriptions[3] = {0u, 0u, 0u};
    gf_kit_status(kit, (*kit).events.subscribe(grid, gf_kit_text("current_cell_changed"), &current_callback,
                                               &subscriptions[0]),
                  GF_STATUS_OK, "subscribe to current cell changes");
    gf_kit_status(kit, (*kit).events.subscribe(grid, gf_kit_text("selection_changed"), &selection_callback,
                                               &subscriptions[1]),
                  GF_STATUS_OK, "subscribe to selection changes");
    gf_kit_status(kit, (*kit).events.subscribe(grid, gf_kit_text("cell_begin_edit"), &begun_callback,
                                               &subscriptions[2]),
                  GF_STATUS_OK, "subscribe to edits beginning");

    uint32_t moved = 9u;
    gf_kit_status(kit, (*kit).data_grid.set_current_cell(grid, gf_kit_text("bravo"), 0u, &moved), GF_STATUS_OK,
                  "move the current cell");
    uint32_t present = 9u;
    uint64_t row = 9u;
    uint64_t column = 9u;
    gf_kit_status(kit, (*kit).data_grid.current_cell(grid, &present, &row, &column), GF_STATUS_OK,
                  "read the current cell");
    gf_kit_check(kit, moved == 1u && present == 1u && row == 1u && column == 0u, "bravo's first cell is current");
    gf_kit_check(kit, current.invoked >= 1u, "current_cell_changed is raised");
    gf_kit_status(kit, (*kit).data_grid.set_current_cell(grid, gf_kit_text("nobody"), 0u, &moved),
                  GF_STATUS_NOT_FOUND, "an unknown row cannot be current");

    uint32_t began = 9u;
    gf_kit_status(kit, (*kit).data_grid.begin_edit(grid, 1u, &began), GF_STATUS_OK, "begin an edit");
    gf_kit_check(kit, began == 1u && begun.invoked == 1u, "the text cell enters edit, raising cell_begin_edit");
    gf_kit_status(kit, (*kit).data_grid.cancel_edit(grid), GF_STATUS_OK, "discard the edit");
    uint32_t committed = 9u;
    gf_kit_status(kit, (*kit).data_grid.end_edit(grid, &committed), GF_STATUS_OK, "end with no edit");
    gf_kit_check(kit, committed == 1u, "no edit in progress commits");
    gf_kit_status(kit, (*kit).data_grid.begin_edit(grid, 2u, &began), GF_STATUS_INVALID_ARGUMENT,
                  "select_all is 0 or 1");

    gf_kit_status(kit, (*kit).data_grid.clear_selection(grid), GF_STATUS_OK, "clear the selection");
    const uint32_t before = selection.invoked;
    gf_kit_status(kit, (*kit).data_grid.set_row_selected(grid, gf_kit_text("alpha"), 1u), GF_STATUS_OK,
                  "select a row");
    gf_kit_check(kit, selection.invoked == before + 1u, "selection_changed is raised once");
    uint64_t positions[4] = {9u, 9u, 9u, 9u};
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).data_grid.selected_rows(grid, positions, 0u, &count), GF_STATUS_BUFFER_TOO_SMALL,
                  "a short array is refused");
    gf_kit_check(kit, count == 1u && positions[0] == 9u, "the count is given, no position written");
    gf_kit_status(kit, (*kit).data_grid.selected_rows(grid, positions, 4u, &count), GF_STATUS_OK,
                  "read the selected rows");
    gf_kit_check(kit, count == 1u && positions[0] == 0u, "alpha is selected");
    gf_kit_status(kit, (*kit).data_grid.set_cell_selected(grid, gf_kit_text("bravo"), 1u, 1u), GF_STATUS_OK,
                  "select a cell too");
    gf_data_grid_cell selected_cells[16];
    gf_kit_status(kit, (*kit).data_grid.selected_cells(grid, selected_cells, 16u, &count), GF_STATUS_OK,
                  "read the selected cells");
    gf_kit_check(kit, count == 5u && selected_cells[4].row == 1u && selected_cells[4].column == 1u,
                 "alpha's four cells, then bravo's one, by row then column");
    gf_kit_status(kit, (*kit).data_grid.set_row_selected(grid, gf_kit_text("alpha"), 2u), GF_STATUS_INVALID_ARGUMENT,
                  "selected is 0 or 1");
    gf_kit_status(kit, (*kit).data_grid.select_all(grid), GF_STATUS_OK, "select every cell");
    gf_kit_status(kit, (*kit).data_grid.selected_cells(grid, selected_cells, 16u, &count), GF_STATUS_OK,
                  "read the whole selection");
    gf_kit_check(kit, count >= 8u, "every cell of the rows is selected");

    for (uint32_t index = 0u; index < 3u; ++index) {
        gf_kit_status(kit, (*kit).events.disconnect(subscriptions[index]), GF_STATUS_OK, "end a subscription");
    }
}

void gf_kit_data_grid(gf_kit* kit) {
    const gf_object grid = gf_kit_create(kit, "data_grid_view", "kit.grid");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(grid, &window), GF_STATUS_OK, "make the grid's window");
    const gf_object label = gf_kit_create(kit, "label", "kit.grid.label");
    uint64_t count = 9u;
    gf_kit_status(kit, (*kit).data_grid.column_count(label, &count), GF_STATUS_WRONG_HANDLE_KIND,
                  "a label is not a data grid view");

    columns(kit, grid);
    rows(kit, grid);
    cells(kit, grid);
    selection_and_edit(kit, grid);

    gf_kit_status(kit, (*kit).data_grid.remove_column(grid, 3u), GF_STATUS_OK, "remove a column");
    gf_kit_status(kit, (*kit).data_grid.remove_column(grid, 3u), GF_STATUS_NOT_FOUND,
                  "a column past the columns is not removed");
    gf_kit_status(kit, (*kit).data_grid.column_count(grid, &count), GF_STATUS_OK, "count the columns again");
    gf_kit_check(kit, count == 3u, "three columns remain");
    gf_kit_status(kit, (*kit).data_grid.clear_rows(grid), GF_STATUS_OK, "clear the rows");
    int64_t new_row = -2;
    gf_kit_status(kit, (*kit).data_grid.row_count(grid, &count, &new_row), GF_STATUS_OK, "count the cleared rows");
    gf_kit_check(kit, (new_row == -1 && count == 0u) || (new_row == 0 && count == 1u),
                 "no rows remain but the new row, when it is shown");

    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the grid's window");
    gf_kit_status(kit, (*kit).objects.release(grid), GF_STATUS_OK, "release the grid");
}
