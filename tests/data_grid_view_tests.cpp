// DataGridView (stage W): columns and unbound rows, layout and published
// parts, selection and keyboard navigation, text editing in the hosted
// editor, check box, button, combo box and image cells, virtual rows on stable row
// IDs with a provider asked by ID, row identity kept across inserts, removals
// and sorts in every row source, sorting, header clicks and column resizing,
// BindingSource rows, scrolling and semantics; and the editing lifecycle
// (batch 1): data errors, validation, dirty state, edit modes, enter and
// leave events, the new row, deleting rows and error text.

#include "gui_forms/gui_forms.hpp"

#include "../src/controls/text_block/line_wrapping.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Positions and addresses, spelled briefly in the tests.
DataGridViewRowPosition row_at(std::size_t position) {
    const DataGridViewRowPosition row{position};
    return row;
}

DataGridViewCellAddress cell_at(std::size_t row, std::size_t column) {
    const DataGridViewCellAddress cell{DataGridViewRowPosition{row}, column};
    return cell;
}

DataGridViewRowId id(std::string_view text) {
    const DataGridViewRowId identity{std::string(text)};
    return identity;
}

// Counts drawn text; paint_part draws every label and content through it.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts;
};

struct Counter final {
    int* count{};
    void operator()() const { ++*count; }
};

struct RecordCurrent final {
    std::vector<std::optional<DataGridViewCellAddress>>* seen{};
    void operator()(std::optional<DataGridViewCellAddress> cell) const { (*seen).push_back(cell); }
};

struct RecordCell final {
    std::vector<DataGridViewCellEvent>* seen{};
    void operator()(const DataGridViewCellEvent& event) const { (*seen).push_back(event); }
};

struct RecordIndex final {
    std::vector<std::size_t>* seen{};
    void operator()(std::size_t index) const { (*seen).push_back(index); }
};

struct CancelBeginEdit final {
    bool* cancel{};
    void operator()(DataGridViewCellCancelEvent& event) const { event.cancel = *cancel; }
};

// Refuses any text that is "bad".
struct RefuseBad final {
    int* calls{};
    void operator()(DataGridViewCellValidatingEvent& event) const {
        ++*calls;
        if (event.formatted_value == "bad") event.cancel = true;
    }
};

// Rows whose values are computed from their IDs; records every ID the grid
// asks for. Column 0 is the ID with a prefix, column 1 whether the ID has an
// even length.
class CountingProvider final : public DataGridViewCellValueProvider {
public:
    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        ++asked;
        asked_ids.insert(row.value);
        if (column == 1U) return BindingValue{(row.value.size() % 2U) == 0U};
        return BindingValue{"label " + row.value};
    }
    bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) override {
        ++pushed;
        if (refuse) return false;
        last_row = row;
        last_column = column;
        last_value = value;
        return true;
    }

    mutable std::size_t asked{};
    mutable std::unordered_set<std::string> asked_ids{};
    std::size_t pushed{};
    bool refuse{};
    DataGridViewRowId last_row{};
    std::size_t last_column{};
    BindingValue last_value{};
};

// IDs "v<first>" to "v<first + count - 1>", in that order.
std::vector<DataGridViewRowId> virtual_order(std::size_t count, std::size_t first = 0U) {
    std::vector<DataGridViewRowId> order;
    order.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        order.push_back(id("v" + std::to_string(first + index)));
    }
    return order;
}

std::vector<DataGridViewColumn> sample_columns() {
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "name", .header_text = "Name", .width = 100.0});
    columns.push_back({.name = "active", .header_text = "On", .kind = DataGridViewColumnKind::check_box,
                       .width = 50.0});
    columns.push_back({.name = "mode", .header_text = "Mode", .kind = DataGridViewColumnKind::combo_box,
                       .width = 90.0, .items = {"AM", "FM", "USB"}});
    columns.push_back({.name = "tune", .header_text = "", .kind = DataGridViewColumnKind::button,
                       .width = 60.0, .button_text = "Tune"});
    return columns;
}

std::vector<DataGridViewColumn> virtual_columns() {
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "label", .header_text = "Label", .width = 120.0});
    columns.push_back({.name = "even", .header_text = "Even", .kind = DataGridViewColumnKind::check_box,
                       .width = 50.0});
    return columns;
}

bool has_action(const SemanticNode& node, SemanticAction action) {
    for (const SemanticAction offered : node.actions) {
        if (offered == action) return true;
    }
    return false;
}

std::shared_ptr<DataGridView> make_grid(const char* identity) {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId(std::string(identity)));
    (*grid).set_columns(sample_columns());
    (*grid).rows.append({.stable_id = {"alpha"}, .cells = {BindingValue{std::string("Alpha")}, BindingValue{true},
                                                           BindingValue{std::string("FM")}}});
    (*grid).rows.append({.stable_id = {"bravo"}, .cells = {BindingValue{std::string("Bravo")}, BindingValue{false},
                                                           BindingValue{std::string("AM")}}});
    (*grid).rows.append({.stable_id = {"charlie"}, .cells = {BindingValue{std::string("Charlie")}}});
    // The tests count the fixture's three rows, so the new row (on by
    // default) is turned off here; new_row_unbound turns it back on.
    (*grid).allow_user_to_add_rows = false;
    return grid;
}

Point centre(const DataGridView& grid, Rect local) {
    const Rect absolute = grid.absolute_bounds();
    const Point point{absolute.x + local.x + local.width * 0.5, absolute.y + local.y + local.height * 0.5};
    return point;
}

// The returned pointer borrows from `cells`, so the vector must outlive it:
// realized_cells() returns a new vector, and a pointer into that temporary
// dangles at the end of the statement. The deleted overload refuses it.
const DataGridView::Cell* find_cell(const std::vector<DataGridView::Cell>& cells, std::size_t row,
                                    std::size_t column) {
    for (const DataGridView::Cell& cell : cells) {
        if (cell.address.row.value == row && cell.address.column == column) return &cell;
    }
    return nullptr;
}
const DataGridView::Cell* find_cell(std::vector<DataGridView::Cell>&& cells, std::size_t row,
                                    std::size_t column) = delete;

// The semantic row node that publishes focused, or null. It borrows from
// `nodes`, as find_cell borrows from its cells; temporaries are refused.
const SemanticNode* focused_row(const std::vector<SemanticNode>& nodes) {
    for (const SemanticNode& node : nodes) {
        if (node.role == SemanticRole::row && has_semantic_state(node.states, SemanticState::focused)) {
            return &node;
        }
    }
    return nullptr;
}
const SemanticNode* focused_row(std::vector<SemanticNode>&& nodes) = delete;

void click(Window& window, Point point, Modifier modifiers = Modifier::none, std::uint32_t clicks = 1U) {
    PointerEvent down{PointerAction::down, PointerButton::primary, point};
    down.modifiers = modifiers;
    down.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent up{PointerAction::up, PointerButton::primary, point};
    up.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(up));
}

bool key(Window& window, std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

template <typename Exception, typename Action>
bool throws(Action action) {
    try {
        action();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

struct AddDottedColumn final {
    DataGridView* grid{};
    void operator()() const { (*grid).add_column({.name = "a.b"}); }
};
struct AddDuplicateRow final {
    DataGridView* grid{};
    void operator()() const { (*grid).rows.append({.stable_id = {"alpha"}}); }
     };
     struct AddItemsToText final {
         DataGridView* grid{};
         void operator()() const { (*grid).add_column({.name = "extra", .items = {"x"}}); }
     };
     struct NarrowColumn final {
         DataGridView* grid{};
         void operator()() const { (*grid).set_column_width(0U, 5.0); }
     };
     struct SetOrder final {
         DataGridView* grid{};
         std::vector<DataGridViewRowId> order{};
         void operator()() const { (*grid).set_virtual_row_order(order); }
     };
     struct RemoveNewRow final {
         DataGridView* grid{};
         void operator()() const { (*grid).rows.erase((*(*grid).new_row_index()).value, 1U); }
     };
     struct SetCount final {
         DataGridView* grid{};
         std::size_t count{};
         void operator()() const { (*grid).set_row_count(count); }
     };
     struct SetSortMode final {
         DataGridView* grid{};
         DataGridViewColumnSortMode mode{};
         void operator()() const { (*grid).set_column_sort_mode(0U, mode); }
     };
     struct SetVirtualRows final {
         DataGridView* grid{};
         std::shared_ptr<DataGridViewCellValueProvider> provider{};
         std::vector<DataGridViewRowId> order{};
         void operator()() const { (*grid).set_virtual_rows(provider, order); }
     };
     
     std::shared_ptr<Control> as_control(const std::shared_ptr<TextBox>& editor) {
         const std::shared_ptr<Control> control = editor;
         return control;
     }
     
     void columns_and_rows() {
         std::shared_ptr<DataGridView> grid = make_grid("grid.model");
    const ControlAnatomy& anatomy = (*grid).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "data_grid_view" && anatomy.item_systems.size() == 3U,
            "DataGridView publishes a valid anatomy with row, cell and column item systems");
    require((*grid).columns().size() == 4U && (*grid).row_count() == 3U, "columns and rows are kept");
    require((*grid).cell_text(row_at(0U), 0U) == "Alpha" && (*grid).cell_value(row_at(0U), 1U) == BindingValue{true},
            "cells keep their binding values");
    require((*grid).cell_text(row_at(2U), 3U) == "Tune" && (*grid).cell_value(row_at(2U), 2U) == BindingValue{},
            "short rows are padded, and an empty button cell shows its column's text");
    require((*grid).row_position(id("bravo")) == row_at(1U) && (*grid).row_id(row_at(2U)) == id("charlie"),
            "rows keep their stable IDs");
    (*grid).rows.append(DataGridViewRow{});
    const DataGridViewRowPosition generated{(*grid).rows.read().size() - 1U};
    require((*grid).row_id(generated).value.rfind("row.", 0U) == 0U, "an empty row ID is generated");
    (*grid).rows.insert(0U, {.stable_id = {"zulu"}});
    require((*grid).row_position(id("alpha")) == row_at(1U), "inserting moves later rows");
    (*grid).rows.erase(0U, 1U);
    require((*grid).row_position(id("alpha")) == row_at(0U) && (*grid).row_count() == 4U,
            "removing moves them back");
    require(!(*grid).row_position(id("zulu")), "a removed row's ID is gone");
    (*grid).set_row_count(6U);
    require((*grid).row_count() == 6U, "an unbound row count grows with generated rows");
    (*grid).set_row_count(3U);
    require((*grid).row_count() == 3U && (*grid).row_position(id("charlie")) == row_at(2U),
            "and is cut from the end");
    std::shared_ptr<DataGridView> fresh = make_control<DataGridView>(StableId("grid.model.fresh"));
    (*fresh).set_columns(sample_columns());
    (*fresh).rows = {{.stable_id = {"row.1"}}, {}};
    require((*fresh).row_id(row_at(1U)).value != "row.1" && !(*fresh).row_id(row_at(1U)).value.empty(),
            "a generated row ID skips the IDs the rows already use");
    require(throws<std::invalid_argument>(AddDottedColumn{grid.get()}), "a column name with '.' is refused");
    require(throws<std::invalid_argument>(AddDuplicateRow{grid.get()}), "a duplicate row ID is refused");
    require(throws<std::invalid_argument>(AddItemsToText{grid.get()}), "items on a text column are refused");
    require(throws<std::invalid_argument>(NarrowColumn{grid.get()}), "a width below the minimum is refused");
    bool past_end = false;
    try {
        static_cast<void>((*grid).cell_value(row_at(9U), 0U));
    } catch (const std::out_of_range&) {
        past_end = true;
    }
    require(past_end, "a cell past the rows is refused");
    bool id_past_end = false;
    try {
        static_cast<void>((*grid).row_id(row_at(3U)));
    } catch (const std::out_of_range&) {
        id_past_end = true;
    }
    require(id_past_end, "a row ID past the rows is refused");
}

void layout_and_parts() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.parts");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    const DataGridView::Parts parts = (*grid).parts();
    require(parts.column_header_band.present && parts.row_header_band.present && parts.top_left_header.present,
            "both header bands and the corner header are present");
    require(!parts.vertical_scroll_bar.present && !parts.horizontal_scroll_bar.present,
            "content that fits shows no scroll bars");
    require(!parts.editor.present, "the editor is absent outside an edit");
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    require(rows.size() == 3U && cells.size() == 12U && columns.size() == 4U,
            "every visible row, cell and column header is realized");
    require(rows[1].position == row_at(1U) && rows[1].stable_id == "bravo",
            "a realized row carries its position and its row ID");
    require((*grid).realized_item_count() == 19U, "the generic path counts rows, cells and headers");
    require((*grid).realized_item_system(0U).value == 0U && (*grid).realized_item_system(3U).value == 1U &&
                (*grid).realized_item_system(18U).value == 2U && (*grid).realized_item_stable_id(18U) == "grid.parts.column.tune",
            "realized items are rows, then cells, then column headers");
    const DataGridView::Cell* text = find_cell(cells, 0U, 0U);
    const DataGridView::Cell* check = find_cell(cells, 0U, 1U);
    const DataGridView::Cell* combo = find_cell(cells, 0U, 2U);
    const DataGridView::Cell* button = find_cell(cells, 0U, 3U);
    require(text != nullptr && (*text).parts.cell_content.present && !(*text).parts.cell_check_well.present &&
                (*text).stable_id == "alpha.name",
            "a text cell shows content and is named row.column");
    require(check != nullptr && (*check).parts.cell_check_well.present && (*check).states.checked &&
                !(*check).parts.cell_content.present,
            "a check box cell shows a well and publishes checked");
    require(combo != nullptr && (*combo).parts.cell_drop_down.present && (*combo).parts.cell_drop_down_glyph.present &&
                (*combo).parts.cell_content.present,
            "a combo box cell shows its value beside a drop-down actuator");
    require(button != nullptr && (*button).parts.cell_button_face.present && (*button).parts.cell_button_label.present,
            "a button cell shows a face and a label");
    require((*text).parts.cell_vertical_rule.present && (*text).parts.cell_horizontal_rule.present,
            "cells publish their grid lines");
    require(columns[0].parts.column_label.present && columns[0].parts.column_divider.present &&
                columns[0].parts.column_grip.present && !columns[0].parts.sort_glyph.present,
            "a column header publishes its label, divider and grip, and no sort glyph unsorted");
    require(rows[0].parts.row_header.present && !rows[0].parts.row_glyph.present,
            "a row header has no glyph until its row is current");

    CountingPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    bool painted_alpha = false;
    bool painted_tune = false;
    bool painted_header = false;
    for (const std::string& drawn : painter.texts) {
        if (drawn == "Alpha") painted_alpha = true;
        if (drawn == "Tune") painted_tune = true;
        if (drawn == "Mode") painted_header = true;
    }
    require(painted_alpha && painted_tune && painted_header,
            "cell content, button labels and header labels are painted by role");

    (*grid).column_headers_visible = false;
    (*grid).row_headers_visible = false;
    window.perform_layout();
    require(!(*grid).parts().column_header_band.present && !(*grid).parts().row_header_band.present &&
                (*grid).realized_columns().empty(),
            "hidden headers publish no bands and no column items");
}

void selection_and_navigation() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.select");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    int selection_changes = 0;
    std::vector<std::optional<DataGridViewCellAddress>> currents;
    SubscriptionToken selection_token = (*grid).selection_changed().subscribe(Counter{&selection_changes});
    SubscriptionToken current_token = (*grid).current_cell_changed().subscribe(RecordCurrent{&currents});
    require(window.request_focus(grid), "the grid takes focus");
    require((*grid).current_cell() == cell_at(0U, 0U) && (*grid).is_cell_selected(row_at(0U), 0U),
            "focus makes the first cell current and selected");
    require((*grid).current_row_id() == id("alpha"), "the current cell names its row's ID");
    require(currents.size() == 1U && selection_changes == 1, "the first current cell is reported once");
    require(key(window, PhysicalKey::right) && (*grid).current_cell() == cell_at(0U, 1U),
            "Right moves to the next column");
    require(key(window, PhysicalKey::down, Modifier::shift) &&
                (*grid).current_cell() == cell_at(1U, 1U) && (*grid).selected_cells().size() == 2U,
            "Shift+Down extends the selection from the anchor");
    require(key(window, PhysicalKey::end, Modifier::control) &&
                (*grid).current_cell() == cell_at(2U, 3U) && (*grid).selected_cells().size() == 1U,
            "Ctrl+End moves to the last cell and selects it alone");
    require(key(window, PhysicalKey::home) && (*grid).current_cell() == cell_at(2U, 0U),
            "Home moves to the row's first cell");
    require(key(window, PhysicalKey::tab, Modifier::shift) && (*grid).current_cell() == cell_at(1U, 3U),
            "Shift+Tab from a row's first cell moves to the previous row's last cell");
    require(key(window, PhysicalKey::tab) && (*grid).current_cell() == cell_at(2U, 0U),
            "Tab wraps to the next row");
    require(key(window, PhysicalKey::page_up) && (*grid).current_cell() == cell_at(0U, 0U),
            "Page Up moves up by a page");
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows[0].states.current && rows[0].parts.row_glyph.present, "the current row shows its glyph");

    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    click(window, centre(*grid, (*find_cell(cells, 2U, 0U)).parts.cell.bounds), Modifier::control);
    require((*grid).is_cell_selected(row_at(0U), 0U) && (*grid).is_cell_selected(row_at(2U), 0U),
            "Ctrl+click adds a cell to the selection");
    click(window, centre(*grid, (*find_cell(cells, 2U, 0U)).parts.cell.bounds), Modifier::control);
    require(!(*grid).is_cell_selected(row_at(2U), 0U), "Ctrl+click again removes it");

    (*grid).selection_mode = DataGridViewSelectionMode::full_row_select;
    click(window, centre(*grid, (*find_cell(cells, 1U, 0U)).parts.cell.bounds));
    require((*grid).selected_rows().size() == 1U && (*grid).selected_rows()[0] == row_at(1U) &&
                (*grid).is_cell_selected(row_at(1U), 3U) && (*grid).realized_rows()[1].states.selected,
            "full-row selection selects the clicked cell's row");
    require(key(window, PhysicalKey::down, Modifier::shift) && (*grid).selected_rows().size() == 2U,
            "Shift+Down extends a row selection");
    (*grid).multi_select = false;
    require((*grid).selected_rows().size() == 1U, "turning multi-select off keeps the current row alone");
    (*grid).select_all();
    require((*grid).selected_rows().size() == 1U, "select_all needs multi-select");
    (*grid).multi_select = true;
    (*grid).select_all();
    require((*grid).selected_rows().size() == 3U, "select_all selects every row");
    const int before = selection_changes;
    (*grid).clear_selection();
    require(selection_changes == before + 1 && (*grid).selected_cells().empty(),
            "clear_selection raises selection_changed once");
    require(!(*grid).is_row_selected(row_at(7U)) && !(*grid).is_cell_selected(row_at(7U), 0U),
            "a position past the rows is not selected");
}

void text_editing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.edit");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    std::vector<DataGridViewCellEvent> ended;
    std::vector<DataGridViewCellEvent> changed;
    SubscriptionToken end_token = (*grid).cell_end_edit().subscribe(RecordCell{&ended});
    SubscriptionToken change_token = (*grid).cell_value_changed().subscribe(RecordCell{&changed});
    require(window.request_focus(grid), "the grid takes focus");

    require(key(window, PhysicalKey::f2) && (*grid).is_current_cell_in_edit_mode(), "F2 begins an edit");
    window.perform_layout();
    require((*grid).parts().editor.present && window.focused_control() == as_control((*grid).editing_control()),
            "the editor covers the cell and takes focus");
    require((*(*grid).editing_control()).text.read() == "Alpha", "the editor starts with the cell's text");
    require(window.dispatch_text({"Delta"}) && (*(*grid).editing_control()).text.read() == "Delta",
            "typing replaces the selected text");
    require(key(window, PhysicalKey::enter), "Enter commits");
    require(!(*grid).is_current_cell_in_edit_mode() && (*grid).cell_text(row_at(0U), 0U) == "Delta",
            "the committed text is the cell's value");
    require(changed.size() == 1U && ended.size() == 1U && ended[0].row == row_at(0U) &&
                ended[0].row_id == id("alpha"),
            "a commit raises cell_value_changed then cell_end_edit, with the row's ID");
    const std::shared_ptr<Control> grid_control = grid;
    require((*grid).current_cell() == cell_at(1U, 0U) && window.focused_control() == grid_control,
            "Enter moves to the cell below and focus returns to the grid");

    require(window.dispatch_text({"X"}) && (*grid).is_current_cell_in_edit_mode() &&
                (*(*grid).editing_control()).text.read() == "X",
            "typing on a text cell begins an edit with the typed text");
    require(key(window, PhysicalKey::escape) && !(*grid).is_current_cell_in_edit_mode() &&
                (*grid).cell_text(row_at(1U), 0U) == "Bravo" && ended.size() == 2U && changed.size() == 1U,
            "Escape discards the edit");

    bool cancel = true;
    SubscriptionToken begin_token = (*grid).cell_begin_edit().subscribe(CancelBeginEdit{&cancel});
    require(!(*grid).begin_edit() && !(*grid).is_current_cell_in_edit_mode(),
            "a cancelled cell_begin_edit keeps the cell out of edit");
    cancel = false;

    int validations = 0;
    SubscriptionToken validate_token = (*grid).cell_validating().subscribe(RefuseBad{&validations});
    require((*grid).begin_edit(true), "an edit begins");
    require(window.dispatch_text({"bad"}), "the editor takes typing");
    require(key(window, PhysicalKey::tab) && (*grid).is_current_cell_in_edit_mode() && validations == 1,
            "a refused value keeps the cell in edit");
    const std::vector<DataGridView::Cell> refused_cells = (*grid).realized_cells();
    const DataGridView::Cell* refused = find_cell(refused_cells, 1U, 0U);
    require(refused != nullptr && (*refused).states.invalid, "the refused cell publishes invalid");
    (*(*grid).editing_control()).text = "Echo";
    require(key(window, PhysicalKey::tab) && !(*grid).is_current_cell_in_edit_mode() &&
                (*grid).cell_text(row_at(1U), 0U) == "Echo" && (*grid).current_cell() == cell_at(1U, 1U),
            "Tab commits and moves to the next cell");

    // A number column converts the typed text and refuses what is not a number.
    (*grid).add_column({.name = "frequency", .header_text = "MHz", .width = 60.0,
                        .value_kind = BindingValueKind::number});
    require((*grid).set_current_cell(cell_at(0U, 4U)) && (*grid).begin_edit(), "edit a number");
    (*(*grid).editing_control()).text = "abc";
    require(!(*grid).end_edit() && (*grid).is_current_cell_in_edit_mode(), "text that is not a number is refused");
    (*(*grid).editing_control()).text = "7.25";
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 4U) == BindingValue{7.25} &&
                (*grid).cell_text(row_at(0U), 4U) == "7.25",
            "a number commits as a number and shows its shortest text");

    // Read-only cells do not edit; a double click edits a text cell.
    (*grid).set_column_read_only(0U, true);
    require((*grid).set_current_cell(cell_at(2U, 0U)) && !(*grid).begin_edit(),
            "a read-only column does not edit");
    (*grid).set_column_read_only(0U, false);
    window.perform_layout();
    const std::vector<DataGridView::Cell> target_cells = (*grid).realized_cells();
    const DataGridView::Cell* target = find_cell(target_cells, 2U, 0U);
    click(window, centre(*grid, (*target).parts.cell.bounds), Modifier::none, 2U);
    require((*grid).is_current_cell_in_edit_mode(), "a double click begins an edit");
    (*grid).cancel_edit();
}

void check_button_and_combo_cells() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.kinds");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    std::vector<DataGridViewCellEvent> changed;
    std::vector<DataGridViewCellEvent> clicked;
    SubscriptionToken change_token = (*grid).cell_value_changed().subscribe(RecordCell{&changed});
    SubscriptionToken click_token = (*grid).cell_content_click().subscribe(RecordCell{&clicked});
    require(window.request_focus(grid), "the grid takes focus");

    require((*grid).set_current_cell(cell_at(0U, 1U)), "a check box cell becomes current");
    require(key(window, PhysicalKey::space) && (*grid).cell_value(row_at(0U), 1U) == BindingValue{false} &&
                changed.size() == 1U && clicked.size() == 1U,
            "Space toggles a check box cell");
    const std::vector<DataGridView::Cell> check_cells = (*grid).realized_cells();
    const DataGridView::Cell* check = find_cell(check_cells, 1U, 1U);
    click(window, centre(*grid, (*check).parts.cell_check_well.bounds));
    const std::vector<DataGridView::Cell> checked_cells = (*grid).realized_cells();
    require((*grid).cell_value(row_at(1U), 1U) == BindingValue{true} &&
                (*find_cell(checked_cells, 1U, 1U)).states.checked,
            "clicking the check well toggles and publishes checked");
    (*grid).read_only = true;
    require(key(window, PhysicalKey::space) && (*grid).cell_value(row_at(1U), 1U) == BindingValue{true},
            "a read-only grid does not toggle");
    (*grid).read_only = false;

    const std::vector<DataGridView::Cell> button_cells = (*grid).realized_cells();
    const DataGridView::Cell* button = find_cell(button_cells, 2U, 3U);
    const Point face = centre(*grid, (*button).parts.cell_button_face.bounds);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, face}),
            "the button cell takes the press");
    const std::vector<DataGridView::Cell> pressed_cells = (*grid).realized_cells();
    require((*find_cell(pressed_cells, 2U, 3U)).states.pressed, "a held button cell publishes pressed");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, face}));
    require(clicked.size() == 3U && clicked.back().row == row_at(2U) && clicked.back().column == 3U &&
                clicked.back().row_id == id("charlie"),
            "releasing on the button cell raises cell_content_click");

    require((*grid).set_current_cell(cell_at(0U, 2U)), "a combo box cell becomes current");
    require(key(window, PhysicalKey::f4) && (*grid).drop_down_open() && (*grid).is_current_cell_in_edit_mode(),
            "F4 opens the combo box cell's choices");
    const std::vector<DataGridView::Cell> open_cells = (*grid).realized_cells();
    require((*find_cell(open_cells, 0U, 2U)).states.open, "the open combo box cell publishes open");
    const std::size_t before = changed.size();
    require(key(window, PhysicalKey::down) && key(window, PhysicalKey::enter), "a choice is made by keyboard");
    require(!(*grid).drop_down_open() && !(*grid).is_current_cell_in_edit_mode() &&
                (*grid).cell_text(row_at(0U), 2U) == "FM" && changed.size() == before,
            "choosing the current value changes nothing");
    require(key(window, PhysicalKey::down, Modifier::alt) && (*grid).drop_down_open(), "Alt+Down opens the choices");
    require(key(window, PhysicalKey::end) && key(window, PhysicalKey::enter) &&
                (*grid).cell_text(row_at(0U), 2U) == "USB" && changed.size() == before + 1U,
            "a different choice becomes the cell's value");
    const std::vector<DataGridView::Cell> combo_cells = (*grid).realized_cells();
    const DataGridView::Cell* combo = find_cell(combo_cells, 1U, 2U);
    click(window, centre(*grid, (*combo).parts.cell_drop_down.bounds));
    require((*grid).drop_down_open() && (*grid).current_cell() == cell_at(1U, 2U),
            "clicking a drop-down actuator opens that cell's choices");
    (*grid).close_drop_down();
    require(!(*grid).is_current_cell_in_edit_mode(), "closing the choices ends the edit");
}

void virtual_rows() {
    std::shared_ptr<CountingProvider> provider = std::make_shared<CountingProvider>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.virtual"));
    (*grid).set_columns(virtual_columns());
    const std::vector<DataGridViewRowId> order = virtual_order(1000000U);
    (*grid).set_virtual_rows(provider, order);
    Window window(grid, {300.0, 200.0});
    window.perform_layout();
    require((*grid).row_source() == DataGridViewRowSource::virtual_rows && (*grid).row_count() == 1000000U,
            "virtual rows are counted by their order");
    require((*grid).parts().vertical_scroll_bar.present, "a million rows show a vertical scroll bar");
    const std::size_t shown = (*grid).displayed_row_end().value - (*grid).first_displayed_row().value;
    require(shown > 0U && shown < 20U, "only a window of rows is displayed");

    // Realization is bounded by the viewport: rows, their cells and headers.
    const std::vector<DataGridView::Row> realized = (*grid).realized_rows();
    require(realized.size() == shown && (*grid).realized_cells().size() == shown * 2U &&
                (*grid).realized_item_count() == shown + shown * 2U + 2U,
            "only the displayed rows and cells are realized");
    require(realized[0].stable_id == "v0" && realized[shown - 1U].stable_id == order[shown - 1U].value,
            "realized rows carry the application's row IDs in order");

    // The provider is asked by ID, and only for displayed rows.
    (*provider).asked = 0U;
    (*provider).asked_ids.clear();
    CountingPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    std::unordered_set<std::string> displayed;
    for (std::size_t position = 0; position < shown; ++position) displayed.insert(order[position].value);
    bool only_displayed = !(*provider).asked_ids.empty();
    for (const std::string& asked : (*provider).asked_ids) {
        if (displayed.count(asked) == 0U) only_displayed = false;
    }
    require(only_displayed && (*provider).asked <= shown * 2U * 3U,
            "paint asks only for the displayed rows' values, by ID");
    require((*grid).cell_text(row_at(5U), 0U) == "label v5" && (*grid).row_id(row_at(5U)) == id("v5"),
            "virtual rows take their values from the provider by ID");

    (*grid).set_first_displayed_row(row_at(500000U));
    window.perform_layout();
    (*provider).asked_ids.clear();
    CountingPainter later;
    (*grid).on_paint(later, (*grid).client_rectangle());
    bool scrolled_only = !(*provider).asked_ids.empty();
    for (const std::string& asked : (*provider).asked_ids) {
        const std::optional<DataGridViewRowPosition> position = (*grid).row_position(id(asked));
        if (!position || (*position).value < 500000U || (*position).value >= 500000U + shown) scrolled_only = false;
    }
    require(scrolled_only, "scrolled rows are asked for where they are, and no others");
    require((*grid).realized_rows()[0].stable_id == "v500000", "the realized window follows the scroll");

    require(window.request_focus(grid), "the grid takes focus");
    require((*grid).current_cell() == cell_at(500000U, 0U) && (*grid).current_row_id() == id("v500000"),
            "the first displayed cell becomes current");
    require(window.dispatch_text({"new"}) && key(window, PhysicalKey::enter), "an edit commits through the provider");
    require((*provider).pushed == 1U && (*provider).last_row == id("v500000") &&
                (*provider).last_value == BindingValue{std::string("new")},
            "the provider receives the committed value with the row's ID");
    (*provider).refuse = true;
    require((*grid).set_current_cell(cell_at(500000U, 1U)) && key(window, PhysicalKey::space) &&
                (*provider).pushed == 2U,
            "a toggle pushes to the provider");
    require(!(*grid).set_cell_value(row_at(3U), 0U, BindingValue{std::string("x")}), "a refused value reports false");
    require(key(window, PhysicalKey::end, Modifier::control) && (*grid).current_cell() == cell_at(999999U, 1U),
            "Ctrl+End moves to the last virtual row");
    window.perform_layout();
    require((*grid).displayed_row_end() == row_at(1000000U), "and scrolls it into view");
}

// Virtual rows: the application inserts, removes and sorts rows by publishing
// a new order; the grid keeps the selection, the current cell, focus and an
// edit on their row IDs.
void virtual_row_identity() {
    std::shared_ptr<CountingProvider> provider = std::make_shared<CountingProvider>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.identity"));
    (*grid).set_columns(virtual_columns());
    (*grid).set_virtual_rows(provider, virtual_order(20U));
    Window window(grid, {300.0, 200.0});
    window.perform_layout();
    require(window.request_focus(grid), "the grid takes focus");
    require((*grid).set_current_cell(cell_at(3U, 0U)), "v3 becomes current");
    (*grid).set_cell_selected(row_at(5U), 0U, true);
    require((*grid).selected_cells().size() == 2U && (*grid).current_row_id() == id("v3"),
            "v3 and v5 are selected, v3 current");
    int selection_changes = 0;
    std::vector<std::optional<DataGridViewCellAddress>> currents;
    SubscriptionToken selection_token = (*grid).selection_changed().subscribe(Counter{&selection_changes});
    SubscriptionToken current_token = (*grid).current_cell_changed().subscribe(RecordCurrent{&currents});

    // Insert two rows above.
    std::vector<DataGridViewRowId> inserted{id("n1"), id("n2")};
    const std::vector<DataGridViewRowId> base = virtual_order(20U);
    inserted.insert(inserted.end(), base.begin(), base.end());
    (*grid).set_virtual_row_order(inserted);
    window.perform_layout();
    require((*grid).row_count() == 22U && (*grid).current_cell() == cell_at(5U, 0U) &&
                (*grid).current_row_id() == id("v3"),
            "an insert above moves the current cell with its row");
    require((*grid).is_cell_selected(row_at(5U), 0U) && (*grid).is_cell_selected(row_at(7U), 0U) &&
                !(*grid).is_cell_selected(row_at(3U), 0U) && (*grid).selected_cells().size() == 2U,
            "the selection stays on its rows");
    require(currents.empty() && selection_changes == 0,
            "a moved row is not a new current cell or selection");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    const SemanticNode* focused = focused_row(nodes);
    require(focused != nullptr && (*focused).stable_id == "v3", "focus stays on the current row's ID");
    const std::vector<DataGridView::Row> realized = (*grid).realized_rows();
    std::size_t current_rows = 0U;
    for (const DataGridView::Row& row : realized) {
        if (!row.states.current) continue;
        ++current_rows;
        require(row.stable_id == "v3" && row.position == row_at(5U), "the realized current row is v3 at its new position");
    }
    require(current_rows == 1U && (*grid).first_displayed_row() == row_at(2U),
            "one realized row is current, and the first displayed row stayed on v0");

    // Remove a selected row and a row above the current one.
    std::vector<DataGridViewRowId> removed{};
    for (const DataGridViewRowId& row : inserted) {
        if (row == id("n1") || row == id("v5")) continue;
        removed.push_back(row);
    }
    (*grid).set_virtual_row_order(removed);
    require((*grid).row_count() == 20U && (*grid).current_cell() == cell_at(4U, 0U) &&
                (*grid).selected_cells().size() == 1U && selection_changes == 1 && currents.empty(),
            "a removed selected row leaves the selection once; the current cell stays on v3");

    // Sort: the application reverses its order.
    const std::vector<DataGridViewRowId> reversed(removed.rbegin(), removed.rend());
    (*grid).set_virtual_row_order(reversed);
    const std::optional<DataGridViewRowPosition> v3 = (*grid).row_position(id("v3"));
    require(v3 && (*grid).current_cell() == DataGridViewCellAddress{*v3, 0U} && (*grid).is_cell_selected(*v3, 0U) &&
                (*grid).selected_cells().size() == 1U && currents.empty(),
            "a sort keeps the current cell and the selection on their IDs");

    // Full-row selection is kept by ID too.
    (*grid).selection_mode = DataGridViewSelectionMode::full_row_select;
    (*grid).set_row_selected(*(*grid).row_position(id("v10")), true);
    (*grid).set_virtual_row_order(removed);
    require((*grid).is_row_selected(*(*grid).row_position(id("v10"))) &&
                (*grid).is_row_selected(*(*grid).row_position(id("v3"))) && (*grid).selected_rows().size() == 2U,
            "selected rows stay selected through a sort");
    (*grid).selection_mode = DataGridViewSelectionMode::cell_select;

    // An edit in progress stays on its row through a reorder and commits to it.
    require((*grid).set_current_cell(DataGridViewCellAddress{*(*grid).row_position(id("v3")), 0U}) &&
                (*grid).begin_edit(),
            "an edit begins on v3");
    (*(*grid).editing_control()).text = "edited";
    std::vector<DataGridViewRowId> moved{};
    for (const DataGridViewRowId& row : removed) {
        if (!(row == id("v3"))) moved.push_back(row);
    }
    moved.push_back(id("v3"));
    (*grid).set_virtual_row_order(moved);
    window.perform_layout();
    require((*grid).is_current_cell_in_edit_mode() && (*grid).current_cell() == cell_at(19U, 0U),
            "the edit follows its row to the end");
    require((*grid).end_edit() && (*provider).last_row == id("v3") &&
                (*provider).last_value == BindingValue{std::string("edited")},
            "the edit commits to its own row's ID");

    // Removing the current row discards its edit; the row now at its position
    // becomes current.
    std::vector<DataGridViewCellEvent> ended;
    SubscriptionToken end_token = (*grid).cell_end_edit().subscribe(RecordCell{&ended});
    require((*grid).set_current_cell(cell_at(2U, 0U)) && (*grid).begin_edit(), "an edit begins at position 2");
    const DataGridViewRowId edited = (*grid).row_id(row_at(2U));
    const DataGridViewRowId following = (*grid).row_id(row_at(3U));
    std::vector<DataGridViewRowId> without{};
    for (const DataGridViewRowId& row : moved) {
        if (!(row == edited)) without.push_back(row);
    }
    (*grid).set_virtual_row_order(without);
    require(!(*grid).is_current_cell_in_edit_mode() && ended.size() == 1U && ended[0].row_id == edited &&
                ended[0].row == row_at(2U),
            "the removed row's edit is discarded and reported with its last position");
    require((*grid).current_row_id() == following && (*grid).current_cell() == cell_at(2U, 0U),
            "the row that takes the removed row's position becomes current");

    // The first displayed row stays on its ID, as ObjectView's top row does.
    (*grid).set_first_displayed_row(row_at(4U));
    const DataGridViewRowId top = (*grid).row_id(row_at(4U));
    std::vector<DataGridViewRowId> above{id("n7"), id("n8"), id("n9")};
    above.insert(above.end(), without.begin(), without.end());
    (*grid).set_virtual_row_order(above);
    require((*grid).first_displayed_row() == *(*grid).row_position(top), "the first displayed row follows its ID");

    // Refusals leave the grid unchanged.
    const std::size_t count = (*grid).row_count();
    const std::optional<DataGridViewRowId> current = (*grid).current_row_id();
    std::vector<DataGridViewRowId> duplicate = above;
    duplicate.push_back(id("v0"));
    require(throws<std::invalid_argument>(SetOrder{grid.get(), duplicate}), "a repeated ID is refused");
    require(throws<std::invalid_argument>(SetOrder{grid.get(), {id("v0"), id("")}}), "an empty ID is refused");
    require((*grid).row_count() == count && (*grid).current_row_id() == current,
            "a refused order changes nothing");
    require(throws<std::logic_error>(SetCount{grid.get(), 5U}), "virtual rows are not counted by set_row_count");
    require(throws<std::invalid_argument>(SetVirtualRows{grid.get(), nullptr, virtual_order(2U)}),
            "virtual rows need a provider");
    require(throws<std::invalid_argument>(SetVirtualRows{grid.get(), provider, {id("a"), id("a")}}) &&
                (*grid).row_count() == count,
            "set_virtual_rows refuses a repeated ID and changes nothing");
    std::shared_ptr<DataGridView> unbound = make_grid("grid.identity.unbound");
    require(throws<std::logic_error>(SetOrder{unbound.get(), virtual_order(2U)}),
            "only virtual rows take an order");
}

// Unbound and bound rows keep the same identity rules.
void unbound_row_identity() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.unbound.identity");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).set_current_cell(cell_at(1U, 0U)), "bravo becomes current");
    (*grid).set_cell_selected(row_at(2U), 0U, true);
    (*grid).rows.insert(0U, {.stable_id = {"zulu"}});
    require((*grid).current_cell() == cell_at(2U, 0U) && (*grid).current_row_id() == id("bravo") &&
                (*grid).is_cell_selected(row_at(3U), 0U) && (*grid).selected_cells().size() == 2U,
            "an insert above keeps the current cell and the selection on their rows");
    int selection_changes = 0;
    SubscriptionToken token = (*grid).selection_changed().subscribe(Counter{&selection_changes});
    (*grid).rows.erase(3U, 1U);
    require((*grid).selected_cells().size() == 1U && selection_changes == 1 && (*grid).current_row_id() == id("bravo"),
            "removing a selected row drops it from the selection");
    (*grid).rows.erase(2U, 1U);
    require((*grid).current_row_id() == id("alpha") && (*grid).current_cell() == cell_at(1U, 0U),
            "removing the current last row makes the new last row current");
}

void sorting_headers_and_resizing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.sort");
    static_cast<void>((*grid).set_cell_value(row_at(2U), 0U, BindingValue{std::string("Able")}));
    // The header click here only raises the event; sort_modes covers the
    // automatic sort a text column's click makes by default.
    (*grid).set_column_sort_mode(0U, DataGridViewColumnSortMode::programmatic);
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    std::vector<std::size_t> header_clicks;
    std::vector<std::size_t> widths;
    SubscriptionToken header_token = (*grid).column_header_click().subscribe(RecordIndex{&header_clicks});
    SubscriptionToken width_token = (*grid).column_width_changed().subscribe(RecordIndex{&widths});
    std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    click(window, centre(*grid, columns[0].parts.column_label.bounds));
    require(header_clicks.size() == 1U && header_clicks[0] == 0U, "clicking a header raises column_header_click");
    require((*grid).set_current_cell(cell_at(0U, 0U)), "Alpha is current");
    (*grid).set_cell_selected(row_at(1U), 0U, true);
    require((*grid).sort(0U, DataGridViewSortDirection::ascending), "unbound rows sort");
    require((*grid).cell_text(row_at(0U), 0U) == "Able" && (*grid).cell_text(row_at(1U), 0U) == "Alpha" &&
                (*grid).cell_text(row_at(2U), 0U) == "Bravo",
            "rows are ordered by the column's text");
    require((*grid).current_cell() == cell_at(1U, 0U) && (*grid).current_row_id() == id("alpha") &&
                (*grid).is_cell_selected(row_at(1U), 0U) && (*grid).is_cell_selected(row_at(2U), 0U) &&
                (*grid).row_position(id("charlie")) == row_at(0U),
            "the current cell and selection stay on their rows");
    window.perform_layout();
    columns = (*grid).realized_columns();
    require(columns[0].states.sorted_ascending && columns[0].parts.sort_glyph.present &&
                (*grid).columns()[0].sort_glyph == DataGridViewSortDirection::ascending,
            "the sorted column shows its glyph and publishes sorted_ascending");
    require((*grid).sort(1U, DataGridViewSortDirection::descending) &&
                (*grid).cell_value(row_at(0U), 1U) == BindingValue{true} &&
                (*grid).columns()[0].sort_glyph == DataGridViewSortDirection::none,
            "sorting another column moves the glyph to it");

    window.perform_layout();
    columns = (*grid).realized_columns();
    const Rect grip = columns[0].parts.column_grip.bounds;
    const Point start = centre(*grid, grip);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}),
            "the divider takes the press");
    require((*grid).realized_columns()[0].states.captured, "a resizing column publishes captured");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x + 30.0, start.y}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {start.x + 30.0, start.y}}));
    require((*grid).columns()[0].width == 130.0 && !widths.empty() && widths.back() == 0U,
            "dragging the divider widens the column");
    require(header_clicks.size() == 1U, "a resize is not a header click");

    std::shared_ptr<DataGridView> virtual_grid = make_control<DataGridView>(StableId("grid.sort.virtual"));
    (*virtual_grid).set_columns({{.name = "only"}});
    (*virtual_grid).set_virtual_rows(std::make_shared<CountingProvider>(), virtual_order(10U));
    require(!(*virtual_grid).sort(0U, DataGridViewSortDirection::ascending),
            "virtual rows are not sorted by the grid");
}

void bound_rows() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.bound"));
    (*grid).set_columns({{.name = "name", .header_text = "Name", .data_property_name = "Name"},
                         {.name = "city", .header_text = "City", .data_property_name = "City"}});
    // The rows counted below are the source's records alone; new_row_bound
    // covers the new row of a bound grid.
    (*grid).allow_user_to_add_rows = false;
    Window window(grid, {360.0, 160.0});
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    BindingRecord::FieldMap first;
    first["Name"] = std::string("Ada");
    first["City"] = std::string("London");
    BindingRecord::FieldMap second;
    second["Name"] = std::string("Grace");
    second["City"] = std::string("Arlington");
    (*source).set_records({BindingRecord("person.1", first), BindingRecord("person.2", second)});
    (*grid).data_source = source;
    window.perform_layout();
    require((*grid).row_source() == DataGridViewRowSource::binding_source && (*grid).row_count() == 2U &&
                (*grid).cell_text(row_at(1U), 0U) == "Grace" && (*grid).row_id(row_at(1U)) == id("person.2"),
            "bound rows are the source's records");
    require((*source).set_position(1) && (*grid).current_cell() == cell_at(1U, 0U),
            "the current row follows the source's position");
    require((*grid).set_current_cell(cell_at(0U, 1U)) && (*source).position() == 0,
            "moving the current row moves the position");
    require((*grid).begin_edit(), "a bound cell edits");
    (*(*grid).editing_control()).text = "Paris";
    require((*grid).end_edit() && (*source).current_field("City") == BindingValue{std::string("Paris")} &&
                (*grid).cell_text(row_at(0U), 1U) == "Paris",
            "a committed edit sets the source's field");
    static_cast<void>((*source).add(BindingRecord("person.3", first)));
    require((*grid).row_count() == 3U, "an added record is a new row");
    (*grid).set_cell_selected(row_at(2U), 0U, true);
    static_cast<void>((*source).insert(0U, BindingRecord("person.0", second)));
    require((*grid).current_row_id() == id("person.1") && (*grid).current_cell() == cell_at(1U, 1U) &&
                (*grid).is_cell_selected(row_at(3U), 0U) && (*source).position() == 1,
            "an inserted record keeps the current cell and the selection on their records");
    // Audit N47: the row index is updated from the change's position.
    require((*grid).row_position(id("person.0")) == row_at(0U) && (*grid).row_position(id("person.1")) == row_at(1U) &&
                (*grid).row_position(id("person.3")) == row_at(3U),
            "an insert reindexes the rows from its position on");
    require((*source).remove_at(1U) && (*grid).row_count() == 3U && (*grid).row_position(id("person.1")) == std::nullopt,
            "a removed record leaves the grid");
    // As for unbound rows: the record that took the removed record's place
    // is current (the source's position agrees), and the selection keeps
    // the records that remain.
    require((*grid).current_row_id() == id("person.2") && (*grid).current_cell() == cell_at(1U, 1U) &&
                (*source).position() == 1,
            "the record that takes the removed current record's place becomes current");
    require((*grid).is_cell_selected(row_at(2U), 0U) && (*grid).selected_cells().size() == 1U,
            "the removed record leaves the selection and person.3 stays selected");
    require((*grid).row_position(id("person.0")) == row_at(0U) && (*grid).row_position(id("person.2")) == row_at(1U) &&
                (*grid).row_position(id("person.3")) == row_at(2U),
            "a removal reindexes the rows after it");
    // Audit N47: appends, inserts and removals keep the row index without
    // a rebuild; every record is still found at its position.
    for (std::size_t index = 0; index < 300U; ++index) {
        static_cast<void>((*source).add(BindingRecord("bulk." + std::to_string(index), first)));
    }
    static_cast<void>((*source).insert(1U, BindingRecord("bulk.inserted", second)));
    require((*source).remove_at(5U), "a bulk record is removed");
    bool indexed = (*grid).row_count() == (*source).count();
    for (std::size_t position = 0; indexed && position < (*source).count(); ++position) {
        const DataGridViewRowId row = id((*source).records()[position].stable_id);
        indexed = (*grid).row_position(row) == row_at(position);
    }
    require(indexed, "the row index follows appends, inserts and removals");
    (*grid).data_source = nullptr;
    require((*grid).row_source() == DataGridViewRowSource::unbound && (*grid).row_count() == 0U,
            "a null source returns to empty unbound rows");
}

void scrolling() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.scroll");
    (*grid).set_row_count(40U);
    Window window(grid, {200.0, 140.0});
    window.perform_layout();
    const DataGridView::Parts parts = (*grid).parts();
    require(parts.vertical_scroll_bar.present && parts.horizontal_scroll_bar.present && parts.scroll_corner.present,
            "content larger than the grid shows both bars and the corner");
    require(window.request_focus(grid), "the grid takes focus");
    for (int step = 0; step < 10; ++step) static_cast<void>(key(window, PhysicalKey::down));
    window.perform_layout();
    require((*grid).current_cell() == cell_at(10U, 0U) && (*grid).first_displayed_row().value > 0U &&
                (*grid).displayed_row_end().value > 10U,
            "moving below the displayed rows scrolls them");
    require(static_cast<std::size_t>((*(*grid).vertical_scroll_bar()).value.read()) == (*grid).first_displayed_row().value,
            "the vertical bar follows the first displayed row");
    require(key(window, PhysicalKey::end) && (*grid).horizontal_scroll_offset() > 0.0,
            "moving to the last column scrolls it into view");
    window.perform_layout();
    PointerEvent wheel{PointerAction::wheel, PointerButton::none, centre(*grid, (*grid).parts().well.bounds)};
    wheel.wheel_delta = {0.0, 1.0};
    const std::size_t before = (*grid).first_displayed_row().value;
    require(window.dispatch_pointer(wheel) && (*grid).first_displayed_row().value + 3U == before,
            "a wheel notch scrolls three rows");
    window.perform_layout();
    (*(*grid).vertical_scroll_bar()).value = 20.0;
    require((*grid).first_displayed_row() == row_at(20U), "moving the bar scrolls the rows");
}

// Records the images drawn and the clip each was drawn under; the grid draws
// an image cell's image whole and cuts it with a clip at the cell's edges.
class ImagePainter final : public Painter {
public:
    struct Drawn final {
        ImageId image{};
        Rect bounds{};
        Rect clip{};
    };

    void save() override { saved_.push_back(clip_); }
    void restore() override {
        if (saved_.empty()) return;
        clip_ = saved_.back();
        saved_.pop_back();
    }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { clip_ = rect; }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId image, Rect bounds, double) override { drawn.push_back({image, bounds, clip_}); }

    std::vector<Drawn> drawn;

private:
    Rect clip_{};
    std::vector<Rect> saved_;
};

// A solid image of one size, held by the window.
ImageId load_solid_image(Window& window, std::uint32_t width, std::uint32_t height) {
    const std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U, std::byte{0xFF});
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(width, height, std::uint64_t{width} * 4U, pixels);
    require(static_cast<bool>(loaded), "the test image loads");
    return loaded.image;
}

struct SetBadLayout final {
    DataGridView* grid{};
    void operator()() const {
        (*grid).add_column({.name = "bad", .kind = DataGridViewColumnKind::image,
                            .image_layout = static_cast<DataGridViewImageLayout>(9)});
    }
};

bool near(double left, double right) {
    const bool result = left - right < 0.001 && right - left < 0.001;
    return result;
}

void image_cells() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.images"));
    (*grid).set_columns({
        {.name = "name", .header_text = "Name", .width = 80.0},
        {.name = "badge", .header_text = "Badge", .kind = DataGridViewColumnKind::image, .width = 60.0,
         .image_description = "Validated"},
        {.name = "thumb", .header_text = "Thumb", .kind = DataGridViewColumnKind::image, .width = 60.0,
         .image_layout = DataGridViewImageLayout::zoom},
        {.name = "fill", .header_text = "Fill", .kind = DataGridViewColumnKind::image, .width = 60.0,
         .image_layout = DataGridViewImageLayout::stretch},
        {.name = "strip", .header_text = "Strip", .kind = DataGridViewColumnKind::image, .width = 30.0,
         .minimum_width = 20.0},
    });
    Window window(grid, {400.0, 160.0});
    const ImageId badge = load_solid_image(window, 16U, 8U);
    const ImageId wide = load_solid_image(window, 80U, 10U);
    (*grid).rows.append({.stable_id = {"alpha"},
                          .cells = {BindingValue{std::string("Alpha")}, BindingValue{badge}, BindingValue{badge},
                                    BindingValue{badge}, BindingValue{wide}}});
    (*grid).rows.append({.stable_id = {"bravo"}, .cells = {BindingValue{std::string("Bravo")}}});
    (*grid).rows.append({.stable_id = {"charlie"},
                          .cells = {BindingValue{std::string("Charlie")}, BindingValue{ImageId{0x7FFFFFFFU}},
                                    BindingValue{std::string("not an image")}}});
    window.perform_layout();
    require(throws<std::invalid_argument>(SetBadLayout{grid.get()}) && (*grid).columns().size() == 5U,
            "an image layout outside the enumeration is refused");

    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* normal = find_cell(cells, 0U, 1U);
    const Rect normal_inside = (*normal).parts.cell_selection.bounds;
    const Rect normal_image = (*normal).parts.cell_image.bounds;
    require((*normal).parts.cell_image.present && !(*normal).parts.cell_content.present &&
                near(normal_image.width, 16.0) && near(normal_image.height, 8.0) &&
                near(normal_image.x + 8.0, normal_inside.x + normal_inside.width * 0.5) &&
                near(normal_image.y + 4.0, normal_inside.y + normal_inside.height * 0.5),
            "a normal image cell shows its image at its own size, centred");
    const DataGridView::Cell* zoomed = find_cell(cells, 0U, 2U);
    const Rect zoom_inside = (*zoomed).parts.cell_selection.bounds;
    const Rect zoom_image = (*zoomed).parts.cell_image.bounds;
    require((*zoomed).parts.cell_image.present && near(zoom_image.width, zoom_image.height * 2.0) &&
                (near(zoom_image.width, zoom_inside.width) || near(zoom_image.height, zoom_inside.height)) &&
                zoom_image.width <= zoom_inside.width + 0.001 && zoom_image.height <= zoom_inside.height + 0.001,
            "a zoomed image is as large as fits, its shape kept");
    const DataGridView::Cell* stretched = find_cell(cells, 0U, 3U);
    const Rect fill_inside = (*stretched).parts.cell_selection.bounds;
    const Rect fill_image = (*stretched).parts.cell_image.bounds;
    require((*stretched).parts.cell_image.present && near(fill_image.x, fill_inside.x) &&
                near(fill_image.width, fill_inside.width) && near(fill_image.height, fill_inside.height),
            "a stretched image fills its cell");
    const DataGridView::Cell* strip = find_cell(cells, 0U, 4U);
    const Rect strip_inside = (*strip).parts.cell_selection.bounds;
    require((*strip).parts.cell_image.present && near((*strip).parts.cell_image.bounds.width, strip_inside.width) &&
                near((*strip).parts.cell_image.bounds.x, strip_inside.x),
            "an image wider than its cell is published as it is seen, cut at the cell's edges");
    require(!(*find_cell(cells, 1U, 1U)).parts.cell_image.present &&
                !(*find_cell(cells, 2U, 1U)).parts.cell_image.present &&
                !(*find_cell(cells, 2U, 2U)).parts.cell_image.present,
            "an empty cell, an image the window does not hold and a value that is no image show nothing");
    require((*grid).cell_text(row_at(0U), 1U) == "Validated" && (*grid).cell_text(row_at(1U), 1U).empty() &&
                (*grid).cell_text(row_at(0U), 2U).empty(),
            "an image cell's text is its column's description while it holds an image");

    ImagePainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    require(painter.drawn.size() == 4U, "each held image is drawn once, and nothing for the cells without one");
    bool strip_drawn_whole = false;
    for (const ImagePainter::Drawn& drawn : painter.drawn) {
        if (drawn.image == wide && near(drawn.bounds.width, 80.0) && near(drawn.clip.width, strip_inside.width) &&
            near(drawn.clip.x, strip_inside.x)) {
            strip_drawn_whole = true;
        }
    }
    require(strip_drawn_whole, "the wide image is drawn whole under a clip at its cell's edges");

    std::vector<DataGridViewCellEvent> clicked;
    SubscriptionToken click_token = (*grid).cell_content_click().subscribe(RecordCell{&clicked});
    require(window.request_focus(grid), "the grid takes focus");
    click(window, centre(*grid, normal_image));
    require(clicked.size() == 1U && clicked[0].row_id == id("alpha") && clicked[0].column == 1U,
            "clicking an image raises cell_content_click");
    click(window, centre(*grid, (*find_cell(cells, 1U, 1U)).parts.cell.bounds));
    require(clicked.size() == 1U && (*grid).current_cell() == cell_at(1U, 1U),
            "clicking an image cell without an image only makes it current");
    static_cast<void>(key(window, PhysicalKey::f2));
    require(!(*grid).is_current_cell_in_edit_mode(), "an image cell does not edit");

    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    const SemanticNode& row = nodes[5];
    require(row.stable_id == "alpha" && row.children[2].name == "Badge" && row.children[2].value == "Validated" &&
                !has_action(row.children[2], SemanticAction::set_value) &&
                !has_action(row.children[2], SemanticAction::press),
            "an image cell is named by its column, valued by its description, and offers no set_value");
    require(!(*grid).on_semantic_child_action("alpha.badge", SemanticAction::set_value, "Other") &&
                (*grid).cell_value(row_at(0U), 1U) == BindingValue{badge},
            "set_value on an image cell is refused");
}

// Records drawn text with its colour, to tell a link's family by its colour.
class TextColourPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point from, Point to, Color color, double width) override {
        lines.push_back({from, to, color, width});
    }
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color color) override {
        drawn.push_back({std::string(text), color});
    }
    void draw_image(ImageId, Rect, double) override {}

    struct Drawn final {
        std::string text{};
        Color color{};
    };
    std::vector<Drawn> drawn;
    struct Line final {
        Point from{};
        Point to{};
        Color color{};
        double width{};
    };
    std::vector<Line> lines;

    // Whether a one-unit horizontal rule of `color` fills `bounds`, a
    // one-unit high part.
    [[nodiscard]] bool has_rule(Rect bounds, Color color) const {
        const double y = bounds.y + bounds.height - 0.5;
        for (const Line& line : lines) {
            if (line.color == color && near(line.width, 1.0) && near(line.from.y, y) && near(line.to.y, y) &&
                near(line.from.x, bounds.x) && near(line.to.x, bounds.x + bounds.width)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<Color> colour_of(std::string_view text) const {
        for (const Drawn& entry : drawn) {
            if (entry.text == text) return entry.color;
        }
        return std::nullopt;
    }
};

struct LinkVisitedOnText final {
    DataGridView* grid{};
    void operator()() const { static_cast<void>((*grid).link_visited(row_at(0U), 0U)); }
};
struct LinkVisitedPastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_link_visited(row_at(9U), 1U, true); }
};
struct AddBadLinkText final {
    DataGridView* grid{};
    void operator()() const {
        (*grid).add_column({.name = "bad", .kind = DataGridViewColumnKind::link, .link_text = std::string("\xff")});
    }
};

void link_cells() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.links"));
    (*grid).set_columns({
        {.name = "name", .header_text = "Name", .width = 80.0},
        {.name = "site", .header_text = "Site", .kind = DataGridViewColumnKind::link, .width = 150.0,
         .link_text = "Open"},
        {.name = "plain", .header_text = "Plain", .kind = DataGridViewColumnKind::link, .width = 100.0,
         .default_cell_style = {.alignment = DataGridViewContentAlignment::middle_end}, .track_visited_state = false},
    });
    Window window(grid, {420.0, 160.0});
    (*grid).rows.append({.stable_id = {"alpha"},
                          .cells = {BindingValue{std::string("Alpha")}, BindingValue{std::string("Go")},
                                    BindingValue{std::string("Docs")}}});
    (*grid).rows.append({.stable_id = {"bravo"}, .cells = {BindingValue{std::string("Bravo")}}});
    window.perform_layout();
    require(throws<std::invalid_argument>(AddBadLinkText{grid.get()}), "link text must be valid UTF-8");
    require(throws<std::invalid_argument>(LinkVisitedOnText{grid.get()}),
            "the visited state belongs to link columns only");
    require(throws<std::out_of_range>(LinkVisitedPastEnd{grid.get()}), "a link cell past the rows is refused");
    require((*grid).cell_text(row_at(0U), 1U) == "Go" && (*grid).cell_text(row_at(1U), 1U) == "Open" &&
                (*grid).cell_text(row_at(1U), 2U).empty(),
            "a link cell shows its value, or its column's link text when empty");

    // The link is its text's extent in the content area, placed by the
    // column's alignment; an empty cell without link text has no link.
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* const go_cell = find_cell(cells, 0U, 1U);
    const DataGridView::Cell* const docs_cell = find_cell(cells, 0U, 2U);
    const DataGridView::Cell* const open_cell = find_cell(cells, 1U, 1U);
    const DataGridView::Cell* const empty_cell = find_cell(cells, 1U, 2U);
    require(go_cell != nullptr && docs_cell != nullptr && open_cell != nullptr && empty_cell != nullptr,
            "the link cells are realized");
    const DataGridView::Cell& go = *go_cell;
    const Rect go_content = go.parts.cell_content.bounds;
    const Rect go_link = go.parts.cell_link.bounds;
    require(go.parts.cell_link.present && near(go_link.x, go_content.x) && go_link.width > 0.0 &&
                go_link.width < go_content.width * 0.5 && near(go_link.height, go_content.height),
            "a start-aligned link starts at the content area and is as wide as its text");
    const DataGridView::Cell& docs = *docs_cell;
    const Rect docs_content = docs.parts.cell_content.bounds;
    require(docs.parts.cell_link.present &&
                near(docs.parts.cell_link.bounds.x + docs.parts.cell_link.bounds.width,
                     docs_content.x + docs_content.width),
            "an end-aligned link ends at the content area's end");
    require((*open_cell).parts.cell_link.present && !(*empty_cell).parts.cell_link.present,
            "an empty link cell shows its column's link text, or no link without one");
    require(!go.states.visited && !(*grid).link_visited(row_at(0U), 1U), "a link starts unvisited");

    // Clicking the link follows it: cell_content_click, and the link is
    // visited where the column tracks it. Clicking beside the link only
    // makes the cell current.
    std::vector<DataGridViewCellEvent> clicked;
    SubscriptionToken click_token = (*grid).cell_content_click().subscribe(RecordCell{&clicked});
    require(window.request_focus(grid), "the grid takes focus");
    const Rect beside{go_link.x + go_link.width + 20.0, go_link.y, 10.0, go_link.height};
    PointerEvent over_link{PointerAction::move, PointerButton::none, centre(*grid, go_link)};
    static_cast<void>(window.dispatch_pointer(over_link));
    require((*grid).cursor.read() == CursorKind::hand, "the pointer over a link is the hand");
    PointerEvent over_beside{PointerAction::move, PointerButton::none, centre(*grid, beside)};
    static_cast<void>(window.dispatch_pointer(over_beside));
    require((*grid).cursor.read() == CursorKind::arrow, "the pointer beside a link is the arrow");
    click(window, centre(*grid, beside));
    require(clicked.empty() && (*grid).current_cell() == cell_at(0U, 1U) && !(*grid).link_visited(row_at(0U), 1U),
            "a click beside the link makes the cell current and follows nothing");
    click(window, centre(*grid, go_link));
    require(clicked.size() == 1U && clicked[0].row_id == id("alpha") && clicked[0].column == 1U &&
                (*grid).link_visited(row_at(0U), 1U),
            "clicking the link raises cell_content_click and visits the link");
    const std::vector<DataGridView::Cell> visited_cells = (*grid).realized_cells();
    const DataGridView::Cell* const visited_cell = find_cell(visited_cells, 0U, 1U);
    require(visited_cell != nullptr && (*visited_cell).states.visited,
            "a visited link cell publishes the visited state");

    // Space follows the current cell's link; a column that does not track
    // visits leaves its link unvisited.
    require((*grid).set_current_cell(cell_at(0U, 2U)), "the plain link becomes current");
    static_cast<void>(key(window, PhysicalKey::space));
    require(clicked.size() == 2U && clicked[1].column == 2U && !(*grid).link_visited(row_at(0U), 2U),
            "space follows a link; an untracked link stays unvisited");
    static_cast<void>(key(window, PhysicalKey::f2));
    require(!(*grid).is_current_cell_in_edit_mode(), "a link cell does not edit");

    // The link is drawn in the link family's colour, visited or not.
    TextColourPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    const BasicControlStyle& style = (*grid).effective_theme().basic_style();
    const std::optional<Color> go_colour = painter.colour_of("Go");
    const std::optional<Color> open_colour = painter.colour_of("Open");
    require(go_colour && *go_colour == style.visited_link && open_colour && *open_colour == style.link,
            "a visited link is drawn in the visited link colour, another in the link colour");

    // The underline is a one-unit rule just below the text's baseline, as
    // wide as the link, in the link's colour.
    const Rect go_underline = go.parts.cell_link_underline.bounds;
    const Rect open_underline = (*open_cell).parts.cell_link_underline.bounds;
    require(go.parts.cell_link_underline.present && near(go_underline.x, go_link.x) &&
                near(go_underline.width, go_link.width) && near(go_underline.height, 1.0) &&
                go_underline.y > go_link.y + go_link.height * 0.5 &&
                go_underline.y + go_underline.height <= go_link.y + go_link.height,
            "a link's underline lies below its baseline, inside the link, as wide as it");
    require(!(*empty_cell).parts.cell_link_underline.present, "a cell with no link has no underline");
    require(painter.has_rule(go_underline, style.visited_link) && painter.has_rule(open_underline, style.link),
            "the underline is drawn by the underline mark in the link's colour");

    // Semantics: a link cell is named by its text, valued visited or
    // unvisited, and pressed to follow its link.
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    const SemanticNode* alpha = nullptr;
    const SemanticNode* bravo = nullptr;
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == "alpha") alpha = &node;
        if (node.stable_id == "bravo") bravo = &node;
    }
    require(alpha != nullptr && bravo != nullptr, "both rows are published");
    const SemanticNode& visited_node = (*alpha).children[2];
    const SemanticNode& open_node = (*bravo).children[2];
    require(visited_node.role == SemanticRole::cell && visited_node.name == "Go" &&
                visited_node.value == "visited" && has_action(visited_node, SemanticAction::press) &&
                !has_action(visited_node, SemanticAction::set_value) && open_node.name == "Open" &&
                open_node.value == "unvisited",
            "a link cell is named by its text, valued by its visited state, and offers press");
    require((*grid).on_semantic_child_action("bravo.site", SemanticAction::press, {}) && clicked.size() == 3U &&
                (*grid).link_visited(row_at(1U), 1U),
            "pressing a link cell follows its link");
    require(!(*grid).on_semantic_child_action("bravo.site", SemanticAction::set_value, "x"),
            "set_value on a link cell is refused");

    // The visited state is kept by row, and ends with its row and with
    // replaced columns.
    (*grid).set_link_visited(row_at(1U), 1U, false);
    require(!(*grid).link_visited(row_at(1U), 1U), "set_link_visited clears a visit");
    (*grid).set_link_visited(row_at(1U), 1U, true);
    require((*grid).sort(0U, DataGridViewSortDirection::descending) && (*grid).link_visited(row_at(0U), 1U) &&
                (*grid).link_visited(row_at(1U), 1U) && (*grid).row_position(id("bravo")) == row_at(0U),
            "visits stay on their rows when rows move");
    (*grid).rows.erase(0U, 1U);
    (*grid).rows.append({.stable_id = {"bravo"}});
    require(!(*grid).link_visited(row_at(1U), 1U) && (*grid).link_visited(row_at(0U), 1U),
            "a removed row's visits end with it");
    const std::span<const DataGridViewColumn> kept = (*grid).columns();
    (*grid).set_columns(std::vector<DataGridViewColumn>(kept.begin(), kept.end()));
    require(!(*grid).link_visited(row_at(0U), 1U), "replacing the columns ends every visit");
}

void semantics() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.semantics");
    (*grid).accessible_name = "Stations";
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    const SemanticDescriptor descriptor = (*grid).semantic_descriptor();
    require(descriptor.role == SemanticRole::table && descriptor.name == "Stations" &&
                descriptor.column_count == 4U && descriptor.row_count == (*grid).row_count() &&
                has_semantic_state(descriptor.states, SemanticState::multiselectable),
            "the grid is a named multiple-selection table sized by its rows and columns");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    require(nodes.size() == 7U && nodes[0].role == SemanticRole::column_header &&
                nodes[0].stable_id == "grid.semantics.column.name" && nodes[0].column_index == 1U &&
                nodes[3].column_index == 4U,
            "column headers carry their column, before the rows");
    const SemanticNode& row = nodes[4];
    require(row.role == SemanticRole::row && row.stable_id == "alpha" && row.children.size() == 5U &&
                row.row_index == 1U && row.children[3].row_index == 1U && row.children[3].column_index == 3U,
            "each row holds its header and its cells, and each cell carries its row and column");
    const SemanticNode& header = row.children[0];
    require(header.role == SemanticRole::row_header && header.stable_id == "grid.semantics.row-header.alpha" &&
                header.row_index == 1U && !header.column_index && header.name == row.name &&
                header.actions == std::vector<SemanticAction>{SemanticAction::press},
            "a drawn row header comes first in its row, carrying its row only");
    bool all_cells = true;
    for (std::size_t index = 1U; index < row.children.size(); ++index) {
        if (row.children[index].role != SemanticRole::cell) all_cells = false;
    }
    require(all_cells, "every cell publishes the cell role, whatever its kind");
    require(row.children[1].value == "Alpha" && has_action(row.children[1], SemanticAction::set_value) &&
                has_semantic_state(row.children[2].states, SemanticState::checkable) &&
                has_semantic_state(row.children[2].states, SemanticState::checked) &&
                has_action(row.children[2], SemanticAction::press) &&
                has_action(row.children[3], SemanticAction::expand) &&
                has_action(row.children[4], SemanticAction::press),
            "a cell keeps its kind's behaviour: a value, a toggle, a choice, a press");
    require((*grid).on_semantic_child_action("alpha.active", SemanticAction::press, {}) &&
                (*grid).cell_value(row_at(0U), 1U) == BindingValue{false},
            "press toggles a check box cell");
    require((*grid).on_semantic_child_action("bravo.name", SemanticAction::set_value, "Foxtrot") &&
                (*grid).cell_text(row_at(1U), 0U) == "Foxtrot" && !(*grid).is_current_cell_in_edit_mode(),
            "set_value commits a text cell");
    require((*grid).on_semantic_child_action("charlie.mode", SemanticAction::set_value, "USB") &&
                (*grid).cell_text(row_at(2U), 2U) == "USB",
            "set_value chooses a combo box item");
    require(!(*grid).on_semantic_child_action("charlie.mode", SemanticAction::set_value, "SSB"),
            "a value that is no item is refused");
    require((*grid).on_semantic_child_action("bravo", SemanticAction::select, {}) &&
                (*grid).current_row_id() == id("bravo"),
            "selecting a row node by its ID makes its row current");
    require((*grid).on_semantic_child_action("grid.semantics.row-header.charlie", SemanticAction::press, {}) &&
                (*grid).current_row_id() == id("charlie"),
            "pressing a row header makes its row current");
    require(!(*grid).on_semantic_child_action("grid.semantics.row-header.zulu", SemanticAction::press, {}) &&
                !(*grid).on_semantic_child_action("grid.semantics.row-header.alpha", SemanticAction::select, {}),
            "a row header of no row, or an action other than press, is refused");
    (*grid).row_headers_visible = false;
    window.perform_layout();
    const std::vector<SemanticNode> headless = (*grid).semantic_virtual_children();
    require(headless[4].children.size() == 4U && headless[4].children[0].role == SemanticRole::cell,
            "a grid that draws no row headers publishes none");
    std::vector<std::size_t> header_clicks;
    SubscriptionToken token = (*grid).column_header_click().subscribe(RecordIndex{&header_clicks});
    require((*grid).on_semantic_child_action("grid.semantics.column.mode", SemanticAction::press, {}) &&
                header_clicks.size() == 1U && header_clicks[0] == 2U,
            "pressing a header node raises column_header_click");
}


// -- the editing lifecycle (batch 1) -------------------------------------------

// Records every event of a move, by name, in order.
struct EventLog final {
    std::vector<std::string> names{};
};
struct LogCell final {
    EventLog* log{};
    const char* name{};
    void operator()(const DataGridViewCellEvent&) const { (*log).names.emplace_back(name); }
};
struct LogRow final {
    EventLog* log{};
    const char* name{};
    void operator()(const DataGridViewRowEvent&) const { (*log).names.emplace_back(name); }
};
struct LogValidating final {
    EventLog* log{};
    bool* cancel{};
    void operator()(DataGridViewRowCancelEvent& event) const {
        (*log).names.emplace_back("row_validating");
        event.cancel = *cancel;
    }
};
struct LogCurrent final {
    EventLog* log{};
    void operator()(std::optional<DataGridViewCellAddress>) const { (*log).names.emplace_back("current"); }
};
struct RecordDataError final {
    std::vector<DataGridViewDataErrorEvent>* seen{};
    bool cancel{true};
    void operator()(DataGridViewDataErrorEvent& event) const {
        (*seen).push_back(event);
        event.cancel = cancel;
    }
};
struct RecordDirty final {
    std::vector<bool>* seen{};
    void operator()(bool dirty) const { (*seen).push_back(dirty); }
};
struct RecordRow final {
    std::vector<DataGridViewRowEvent>* seen{};
    void operator()(const DataGridViewRowEvent& event) const { (*seen).push_back(event); }
};
struct FillDefaults final {
    int* calls{};
    void operator()(DataGridViewRowValuesEvent& event) const {
        ++*calls;
        event.values[2] = BindingValue{std::string("USB")};
    }
};
struct KeepRow final {
    std::string kept{};
    int* asked{};
    void operator()(DataGridViewRowCancelEvent& event) const {
        ++*asked;
        if (event.row_id.value == kept) event.cancel = true;
    }
};
// Calls set_current_cell from an enter event, which the grid refuses.
struct ReenterCurrent final {
    DataGridView* grid{};
    bool* refused{};
    void operator()(const DataGridViewCellEvent&) const {
        try {
            static_cast<void>((*grid).set_current_cell(cell_at(0U, 0U)));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

std::vector<DataGridViewColumn> number_columns() {
    std::vector<DataGridViewColumn> columns = sample_columns();
    columns.push_back({.name = "freq", .header_text = "Freq", .width = 80.0,
                       .value_kind = BindingValueKind::number});
    return columns;
}

void data_errors_and_dirty_state() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.errors");
    (*grid).set_columns(number_columns());
    (*grid).set_row_count(2U);
    Window window(grid, {420.0, 160.0});
    window.perform_layout();
    require(window.request_focus(grid), "the grid takes focus");
    std::vector<DataGridViewDataErrorEvent> errors;
    std::vector<bool> dirty;
    std::vector<DataGridViewCellEvent> validated;
    SubscriptionToken dirty_token = (*grid).current_cell_dirty_state_changed().subscribe(RecordDirty{&dirty});
    SubscriptionToken validated_token = (*grid).cell_validated().subscribe(RecordCell{&validated});
    require((*grid).set_current_cell(cell_at(0U, 4U)), "the number cell is current");

    // B1-1: with no handler a parse error keeps the edit, marked invalid.
    require((*grid).begin_edit() && !(*grid).is_current_cell_dirty(), "F2-style edit starts clean");
    require(window.dispatch_text({"abc"}) && (*grid).is_current_cell_dirty() && dirty == std::vector<bool>{true},
            "typing makes the edit dirty, raised once");
    require(!(*grid).end_edit() && (*grid).is_current_cell_in_edit_mode() &&
                (*grid).cell_value(row_at(0U), 4U) == BindingValue{},
            "an unhandled data error keeps the edit and the old value");
    const std::vector<DataGridView::Cell> invalid_cells = (*grid).realized_cells();
    const DataGridView::Cell* invalid = find_cell(invalid_cells, 0U, 4U);
    require(invalid != nullptr && (*invalid).states.invalid, "the cell is marked invalid");

    SubscriptionToken error_token = (*grid).data_error().subscribe(RecordDataError{&errors, true});
    require(!(*grid).end_edit() && errors.size() == 1U &&
                errors[0].context == DataGridViewDataErrorContext::parsing && errors[0].formatted_value == "abc" &&
                errors[0].row_id == id("alpha") && errors[0].column == 4U,
            "data_error reports the parse with its text and cell");
    error_token.disconnect();
    error_token = (*grid).data_error().subscribe(RecordDataError{&errors, false});
    require((*grid).end_edit() && !(*grid).is_current_cell_in_edit_mode() && !(*grid).is_current_cell_dirty() &&
                (*grid).cell_value(row_at(0U), 4U) == BindingValue{} && validated.empty(),
            "a handler that clears cancel discards the edit; nothing is validated");
    require(dirty.size() == 2U && !dirty[1], "the dirty state ends with the edit");

    // commit_edit stores without leaving edit; refresh_edit restores the cell.
    require((*grid).begin_edit(), "edit again");
    (*(*grid).editing_control()).text = "7.5";
    require((*grid).commit_edit() && (*grid).is_current_cell_in_edit_mode() && !(*grid).is_current_cell_dirty() &&
                (*grid).cell_value(row_at(0U), 4U) == BindingValue{7.5},
            "commit_edit stores and stays in edit");
    require((*grid).is_current_row_dirty(), "a committed value makes the row dirty");
    (*(*grid).editing_control()).text = "9";
    require((*grid).refresh_edit() && (*(*grid).editing_control()).text.read() == "7.5" &&
                !(*grid).is_current_cell_dirty(),
            "refresh_edit shows the stored value again");
    require((*grid).end_edit() && validated.size() == 1U && validated[0].column == 4U,
            "a commit raises cell_validated");

    // A provider that refuses a value raises a commit data error.
    std::shared_ptr<DataGridView> virtual_grid = make_control<DataGridView>(StableId("grid.errors-virtual"));
    (*virtual_grid).set_columns(virtual_columns());
    std::shared_ptr<CountingProvider> provider = std::make_shared<CountingProvider>();
    (*provider).refuse = true;
    (*virtual_grid).set_virtual_rows(provider, virtual_order(3U));
    Window virtual_window(virtual_grid, {300.0, 160.0});
    virtual_window.perform_layout();
    std::vector<DataGridViewDataErrorEvent> commits;
    SubscriptionToken commit_token = (*virtual_grid).data_error().subscribe(RecordDataError{&commits, true});
    require((*virtual_grid).set_current_cell(cell_at(1U, 0U)) && (*virtual_grid).begin_edit(), "virtual edit");
    (*(*virtual_grid).editing_control()).text = "changed";
    require(!(*virtual_grid).end_edit() && commits.size() == 1U &&
                commits[0].context == DataGridViewDataErrorContext::commit && commits[0].row_id == id("v1"),
            "a refused store is a commit data error");
    (*virtual_grid).cancel_edit();
    require(!(*virtual_grid).is_current_cell_in_edit_mode(), "the refused edit is discarded by cancel_edit");
}

void enter_leave_and_validation() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.moves");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    EventLog log;
    bool cancel_row = false;
    SubscriptionToken cell_leave = (*grid).cell_leave().subscribe(LogCell{&log, "cell_leave"});
    SubscriptionToken cell_enter = (*grid).cell_enter().subscribe(LogCell{&log, "cell_enter"});
    SubscriptionToken row_leave = (*grid).row_leave().subscribe(LogRow{&log, "row_leave"});
    SubscriptionToken row_enter = (*grid).row_enter().subscribe(LogRow{&log, "row_enter"});
    SubscriptionToken row_validated = (*grid).row_validated().subscribe(LogRow{&log, "row_validated"});
    SubscriptionToken row_validating = (*grid).row_validating().subscribe(LogValidating{&log, &cancel_row});
    SubscriptionToken current = (*grid).current_cell_changed().subscribe(LogCurrent{&log});
    require((*grid).set_current_cell(cell_at(0U, 0U)), "first current cell");
    require(log.names == std::vector<std::string>{"row_enter", "cell_enter", "current"},
            "entering the first cell raises row_enter, cell_enter, then current_cell_changed");
    log.names.clear();
    require((*grid).set_current_cell(cell_at(0U, 2U)), "within the row");
    require(log.names == std::vector<std::string>{"cell_leave", "cell_enter", "current"},
            "a move within a row validates no row");
    log.names.clear();
    require((*grid).set_current_cell(cell_at(1U, 2U)), "to another row");
    require(log.names == std::vector<std::string>{"row_validating", "cell_leave", "row_leave", "row_validated",
                                                  "row_enter", "cell_enter", "current"},
            "a row change validates, leaves, then enters, in order");
    log.names.clear();
    cancel_row = true;
    require(!(*grid).set_current_cell(cell_at(2U, 0U)) && (*grid).current_cell() == cell_at(1U, 2U) &&
                log.names == std::vector<std::string>{"row_validating"},
            "a cancelled row_validating keeps the current row");
    cancel_row = false;
    log.names.clear();
    require((*grid).set_current_cell(std::nullopt) && !(*grid).current_cell(), "no current cell");
    require(log.names == std::vector<std::string>{"row_validating", "cell_leave", "row_leave", "row_validated",
                                                  "current"},
            "clearing the current cell validates and leaves its row");
    bool refused = false;
    SubscriptionToken reenter = (*grid).cell_enter().subscribe(ReenterCurrent{grid.get(), &refused});
    require((*grid).set_current_cell(cell_at(2U, 0U)) && refused && (*grid).current_cell() == cell_at(2U, 0U),
            "set_current_cell from an enter handler is refused");
}

void edit_modes() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.modes");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require(window.request_focus(grid) && (*grid).current_cell() == cell_at(0U, 0U), "focused on the first cell");
    require((*grid).edit_mode.read() == DataGridViewEditMode::edit_on_keystroke_or_f2, "the default edit mode");
    (*grid).edit_mode = DataGridViewEditMode::edit_programmatically;
    require(!window.dispatch_text({"x"}) && !(*grid).is_current_cell_in_edit_mode(), "typing does not edit");
    static_cast<void>(key(window, PhysicalKey::f2));
    require(!(*grid).is_current_cell_in_edit_mode(), "F2 does not edit programmatically");
    require((*grid).begin_edit(), "begin_edit always edits");
    (*grid).cancel_edit();
    (*grid).edit_mode = DataGridViewEditMode::edit_on_f2;
    require(!window.dispatch_text({"x"}) && !(*grid).is_current_cell_in_edit_mode(), "on_f2 ignores typing");
    static_cast<void>(key(window, PhysicalKey::f2));
    require((*grid).is_current_cell_in_edit_mode(), "on_f2 edits on F2");
    (*grid).cancel_edit();
    (*grid).edit_mode = DataGridViewEditMode::edit_on_keystroke;
    static_cast<void>(key(window, PhysicalKey::f2));
    require(!(*grid).is_current_cell_in_edit_mode(), "on_keystroke ignores F2");
    require(window.dispatch_text({"Z"}) && (*grid).is_current_cell_in_edit_mode() &&
                (*grid).is_current_cell_dirty(),
            "on_keystroke edits on typing, dirty at once");
    (*grid).cancel_edit();
    (*grid).edit_mode = DataGridViewEditMode::edit_on_enter;
    static_cast<void>(key(window, PhysicalKey::down));
    require((*grid).current_cell() == cell_at(1U, 0U) && (*grid).is_current_cell_in_edit_mode(),
            "on_enter edits a text cell as it becomes current");
    static_cast<void>(key(window, PhysicalKey::tab));
    require((*grid).current_cell() == cell_at(1U, 1U) && !(*grid).is_current_cell_in_edit_mode(),
            "on_enter does not edit a check box cell");
}

void new_row_unbound() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.new-row");
    Window window(grid, {360.0, 200.0});
    window.perform_layout();
    std::shared_ptr<DataGridView> fresh = make_control<DataGridView>(StableId("grid.new-row.fresh"));
    require((*fresh).allow_user_to_add_rows.read() && (*fresh).allow_user_to_delete_rows.read(),
            "adding and deleting rows are on by default, as in Windows Forms");
    (*fresh).set_columns(sample_columns());
    require((*fresh).row_count() == 1U && (*fresh).new_row_index() == row_at(0U),
            "a fresh grid with columns shows only the new row");
    require(!(*grid).new_row_index() && (*grid).row_count() == 3U, "no new row once turned off");
    (*grid).allow_user_to_add_rows = true;
    window.perform_layout();
    require((*grid).new_row_index() == row_at(3U) && (*grid).row_count() == 4U,
            "the new row follows the last row");
    const DataGridViewRowId placeholder = (*grid).row_id(row_at(3U));
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows.size() == 4U && rows[3].parts.row_glyph.present, "the new row shows its star glyph");
    int defaults = 0;
    std::vector<DataGridViewRowEvent> added;
    std::vector<DataGridViewRowEvent> cancelled;
    SubscriptionToken default_token = (*grid).default_values_needed().subscribe(FillDefaults{&defaults});
    SubscriptionToken added_token = (*grid).user_added_row().subscribe(RecordRow{&added});
    SubscriptionToken cancelled_token = (*grid).row_edit_cancelled().subscribe(RecordRow{&cancelled});
    require(window.request_focus(grid), "focus");
    require((*grid).set_current_cell(cell_at(3U, 0U)) && defaults == 1 &&
                (*grid).cell_text(row_at(3U), 2U) == "USB",
            "entering the new row asks for its default values");
    require(!(*grid).is_current_row_dirty() && added.empty(), "entering adds nothing");
    require(window.dispatch_text({"Delta"}) && (*grid).is_current_row_dirty(), "typing into the new row");
    require(key(window, PhysicalKey::tab), "Tab commits");
    require(added.size() == 1U && added[0].row_id == placeholder && (*grid).row_count() == 5U &&
                (*grid).new_row_index() == row_at(4U) && (*grid).cell_text(row_at(3U), 0U) == "Delta" &&
                (*grid).cell_text(row_at(3U), 2U) == "USB" && (*grid).row_id(row_at(3U)) == placeholder,
            "the commit adds the row with the new row's ID and its defaults; a new row follows");
    require((*grid).current_cell() == cell_at(3U, 1U) && (*grid).is_current_row_dirty(),
            "the added row stays current and dirty");
    require(key(window, PhysicalKey::escape) && cancelled.size() == 1U && (*grid).row_count() == 4U &&
                !(*grid).row_position(placeholder) && (*grid).current_cell() == cell_at(3U, 1U) &&
                (*grid).new_row_index() == row_at(3U),
            "Escape discards the added row; the new row takes its place");
    // Adding and leaving keeps the row.
    require((*grid).set_current_cell(cell_at(3U, 1U)), "the new row's check box");
    require(key(window, PhysicalKey::space) && (*grid).row_count() == 5U && added.size() == 2U,
            "a toggle adds the row too");
    require((*grid).set_current_cell(cell_at(0U, 0U)) && !(*grid).cancel_row_edit() && (*grid).row_count() == 5U,
            "once left, the added row is kept");
    require(throws<std::out_of_range>(RemoveNewRow{grid.get()}), "the new row is not removed");
    (*grid).allow_user_to_add_rows = false;
    require((*grid).row_count() == 4U && !(*grid).new_row_index(), "turning it off hides the new row");
}

void new_row_bound() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.new-bound"));
    (*grid).set_columns({{.name = "name", .header_text = "Name", .data_property_name = "Name"},
                         {.name = "city", .header_text = "City", .data_property_name = "City"}});
    Window window(grid, {360.0, 160.0});
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    BindingRecord::FieldMap first;
    first["Name"] = std::string("Ada");
    (*source).set_records({BindingRecord("person.1", first)});
    (*grid).data_source = source;
    (*grid).allow_user_to_add_rows = true;
    window.perform_layout();
    require((*grid).row_count() == 2U && (*grid).new_row_index() == row_at(1U), "a bound grid shows the new row");
    const DataGridViewRowId placeholder = (*grid).row_id(row_at(1U));
    require((*grid).set_current_cell(cell_at(1U, 1U)) && (*grid).begin_edit(), "edit the new row");
    (*(*grid).editing_control()).text = "Paris";
    require((*grid).end_edit() && (*source).count() == 2U && (*source).records()[1].stable_id == placeholder.value &&
                (*grid).cell_text(row_at(1U), 1U) == "Paris" && (*grid).row_count() == 3U,
            "the commit adds a record with the new row's ID and the value");
    (*source).set_allow_new(false);
    window.perform_layout();
    require((*grid).row_count() == 2U && !(*grid).new_row_index(), "a source without additions has no new row");
}

void deleting_rows() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.delete");
    (*grid).selection_mode = DataGridViewSelectionMode::full_row_select;
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require(window.request_focus(grid), "focus");
    (*grid).allow_user_to_delete_rows = false;
    require((*grid).set_current_cell(cell_at(1U, 0U)) && !key(window, PhysicalKey::delete_forward) &&
                (*grid).row_count() == 3U,
            "Delete does nothing unless deleting is allowed");
    (*grid).allow_user_to_delete_rows = true;
    int asked = 0;
    std::vector<DataGridViewRowEvent> deleted;
    SubscriptionToken deleting_token = (*grid).user_deleting_row().subscribe(KeepRow{"charlie", &asked});
    SubscriptionToken deleted_token = (*grid).user_deleted_row().subscribe(RecordRow{&deleted});
    (*grid).set_row_selected(row_at(2U), true);
    require(key(window, PhysicalKey::delete_forward) && asked == 2 && deleted.size() == 1U &&
                deleted[0].row_id == id("bravo") && deleted[0].row == row_at(1U) && (*grid).row_count() == 2U &&
                (*grid).row_position(id("charlie")) == row_at(1U),
            "Delete removes the selected rows a handler does not keep");

    std::shared_ptr<DataGridView> bound = make_control<DataGridView>(StableId("grid.delete-bound"));
    (*bound).set_columns({{.name = "name", .header_text = "Name", .data_property_name = "Name"}});
    (*bound).selection_mode = DataGridViewSelectionMode::full_row_select;
    Window bound_window(bound, {300.0, 160.0});
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(bound_window);
    BindingRecord::FieldMap fields;
    fields["Name"] = std::string("Ada");
    (*source).set_records({BindingRecord("p.1", fields), BindingRecord("p.2", fields)});
    (*bound).data_source = source;
    (*bound).allow_user_to_delete_rows = true;
    bound_window.perform_layout();
    require(bound_window.request_focus(bound) && (*bound).set_current_cell(cell_at(0U, 0U)), "bound focus");
    require(key(bound_window, PhysicalKey::delete_forward) && (*source).count() == 1U &&
                (*source).records()[0].stable_id == "p.2",
            "Delete removes a bound row's record");
}

void error_text() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.error-text");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    std::vector<DataGridViewCellEvent> cell_changes;
    std::vector<DataGridViewRowEvent> row_changes;
    SubscriptionToken cell_token = (*grid).cell_error_text_changed().subscribe(RecordCell{&cell_changes});
    SubscriptionToken row_token = (*grid).row_error_text_changed().subscribe(RecordRow{&row_changes});
    (*grid).set_cell_error_text(row_at(1U), 0U, "Name is required");
    (*grid).set_row_error_text(row_at(1U), "Row has errors");
    window.perform_layout();
    require((*grid).cell_error_text(row_at(1U), 0U) == "Name is required" &&
                (*grid).row_error_text(row_at(1U)) == "Row has errors" && cell_changes.size() == 1U &&
                row_changes.size() == 1U,
            "error text is kept and reported");
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* errored = find_cell(cells, 1U, 0U);
    const DataGridView::Cell* clean = find_cell(cells, 0U, 0U);
    require(errored != nullptr && (*errored).parts.cell_error.present && (*errored).states.invalid &&
                (*errored).parts.cell_content.bounds.width < (*clean).parts.cell_content.bounds.width &&
                !(*clean).parts.cell_error.present,
            "a cell with error text shows the error glyph and is invalid");
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows[1].parts.row_error.present && rows[1].states.invalid && !rows[0].parts.row_error.present,
            "a row with error text shows the glyph in its header");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    bool described = false;
    for (const SemanticNode& node : nodes) {
        if (node.stable_id != "bravo") continue;
        bool cell_described = false;
        for (const SemanticNode& child : node.children) {
            if (child.stable_id == "bravo.name") {
                cell_described = child.description == "Name is required" &&
                                 has_semantic_state(child.states, SemanticState::invalid);
            }
        }
        described = cell_described && node.description == "Row has errors";
    }
    require(described, "error text is the semantic description of the cell and row");
    (*grid).show_cell_errors = false;
    window.perform_layout();
    const std::vector<DataGridView::Cell> hidden = (*grid).realized_cells();
    const DataGridView::Cell* quiet = find_cell(hidden, 1U, 0U);
    require(quiet != nullptr && !(*quiet).parts.cell_error.present, "show_cell_errors hides the glyph");
    (*grid).rows.erase(1U, 1U);
    require((*grid).row_count() == 2U && (*grid).cell_error_text(row_at(1U), 0U).empty(),
            "error text ends with its row");
}

} // namespace

// -- batch 2: styles and formatting ---------------------------------------------

struct SetBadFormat final {
    DataGridView* grid{};
    void operator()() const { (*grid).default_cell_style = {.format = std::string("X2")}; }
};
struct SetNegativePadding final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_cell_style(row_at(0U), 0U, {.padding = Insets{-1.0, 0.0, 0.0, 0.0}}); }
};
struct StyleCellPastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_cell_style(row_at(9U), 0U, {}); }
};

// Gives "bravo"'s name its own text and puts "alpha"'s name at the top.
struct FormatNames final {
    int* calls{};
    void operator()(DataGridViewCellFormattingEvent& event) const {
        ++*calls;
        if (event.column != 0U) return;
        if (event.row_id.value == "bravo") {
            event.formatted_value = "B!";
            event.formatting_applied = true;
        }
        if (event.row_id.value == "alpha") event.cell_style.alignment = DataGridViewContentAlignment::top_start;
    }
};

// Parses "*" as the text "star".
struct ParseStar final {
    void operator()(DataGridViewCellParsingEvent& event) const {
        if (event.formatted_value != "*") return;
        event.value = BindingValue{std::string("star")};
        event.parsing_applied = true;
    }
};

struct DrawCells final {
    std::vector<std::string>* ids{};
    bool draw_default{};
    void operator()(ItemContentPaint& item) const {
        (*ids).emplace_back(item.stable_id);
        item.draw_default = draw_default;
    }
};

// Replaces the grid's columns from inside its paint.
struct ReplaceColumns final {
    DataGridView* grid{};
    int* calls{};
    void operator()(ItemContentPaint&) const {
        ++*calls;
        (*grid).set_columns({{.name = "only", .header_text = "Only"}});
    }
};

void cell_styles() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.styles");
    Window window(grid, {360.0, 200.0});
    window.perform_layout();
    const DataGridViewCellStyle empty{};
    require((*grid).default_cell_style.read() == empty && (*grid).inherited_cell_style(row_at(0U), 0U) == empty &&
                (*grid).inherited_column_header_style(0U) == empty,
            "every style is empty by default: an unstyled grid is the theme's");
    const FontSpec big{FontRole::content, 15.0, 700, false};
    (*grid).default_cell_style = {.font = big, .alignment = DataGridViewContentAlignment::middle_start};
    (*grid).set_column_default_cell_style(0U, {.alignment = DataGridViewContentAlignment::middle_end});
    (*grid).rows_default_cell_style = {.wrap_mode = true};
    (*grid).alternating_rows_default_cell_style = {.fore_color = Color{1, 2, 3, 255}};
    (*grid).set_row_default_cell_style(row_at(2U), {.alignment = DataGridViewContentAlignment::bottom_center});
    (*grid).set_cell_style(row_at(2U), 0U, {.padding = Insets{10.0, 2.0, 10.0, 2.0}});
    const DataGridViewCellStyle first = (*grid).inherited_cell_style(row_at(0U), 0U);
    require(first.font == big && first.alignment == DataGridViewContentAlignment::middle_end &&
                first.wrap_mode == true && !first.fore_color,
            "a cell inherits the column's alignment over the grid's, the rows' wrap mode and the grid's font");
    const DataGridViewCellStyle second = (*grid).inherited_cell_style(row_at(1U), 1U);
    require(second.fore_color == Color{1, 2, 3, 255} && second.alignment == DataGridViewContentAlignment::middle_start,
            "odd positions take the alternating rows' style; another column the grid's alignment");
    const DataGridViewCellStyle third = (*grid).inherited_cell_style(row_at(2U), 0U);
    require(third.alignment == DataGridViewContentAlignment::bottom_center && third.padding &&
                (*third.padding).left == 10.0 && (*grid).cell_style(row_at(2U), 0U).padding &&
                !(*grid).cell_style(row_at(2U), 0U).alignment,
            "a row's style wins over the column's, a cell's own over its row's");
    (*grid).column_headers_default_cell_style = {.alignment = DataGridViewContentAlignment::middle_center};
    (*grid).set_column_header_cell_style(1U, {.alignment = DataGridViewContentAlignment::middle_end});
    require((*grid).inherited_column_header_style(0U).alignment == DataGridViewContentAlignment::middle_center &&
                (*grid).inherited_column_header_style(1U).alignment == DataGridViewContentAlignment::middle_end &&
                (*grid).inherited_column_header_style(0U).font == big && (*grid).inherited_row_header_style().font == big,
            "headers inherit their own style, the headers', then the grid's");
    require(throws<std::invalid_argument>(SetBadFormat{grid.get()}) &&
                throws<std::invalid_argument>(SetNegativePadding{grid.get()}) &&
                throws<std::out_of_range>(StyleCellPastEnd{grid.get()}),
            "a format that is not a standard numeric format, a negative padding and a cell past the rows are "
            "refused");

    // Styles are kept by row ID: they follow their rows and end with them.
    require((*grid).sort(0U, DataGridViewSortDirection::descending) &&
                (*grid).row_position(id("charlie")) == row_at(0U) &&
                (*grid).cell_style(row_at(0U), 0U).padding && (*grid).row_default_cell_style(row_at(0U)).alignment,
            "sorting moves a row's and a cell's styles with the row");
    (*grid).rows.erase(0U, 1U);
    require(!(*grid).row_default_cell_style(row_at(0U)).alignment, "a removed row's styles end with it");

    // Geometry: padding and alignment place the content part.
    std::shared_ptr<DataGridView> placed = make_grid("grid.styles.placed");
    Window placed_window(placed, {360.0, 200.0});
    (*placed).row_height = 40.0;
    placed_window.perform_layout();
    const std::vector<DataGridView::Cell> before = (*placed).realized_cells();
    const DataGridView::Cell* plain = find_cell(before, 0U, 0U);
    require(plain != nullptr && (*plain).parts.cell_content.present, "a text cell's content");
    const Rect plain_content = (*plain).parts.cell_content.bounds;
    (*placed).set_cell_style(row_at(0U), 0U, {.alignment = DataGridViewContentAlignment::top_start,
                                              .padding = Insets{12.0, 3.0, 12.0, 3.0}});
    placed_window.perform_layout();
    const std::vector<DataGridView::Cell> after = (*placed).realized_cells();
    const DataGridView::Cell* styled = find_cell(after, 0U, 0U);
    require(styled != nullptr && (*styled).parts.cell_content.bounds.x == plain_content.x + 8.0 &&
                (*styled).parts.cell_content.bounds.y == plain_content.y + 3.0 &&
                (*styled).parts.cell_content.bounds.height < plain_content.height - 6.0,
            "padding moves the content in and top alignment gives it one line at the top");
}

void formatting_and_parsing() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.format"));
    (*grid).allow_user_to_add_rows = false;
    (*grid).set_columns({{.name = "price", .header_text = "Price", .value_kind = BindingValueKind::number},
                         {.name = "note", .header_text = "Note"}});
    (*grid).rows.append({.stable_id = {"a"}, .cells = {BindingValue{3.14159}, BindingValue{std::string("x")}}});
    (*grid).rows.append({.stable_id = {"b"}, .cells = {BindingValue{}, BindingValue{}}});
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).cell_text(row_at(0U), 0U) == "3.14159", "an unformatted number shows as it is");
    (*grid).set_column_default_cell_style(0U, {.format = std::string("F2"),
                                               .null_value = BindingValue{std::string("(none)")},
                                               .data_source_null_value = BindingValue{-1.0}});
    require((*grid).cell_text(row_at(0U), 0U) == "3.14" && (*grid).cell_text(row_at(1U), 0U) == "(none)",
            "the style's fixed format and null value give the cell's text");
    require(window.request_focus(grid) && (*grid).set_current_cell(cell_at(0U, 0U)) && (*grid).begin_edit() &&
                (*(*grid).editing_control()).text.read() == "3.14",
            "an edit begins with the formatted text");
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 0U) == BindingValue{3.14159},
            "an edit the user did not change stores nothing");
    require((*grid).set_current_cell(cell_at(1U, 0U)) && (*grid).set_current_cell(cell_at(0U, 0U)) &&
                (*grid).begin_edit(),
            "edit the number again");
    (*(*grid).editing_control()).text = "(none)";
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 0U) == BindingValue{-1.0},
            "the null value's text stores the data source null value");

    // The standard numeric formats beyond F, and typed text in them.
    (*grid).set_column_default_cell_style(0U, {.format = std::string("N2")});
    require((*grid).cell_text(row_at(0U), 0U) == "-1.00", "N2 formats the number");
    require((*grid).set_current_cell(cell_at(1U, 0U)) && (*grid).set_current_cell(cell_at(0U, 0U)) &&
                (*grid).begin_edit(),
            "edit the number under N2");
    (*(*grid).editing_control()).text = "1,234.5";
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 0U) == BindingValue{1234.5} &&
                (*grid).cell_text(row_at(0U), 0U) == "1,234.50",
            "grouped text stores the number it shows");
    (*grid).set_column_default_cell_style(0U, {.format = std::string("C")});
    require((*grid).cell_text(row_at(0U), 0U) == "\xC2\xA4" "1,234.50", "C formats with the currency sign");
    (*grid).set_column_default_cell_style(0U, {.format = std::string("P0")});
    require((*grid).cell_text(row_at(0U), 0U) == "123,450 %", "P0 formats the hundredfold value");
    require((*grid).set_current_cell(cell_at(1U, 0U)) && (*grid).set_current_cell(cell_at(0U, 0U)) &&
                (*grid).begin_edit(),
            "edit the number under P0");
    (*(*grid).editing_control()).text = "50 %";
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 0U) == BindingValue{0.5},
            "a percent text stores its hundredth");
    (*grid).set_column_default_cell_style(0U, {.format = std::string("E2")});
    require((*grid).cell_text(row_at(0U), 0U) == "5.00E-001", "E2 formats in the scientific form");

    int formatted = 0;
    SubscriptionToken format_token = (*grid).cell_formatting().subscribe(FormatNames{&formatted});
    SubscriptionToken parse_token = (*grid).cell_parsing().subscribe(ParseStar{});
    require((*grid).cell_text(row_at(0U), 1U) == "x" && formatted > 0, "cell_formatting is raised for cell text");
    require((*grid).set_current_cell(cell_at(0U, 1U)) && (*grid).begin_edit(), "edit the note");
    (*(*grid).editing_control()).text = "*";
    require((*grid).end_edit() && (*grid).cell_value(row_at(0U), 1U) == BindingValue{std::string("star")},
            "a cell_parsing handler gives the stored value");

    std::shared_ptr<DataGridView> named = make_grid("grid.format.named");
    Window named_window(named, {360.0, 200.0});
    (*named).row_height = 40.0;
    named_window.perform_layout();
    const std::vector<DataGridView::Cell> plain = (*named).realized_cells();
    const DataGridView::Cell* plain_alpha = find_cell(plain, 0U, 0U);
    require(plain_alpha != nullptr, "alpha's name");
    const double plain_height = (*plain_alpha).parts.cell_content.bounds.height;
    int named_calls = 0;
    SubscriptionToken named_token = (*named).cell_formatting().subscribe(FormatNames{&named_calls});
    require((*named).cell_text(row_at(1U), 0U) == "B!", "a handler's formatted value is the cell's text");
    const std::vector<DataGridView::Cell> cells = (*named).realized_cells();
    const DataGridView::Cell* alpha = find_cell(cells, 0U, 0U);
    require(alpha != nullptr && (*alpha).parts.cell_content.bounds.height < plain_height,
            "the style a handler leaves places the content");
    CountingPainter painter;
    (*named).on_paint(painter, (*named).client_rectangle());
    bool painted_handler_text = false;
    for (const std::string& drawn : painter.texts) {
        if (drawn == "B!") painted_handler_text = true;
    }
    require(painted_handler_text, "the paint draws the handler's text");
}

void borders_and_owner_drawing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.borders");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).border_style.read() == BorderStyle::line && (*grid).parts().edge.present &&
                (*grid).cell_border_style.read() == DataGridViewCellBorderStyle::single &&
                (*grid).column_headers_border_style.read() == DataGridViewHeaderBorderStyle::raised,
            "a grid has a frame, single cell lines and raised header lines by default");
    (*grid).border_style = BorderStyle::none;
    (*grid).cell_border_style = DataGridViewCellBorderStyle::single_vertical;
    (*grid).column_headers_border_style = DataGridViewHeaderBorderStyle::none;
    (*grid).row_headers_border_style = DataGridViewHeaderBorderStyle::none;
    window.perform_layout();
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* cell = find_cell(cells, 0U, 0U);
    const std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(!(*grid).parts().edge.present && cell != nullptr && (*cell).parts.cell_vertical_rule.present &&
                !(*cell).parts.cell_horizontal_rule.present,
            "no frame, and vertical cell lines alone");
    require(!columns[0].parts.column_divider.present && columns[0].parts.column_grip.present &&
                !rows[0].parts.row_rule.present,
            "header lines go; a column header keeps its resize grip");
    (*grid).cell_border_style = DataGridViewCellBorderStyle::raised;
    window.perform_layout();
    const std::vector<DataGridView::Cell> raised = (*grid).realized_cells();
    const DataGridView::Cell* raised_cell = find_cell(raised, 0U, 0U);
    require(raised_cell != nullptr && (*raised_cell).parts.cell_vertical_rule.present &&
                (*raised_cell).parts.cell_horizontal_rule.present,
            "raised lines are the same dividers as single ones");

    std::vector<std::string> drawn_ids;
    SubscriptionToken draw_token = (*grid).draw_item_content().subscribe(DrawCells{&drawn_ids, false});
    (*grid).draw_mode = ItemDrawMode::owner_draw;
    CountingPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    bool painted_alpha = false;
    for (const std::string& drawn : painter.texts) {
        if (drawn == "Alpha") painted_alpha = true;
    }
    require(drawn_ids.size() == 12U && drawn_ids[0] == "alpha.name" && !painted_alpha,
            "owner drawing raises each cell and replaces its content");
    draw_token.disconnect();
    drawn_ids.clear();
    SubscriptionToken default_token = (*grid).draw_item_content().subscribe(DrawCells{&drawn_ids, true});
    CountingPainter second;
    (*grid).on_paint(second, (*grid).client_rectangle());
    bool painted_again = false;
    for (const std::string& drawn : second.texts) {
        if (drawn == "Alpha") painted_again = true;
    }
    require(painted_again, "draw_default keeps the default content");
    default_token.disconnect();

    int replaced = 0;
    SubscriptionToken replace_token = (*grid).draw_item_content().subscribe(ReplaceColumns{grid.get(), &replaced});
    CountingPainter third;
    (*grid).on_paint(third, (*grid).client_rectangle());
    require(replaced == 1 && (*grid).columns().size() == 1U, "a handler that changes the grid stops the paint");
}

// -- authored colours (owner decision T4-1) ------------------------------------

// Records the colours fills, text and lines are drawn in.
class ColourPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override { fills.push_back(color); }
    void fill_rounded_rect(Rect, double, Color color) override { fills.push_back(color); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color color, double) override { lines.push_back(color); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color color) override { texts.push_back(color); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<Color> fills;
    std::vector<Color> lines;
    std::vector<Color> texts;
};

bool contains_colour(const std::vector<Color>& colours, Color wanted) {
    for (const Color& colour : colours) {
        if (colour == wanted) return true;
    }
    return false;
}

void authored_colours() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.colours");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    const Color back{250, 240, 200, 255};
    const Color fore{120, 10, 20, 255};
    const Color line{10, 130, 40, 255};
    const Color highlight{20, 30, 160, 255};
    const Color header_back{60, 60, 60, 255};
    const Color header_fore{240, 240, 240, 255};
    const Color empty{200, 200, 220, 255};
    std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    require(!(*find_cell(cells, 1U, 0U)).parts.cell_selection.authored_background &&
                !(*find_cell(cells, 1U, 0U)).parts.cell_content.authored_foreground &&
                !(*grid).parts().well.authored_background,
            "an unstyled grid authors no colours");

    (*grid).default_cell_style = {.back_color = back, .fore_color = fore};
    (*grid).grid_color = line;
    window.perform_layout();
    cells = (*grid).realized_cells();
    const DataGridView::Cell text = *find_cell(cells, 1U, 0U);
    require(text.parts.cell_selection.authored_background == back &&
                text.parts.cell_content.authored_foreground == fore &&
                text.parts.cell_vertical_rule.authored_foreground == line &&
                text.parts.cell_horizontal_rule.authored_foreground == line,
            "a cell's background, text and lines take the inherited style's and the grid's colours");
    require((*find_cell(cells, 1U, 3U)).parts.cell_button_label.authored_foreground == fore,
            "a button cell's label takes the fore colour");
    ColourPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    require(contains_colour(painter.fills, back) && contains_colour(painter.texts, fore) &&
                contains_colour(painter.lines, line),
            "the paint fills, writes and rules in the authored colours");

    (*grid).set_cell_selected(row_at(1U), 0U, true);
    window.perform_layout();
    cells = (*grid).realized_cells();
    require(!(*find_cell(cells, 1U, 0U)).parts.cell_selection.authored_background,
            "a selected cell with no selection colour keeps the theme's selection look");
    (*grid).default_cell_style = {.back_color = back, .fore_color = fore, .selection_back_color = highlight};
    window.perform_layout();
    cells = (*grid).realized_cells();
    require((*find_cell(cells, 1U, 0U)).parts.cell_selection.authored_background == highlight,
            "a selected cell shows the selection back colour");

    (*grid).column_headers_default_cell_style = {.back_color = header_back, .fore_color = header_fore};
    (*grid).row_headers_default_cell_style = {.back_color = header_back, .fore_color = header_fore};
    window.perform_layout();
    require(!(*grid).realized_columns()[0].parts.column_header.authored_background &&
                !(*grid).realized_rows()[0].parts.row_header.authored_background,
            "with header visual styles the theme draws the headers");
    (*grid).enable_headers_visual_styles = false;
    window.perform_layout();
    const std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(columns[0].parts.column_header.authored_background == header_back &&
                columns[0].parts.column_label.authored_foreground == header_fore &&
                rows[0].parts.row_header.authored_background == header_back,
            "without them the header styles' colours stand on the headers");

    (*grid).background_color = empty;
    window.perform_layout();
    ColourPainter background;
    (*grid).on_paint(background, (*grid).client_rectangle());
    require((*grid).parts().well.authored_background == empty && contains_colour(background.fills, empty),
            "the background colour fills the area no cell covers");
}

// -- wrapped text (batch 2b) ---------------------------------------------------

// One unit per byte, so a run's width is its length.
struct ByteWidth final {
    double operator()(std::string_view run) const { return static_cast<double>(run.size()); }
};

// The wrapped lines' texts.
std::vector<std::string> wrap(std::string_view text, const std::vector<LineBreakOpportunity>& opportunities,
                              double width) {
    std::vector<line_wrapping_detail::WrappedLine> lines;
    const ByteWidth measure{};
    line_wrapping_detail::wrap_lines_greedy(text, std::span<const LineBreakOpportunity>(opportunities), width,
                                            measure, lines);
    std::vector<std::string> texts;
    for (const line_wrapping_detail::WrappedLine& line : lines) {
        texts.emplace_back(text.substr(line.begin_byte, line.end_byte - line.begin_byte));
    }
    return texts;
}

// The opportunities of `text` with breaks allowed after spaces and
// mandatory after line ends.
std::vector<LineBreakOpportunity> simple_opportunities(std::string_view text) {
    std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (text[index] == ' ') opportunities[index] = LineBreakOpportunity::allowed;
        if (text[index] == '\n') opportunities[index] = LineBreakOpportunity::mandatory;
    }
    return opportunities;
}

void line_wrapping() {
    const std::string_view words = "aa bb cc";
    require(wrap(words, simple_opportunities(words), 5.0) == std::vector<std::string>{"aa bb", "cc"},
            "lines take the longest run that fits, trailing spaces hung");
    const std::string_view long_word = "aaaaaaa bb";
    require(wrap(long_word, simple_opportunities(long_word), 3.0) == std::vector<std::string>{"aaaaaaa", "bb"},
            "a run with no opportunity overflows on a line of its own");
    const std::string_view ends = "a\n\nb";
    require(wrap(ends, simple_opportunities(ends), 100.0) == std::vector<std::string>{"a", "", "b"},
            "a line end always ends the line, an empty one included");
    require(wrap(words, {}, 2.0) == std::vector<std::string>{"aa bb cc"},
            "opportunities of another length leave one line");
    require(wrap("", {}, 2.0).empty(), "no text has no lines");
}

void wrapped_cells() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.wrap");
    Window window(grid, {360.0, 200.0});
    static_cast<void>((*grid).set_cell_value(row_at(0U), 0U, BindingValue{std::string("Kilo Lima Mike November")}));
    static_cast<void>((*grid).set_cell_value(row_at(1U), 0U, BindingValue{std::string("Oscar\nPapa")}));
    window.perform_layout();
    CountingPainter single;
    (*grid).on_paint(single, (*grid).client_rectangle());
    bool whole = false;
    for (const std::string& drawn : single.texts) {
        if (drawn == "Kilo Lima Mike November") whole = true;
    }
    require(whole, "without wrap_mode the text is one line");

    (*grid).set_column_default_cell_style(0U, {.wrap_mode = true});
    window.perform_layout();
    CountingPainter wrapped;
    (*grid).on_paint(wrapped, (*grid).client_rectangle());
    std::string joined;
    std::size_t pieces = 0U;
    bool oscar = false;
    bool papa = false;
    for (const std::string& drawn : wrapped.texts) {
        if (drawn == "Oscar") oscar = true;
        if (drawn == "Papa") papa = true;
        if (drawn.empty() || std::string_view("Kilo Lima Mike November").find(drawn) == std::string_view::npos) {
            continue;
        }
        if (!joined.empty()) joined.push_back(' ');
        joined.append(drawn);
        ++pieces;
    }
    require(pieces >= 2U && joined == "Kilo Lima Mike November",
            "with wrap_mode the text is drawn as lines at the column's width, in order");
    require(oscar && papa, "a line end in the text starts a new line");
    std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* first = find_cell(cells, 0U, 0U);
    const Rect inside = (*first).parts.cell_selection.bounds;
    require((*first).parts.cell_content.present && (*first).parts.cell_content.bounds.y >= inside.y &&
                (*first).parts.cell_content.bounds.y + (*first).parts.cell_content.bounds.height <=
                    inside.y + inside.height + 0.001,
            "the content part is the lines' box, cut to the cell");
}

// -- automatic column sizing ---------------------------------------------------

struct SetFillWeight final {
    DataGridView* grid{};
    double weight{};
    void operator()() const { (*grid).set_column_fill_weight(0U, weight); }
};
struct ResizeColumnsToFill final {
    DataGridView* grid{};
    void operator()() const { (*grid).auto_resize_columns(DataGridViewAutoSizeColumnsMode::fill); }
};

// The widest preferred width of a column's cells in rows [begin, end), and
// its header's when `header`, at least the column's minimum.
double expected_width(const DataGridView& grid, std::size_t column, std::size_t begin, std::size_t end,
                      bool header) {
    double widest = header ? grid.preferred_column_header_width(column) : 0.0;
    for (std::size_t position = begin; position < end; ++position) {
        widest = std::max(widest, grid.preferred_cell_width(row_at(position), column));
    }
    const double result = std::max(grid.columns()[column].minimum_width, widest);
    return result;
}

void column_auto_sizing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.autosize");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).auto_size_columns_mode.read() == DataGridViewAutoSizeColumnsMode::none &&
                (*grid).column_auto_size_mode(0U) == DataGridViewAutoSizeColumnMode::none &&
                (*grid).columns()[0].width == 100.0,
            "columns keep their widths by default");
    require((*grid).preferred_cell_width(row_at(0U), 1U) > 13.0 &&
                (*grid).preferred_cell_width(row_at(0U), 0U) > (*grid).preferred_cell_width(row_at(2U), 1U) &&
                (*grid).preferred_column_header_width(0U) > 0.0,
            "cells and headers have preferred widths");

    (*grid).set_column_auto_size_mode(0U, DataGridViewAutoSizeColumnMode::all_cells);
    window.perform_layout();
    require((*grid).columns()[0].width == expected_width(*grid, 0U, 0U, 3U, true) &&
                !(*grid).realized_columns()[0].parts.column_grip.present &&
                (*grid).realized_columns()[1].parts.column_grip.present,
            "an all-cells column takes its widest cell's width, and the user cannot resize it");
    const double before = (*grid).columns()[0].width;
    static_cast<void>((*grid).set_cell_value(
        row_at(2U), 0U, BindingValue{std::string("Charlie with a much longer name than the others")}));
    window.perform_layout();
    require((*grid).columns()[0].width > before &&
                (*grid).columns()[0].width == expected_width(*grid, 0U, 0U, 3U, true),
            "a value change sizes the column again");

    (*grid).set_column_auto_size_mode(0U, DataGridViewAutoSizeColumnMode::column_header);
    window.perform_layout();
    require((*grid).columns()[0].width ==
                std::max((*grid).columns()[0].minimum_width, (*grid).preferred_column_header_width(0U)),
            "a column_header column takes its header's width");

    std::shared_ptr<DataGridView> short_grid = make_grid("grid.autosize.displayed");
    Window short_window(short_grid, {360.0, 50.0});
    static_cast<void>((*short_grid).set_cell_value(
        row_at(2U), 0U, BindingValue{std::string("A row out of view with a much longer name")}));
    (*short_grid).auto_size_columns_mode = DataGridViewAutoSizeColumnsMode::displayed_cells_except_header;
    short_window.perform_layout();
    const std::size_t shown = (*short_grid).displayed_row_end().value;
    require(shown < 3U && (*short_grid).column_auto_size_mode(0U) ==
                              DataGridViewAutoSizeColumnMode::displayed_cells_except_header &&
                (*short_grid).columns()[0].width == expected_width(*short_grid, 0U, 0U, shown, false) &&
                (*short_grid).columns()[0].width < expected_width(*short_grid, 0U, 0U, 3U, false),
            "a displayed-cells column measures the rows shown, by the grid's default mode");

    (*grid).set_column_auto_size_mode(0U, DataGridViewAutoSizeColumnMode::none);
    (*grid).set_column_auto_size_mode(2U, DataGridViewAutoSizeColumnMode::fill);
    (*grid).set_column_auto_size_mode(3U, DataGridViewAutoSizeColumnMode::fill);
    (*grid).set_column_fill_weight(3U, 300.0);
    window.perform_layout();
    const std::span<const DataGridViewColumn> columns = (*grid).columns();
    const double total = columns[0].width + columns[1].width + columns[2].width + columns[3].width;
    const Rect data = (*grid).parts().column_header_band.bounds;
    require(std::abs(total - data.width) < 0.01 && std::abs(columns[3].width - columns[2].width * 3.0) < 0.01,
            "fill columns share the width the others leave, by weight");

    std::vector<std::size_t> widths;
    SubscriptionToken width_token = (*grid).column_width_changed().subscribe(RecordIndex{&widths});
    (*grid).auto_resize_column(1U, DataGridViewAutoSizeColumnMode::all_cells_except_header);
    require((*grid).columns()[1].width == expected_width(*grid, 1U, 0U, 3U, false) && widths.size() == 1U &&
                widths[0] == 1U && (*grid).column_auto_size_mode(1U) == DataGridViewAutoSizeColumnMode::none,
            "auto_resize_column sizes once and raises column_width_changed");
    require(throws<std::invalid_argument>(SetFillWeight{grid.get(), 0.0}) &&
                throws<std::invalid_argument>(ResizeColumnsToFill{grid.get()}),
            "a weight that is not positive and a one-time fill are refused");
}

// -- column order --------------------------------------------------------------

struct SetDisplayIndexPastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_column_display_index(0U, DataGridViewDisplayIndex{9U}); }
};

struct SetCurrentToHidden final {
    DataGridView* grid{};
    void operator()() const { static_cast<void>((*grid).set_current_cell(cell_at(0U, 1U))); }
};

struct SetVisiblePastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_column_visible(9U, false); }
};

struct InsertColumnPastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).insert_column(9U, {.name = "late"}); }
};

struct InsertDuplicateColumn final {
    DataGridView* grid{};
    void operator()() const { (*grid).insert_column(0U, {.name = "name"}); }
};

struct SetInvalidHeaderText final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_column_header_text(0U, std::string("\xC3")); }
};

std::vector<std::size_t> display_order(const DataGridView& grid) {
    std::vector<std::size_t> order;
    for (std::size_t display = 0U; display < grid.columns().size(); ++display) {
        order.push_back(grid.column_at_display_index(DataGridViewDisplayIndex{display}));
    }
    return order;
}

void column_visibility() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.visible");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).columns()[1].visible, "columns are shown by default");
    (*grid).set_column_visible(1U, false);
    window.perform_layout();
    const std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    require(columns.size() == 3U && columns[0].index == 0U && columns[1].index == 2U && columns[2].index == 3U,
            "a hidden column is not realized");
    const Rect first = columns[0].parts.column_header.bounds;
    require(columns[1].parts.column_header.bounds.x == first.x + first.width,
            "the column after a hidden one takes its place");
    require(display_order(*grid) == std::vector<std::size_t>{0U, 1U, 2U, 3U} &&
                !(*grid).columns()[1].visible,
            "a hidden column keeps its display index");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    require(nodes[1].role == SemanticRole::column_header && nodes[1].column_index == 2U,
            "column headers count only shown columns");
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* const shown = find_cell(cells, 0U, 2U);
    require(shown != nullptr && (*shown).parts.cell.bounds.x == first.x + first.width,
            "cells after a hidden column are laid out in its place");

    require(window.request_focus(grid) && (*grid).current_cell() == cell_at(0U, 0U),
            "focus makes the first shown cell current");
    require(key(window, PhysicalKey::right) && (*grid).current_cell() == cell_at(0U, 2U),
            "Right passes over a hidden column");
    require(key(window, PhysicalKey::left) && (*grid).current_cell() == cell_at(0U, 0U) &&
                key(window, PhysicalKey::tab) && (*grid).current_cell() == cell_at(0U, 2U),
            "Left and Tab pass over a hidden column");
    require(key(window, PhysicalKey::left, Modifier::shift) && (*grid).is_cell_selected(row_at(0U), 0U) &&
                (*grid).is_cell_selected(row_at(0U), 2U) && !(*grid).is_cell_selected(row_at(0U), 1U),
            "a range leaves a hidden column's cells out");
    (*grid).multi_select = true;
    (*grid).select_all();
    require(!(*grid).is_cell_selected(row_at(1U), 1U) && (*grid).is_cell_selected(row_at(1U), 3U),
            "select_all leaves a hidden column's cells out");
    require(throws<std::logic_error>(SetCurrentToHidden{grid.get()}), "a hidden cell cannot be made current");
    require(throws<std::out_of_range>(SetVisiblePastEnd{grid.get()}), "an unknown column is refused");

    require((*grid).set_current_cell(cell_at(0U, 2U)), "the current cell moves to a shown column");
    (*grid).set_column_visible(2U, false);
    require((*grid).current_cell() == cell_at(0U, 3U), "hiding the current column moves to the next shown one");
    (*grid).set_column_visible(3U, false);
    require((*grid).current_cell() == cell_at(0U, 0U), "past the last shown column it moves to the one before");
    (*grid).set_column_visible(0U, false);
    window.perform_layout();
    require(!(*grid).current_cell() && (*grid).realized_columns().empty(),
            "with no column shown there is no current cell and nothing realized");
    require(!key(window, PhysicalKey::right), "keys move nothing with no column shown");
    for (std::size_t column = 0U; column < 4U; ++column) (*grid).set_column_visible(column, true);
    window.perform_layout();
    require((*grid).realized_columns().size() == 4U, "shown again, every column is realized");

    (*grid).add_column({.name = "secret", .header_text = "Secret", .visible = false});
    window.perform_layout();
    require((*grid).realized_columns().size() == 4U && !(*grid).columns()[4].visible,
            "a column added hidden is not realized");
}

void column_order() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.order");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require(display_order(*grid) == std::vector<std::size_t>{0U, 1U, 2U, 3U} &&
                (*grid).column_display_index(2U) == DataGridViewDisplayIndex{2U} &&
                !(*grid).allow_user_to_order_columns.read(),
            "columns show in index order, and the user does not reorder them, by default");
    std::vector<std::size_t> moved;
    SubscriptionToken moved_token = (*grid).column_display_index_changed().subscribe(RecordIndex{&moved});
    (*grid).set_column_display_index(0U, DataGridViewDisplayIndex{2U});
    window.perform_layout();
    require(display_order(*grid) == std::vector<std::size_t>{1U, 2U, 0U, 3U} &&
                moved == std::vector<std::size_t>{1U, 2U, 0U},
            "moving a column shifts the ones between and reports each, in their new order");
    const std::vector<DataGridView::Column> columns = (*grid).realized_columns();
    const Rect data = (*grid).parts().column_header_band.bounds;
    require(columns.size() == 4U && columns[0].index == 1U && columns[2].index == 0U &&
                columns[0].parts.column_header.bounds.x == data.x &&
                columns[1].parts.column_header.bounds.x > columns[0].parts.column_header.bounds.x,
            "the headers are laid out in display order");
    require(throws<std::out_of_range>(SetDisplayIndexPastEnd{grid.get()}), "a display index past the last is refused");

    require(window.request_focus(grid) && (*grid).current_cell() == cell_at(0U, 1U),
            "focus makes the first cell shown current");
    require(key(window, PhysicalKey::right) && (*grid).current_cell() == cell_at(0U, 2U) &&
                key(window, PhysicalKey::right) && (*grid).current_cell() == cell_at(0U, 0U) &&
                key(window, PhysicalKey::end) && (*grid).current_cell() == cell_at(0U, 3U) &&
                key(window, PhysicalKey::home) && (*grid).current_cell() == cell_at(0U, 1U),
            "Right, End and Home walk the display order");
    require(key(window, PhysicalKey::tab) && (*grid).current_cell() == cell_at(0U, 2U),
            "Tab walks the display order");
    require(key(window, PhysicalKey::right) && key(window, PhysicalKey::left, Modifier::shift) &&
                key(window, PhysicalKey::left, Modifier::shift) && (*grid).is_cell_selected(row_at(0U), 1U) &&
                (*grid).is_cell_selected(row_at(0U), 2U) && (*grid).is_cell_selected(row_at(0U), 0U) &&
                !(*grid).is_cell_selected(row_at(0U), 3U),
            "a range selects the columns between, as shown");
    (*grid).clear_selection();
    (*grid).set_cell_selected(row_at(0U), 0U, true);
    (*grid).set_cell_selected(row_at(0U), 1U, true);
    require((*grid).clipboard_content() == std::string("true\tAlpha\r\n"), "copied fields follow the display order");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    require(nodes[0].role == SemanticRole::column_header && nodes[0].name == "On" && nodes[0].column_index == 1U &&
                nodes[2].name == "Name" && nodes[2].column_index == 3U,
            "column headers are published in display order with their display indices");

    (*grid).remove_column_at(1U);
    require(display_order(*grid) == std::vector<std::size_t>{1U, 0U, 2U},
            "removing a column closes its gap and renumbers the others");
    (*grid).add_column({.name = "extra", .header_text = "Extra"});
    require(display_order(*grid) == std::vector<std::size_t>{1U, 0U, 2U, 3U}, "an added column goes last");
    (*grid).set_columns(sample_columns());
    require(display_order(*grid) == std::vector<std::size_t>{0U, 1U, 2U, 3U}, "setting the columns resets the order");
    // remove_column_at took the first row's "On" value with its column, and
    // the values after it moved down one: the row is set again for the
    // insertion below.
    require((*grid).set_cell_value(row_at(0U), 1U, BindingValue{true}) &&
                (*grid).set_cell_value(row_at(0U), 2U, BindingValue{std::string("FM")}),
            "the first row takes its values again");

    // insert_column: values move with their columns, the display order
    // makes room at the inserted index.
    (*grid).set_column_display_index(3U, DataGridViewDisplayIndex{0U});
    require(display_order(*grid) == std::vector<std::size_t>{3U, 0U, 1U, 2U}, "the last column is shown first");
    (*grid).insert_column(1U, {.name = "frequency", .header_text = "Frequency"});
    require((*grid).columns().size() == 5U && (*grid).columns()[1].name == "frequency" &&
                (*grid).column_index("active") == std::optional<std::size_t>{2U},
            "an inserted column takes its index and the later columns move up one");
    require(display_order(*grid) == std::vector<std::size_t>{4U, 1U, 0U, 2U, 3U},
            "an inserted column takes its display index and the others are renumbered");
    require((*grid).cell_value(row_at(0U), 0U) == BindingValue{std::string("Alpha")} &&
                (*grid).cell_value(row_at(0U), 1U) == BindingValue{} &&
                (*grid).cell_value(row_at(0U), 2U) == BindingValue{true} &&
                (*grid).cell_value(row_at(0U), 3U) == BindingValue{std::string("FM")},
            "unbound values move with their columns and the new column starts empty");
    (*grid).insert_column(5U, {.name = "tail", .header_text = "Tail"});
    require((*grid).column_index("tail") == std::optional<std::size_t>{5U} &&
                (*grid).column_display_index(5U) == DataGridViewDisplayIndex{5U},
            "an insertion at the count appends");
    require(throws<std::out_of_range>(InsertColumnPastEnd{grid.get()}), "an insertion past the count is refused");
    require(throws<std::invalid_argument>(InsertDuplicateColumn{grid.get()}) && (*grid).columns().size() == 6U,
            "a repeated name is refused and changes nothing");
    (*grid).set_column_header_text(1U, "MHz");
    require((*grid).columns()[1].header_text == "MHz", "a header text is set in place");
    require(throws<std::invalid_argument>(SetInvalidHeaderText{grid.get()}) &&
                (*grid).columns()[0].header_text == "Name",
            "a header text that is not UTF-8 is refused");
    (*grid).set_columns(sample_columns());

    std::shared_ptr<DataGridView> dragged = make_grid("grid.order.drag");
    Window drag_window(dragged, {360.0, 160.0});
    drag_window.perform_layout();
    (*dragged).allow_user_to_order_columns = true;
    std::vector<std::size_t> clicks;
    SubscriptionToken click_token = (*dragged).column_header_click().subscribe(RecordIndex{&clicks});
    (*dragged).set_column_sort_mode(0U, DataGridViewColumnSortMode::programmatic);
    const Point start = centre(*dragged, (*dragged).realized_columns()[0].parts.column_label.bounds);
    require(drag_window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}),
            "a header press is taken");
    static_cast<void>(drag_window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x + 2.0, start.y}}));
    require(!(*dragged).parts().column_drop_indicator.present, "a move under 4 units is no drag");
    static_cast<void>(
        drag_window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x + 170.0, start.y}}));
    require((*dragged).parts().column_drop_indicator.present, "a drag shows where the column will go");
    static_cast<void>(drag_window.dispatch_pointer({PointerAction::up, PointerButton::primary, {start.x + 170.0, start.y}}));
    require(display_order(*dragged) == std::vector<std::size_t>{1U, 2U, 0U, 3U} && clicks.empty() &&
                !(*dragged).parts().column_drop_indicator.present,
            "the release moves the column, with no header click");
    click(drag_window, centre(*dragged, (*dragged).realized_columns()[0].parts.column_label.bounds));
    require(clicks.size() == 1U && clicks[0] == 1U && display_order(*dragged) == std::vector<std::size_t>{1U, 2U, 0U, 3U},
            "a press and release in place is still a header click");
}

// -- sorting and the clipboard (batch 3) ---------------------------------------

// Decides every comparison in reverse text order, records what it was
// asked, and tries a change the grid refuses while it sorts.
struct ReverseCompare final {
    DataGridView* grid{};
    int* calls{};
    bool* refused{};
    bool* well_formed{};
    void operator()(DataGridViewSortCompareEvent& event) const {
        ++*calls;
        const std::string first = std::get<std::string>(event.cell_value1);
        const std::string second = std::get<std::string>(event.cell_value2);
        if (event.column != 0U || event.row_id1.empty() || event.row_id2.empty() || event.row1 == event.row2) {
            *well_formed = false;
        }
        event.sort_result = second.compare(first);
        event.handled = true;
        try {
            static_cast<void>((*grid).set_cell_value(row_at(0U), 0U, BindingValue{std::string("x")}));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// Orders rows by their IDs, last first.
class IdDescending final : public DataGridViewRowComparer {
public:
    int compare(const DataGridViewRow& left, const DataGridViewRow& right) const override {
        const int result = right.stable_id.value.compare(left.stable_id.value);
        return result;
    }
};

// Says every row goes first: not an order, which the grid must survive.
class AlwaysBefore final : public DataGridViewRowComparer {
public:
    int compare(const DataGridViewRow&, const DataGridViewRow&) const override { return -1; }
};

std::vector<std::string> row_ids(const DataGridView& grid) {
    std::vector<std::string> ids{};
    for (std::size_t position = 0U; position < grid.row_count(); ++position) {
        ids.push_back(grid.row_id(row_at(position)).value);
    }
    return ids;
}

void sort_modes() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.sort.modes");
    Window window(grid, {360.0, 160.0});
    window.perform_layout();
    require((*grid).column_sort_mode(0U) == DataGridViewColumnSortMode::automatic &&
                (*grid).column_sort_mode(1U) == DataGridViewColumnSortMode::not_sortable &&
                (*grid).column_sort_mode(2U) == DataGridViewColumnSortMode::not_sortable &&
                !(*grid).sorted_column() && (*grid).sort_order() == DataGridViewSortDirection::none,
            "text columns sort automatically by default, other kinds not at all, and nothing is sorted");
    int sorted_count = 0;
    std::vector<std::size_t> header_clicks;
    SubscriptionToken sorted_token = (*grid).sorted().subscribe(Counter{&sorted_count});
    SubscriptionToken header_token = (*grid).column_header_click().subscribe(RecordIndex{&header_clicks});

    static_cast<void>((*grid).set_cell_value(row_at(0U), 0U, BindingValue{std::string("Delta")}));
    click(window, centre(*grid, (*grid).realized_columns()[0].parts.column_label.bounds));
    require(header_clicks.size() == 1U && sorted_count == 1 && (*grid).sorted_column() == 0U &&
                (*grid).sort_order() == DataGridViewSortDirection::ascending &&
                row_ids(*grid) == std::vector<std::string>{"bravo", "charlie", "alpha"} &&
                (*grid).columns()[0].sort_glyph == DataGridViewSortDirection::ascending,
            "clicking an automatic column's header sorts ascending and shows the glyph");
    window.perform_layout();
    click(window, centre(*grid, (*grid).realized_columns()[0].parts.column_label.bounds));
    require(sorted_count == 2 && (*grid).sort_order() == DataGridViewSortDirection::descending &&
                row_ids(*grid) == std::vector<std::string>{"alpha", "charlie", "bravo"},
            "a second click sorts descending");
    window.perform_layout();
    click(window, centre(*grid, (*grid).realized_columns()[1].parts.column_label.bounds));
    require(header_clicks.size() == 3U && sorted_count == 2 && (*grid).sorted_column() == 0U,
            "a not_sortable column's click raises the event and leaves the order");
    (*grid).set_column_sort_mode(0U, DataGridViewColumnSortMode::programmatic);
    require((*grid).column_sort_mode(0U) == DataGridViewColumnSortMode::programmatic,
            "the sort mode is the column's own once set");
    click(window, centre(*grid, (*grid).realized_columns()[0].parts.column_label.bounds));
    require(sorted_count == 2 && row_ids(*grid) == std::vector<std::string>{"alpha", "charlie", "bravo"},
            "a programmatic column's click leaves the order to the application");
    (*grid).set_column_sort_mode(1U, DataGridViewColumnSortMode::automatic);
    require((*grid).sort(1U, DataGridViewSortDirection::ascending) && (*grid).sorted_column() == 1U &&
                row_ids(*grid) == std::vector<std::string>{"charlie", "bravo", "alpha"},
            "sort orders by any column whatever its mode: empty, false, true");

    int calls = 0;
    bool refused = false;
    bool well_formed = true;
    {
        SubscriptionToken compare_token =
            (*grid).sort_compare().subscribe(ReverseCompare{grid.get(), &calls, &refused, &well_formed});
        require((*grid).sort(0U, DataGridViewSortDirection::ascending) && calls > 0 && well_formed &&
                    row_ids(*grid) == std::vector<std::string>{"alpha", "charlie", "bravo"},
                "sort_compare's handler decides each comparison, given the column, values, IDs and positions");
        require(refused && (*grid).cell_text(row_at(0U), 0U) == "Delta",
                "the grid refuses a handler's change while it sorts");
        require((*grid).sort(0U, DataGridViewSortDirection::descending) &&
                    row_ids(*grid) == std::vector<std::string>{"bravo", "charlie", "alpha"},
                "a descending sort reverses the handler's results");
    }
    require((*grid).sort(0U, DataGridViewSortDirection::ascending) &&
                row_ids(*grid) == std::vector<std::string>{"bravo", "charlie", "alpha"},
            "with the handler gone the grid's value order is back");

    const IdDescending by_id{};
    require((*grid).sort(by_id) && row_ids(*grid) == std::vector<std::string>{"charlie", "bravo", "alpha"} &&
                !(*grid).sorted_column() && (*grid).sort_order() == DataGridViewSortDirection::none &&
                (*grid).columns()[0].sort_glyph == DataGridViewSortDirection::none,
            "sort(comparer) orders by the comparer and leaves no sorted column or glyph");
    const AlwaysBefore inconsistent{};
    require((*grid).sort(inconsistent), "a comparer that is not an order still sorts");
    std::vector<std::string> ids = row_ids(*grid);
    std::sort(ids.begin(), ids.end());
    require(ids == std::vector<std::string>{"alpha", "bravo", "charlie"}, "and the rows are all still there, once");

    (*grid).set_column_sort_mode(0U, std::nullopt);
    require((*grid).sort(0U, DataGridViewSortDirection::ascending) && (*grid).sorted_column() == 0U,
            "sorted again");
    (*grid).clear_rows();
    require(!(*grid).sorted_column() && (*grid).sort_order() == DataGridViewSortDirection::none &&
                (*grid).columns()[0].sort_glyph == DataGridViewSortDirection::none,
            "replacing the rows ends an automatic sort and its glyph");
    require(throws<std::invalid_argument>(SetSortMode{grid.get(), static_cast<DataGridViewColumnSortMode>(7)}),
            "a sort mode outside the enumeration is refused");
}

// A host whose clipboard keeps the last text written.
class ClipboardServices final : public HostServices {
public:
    ClipboardServices() : HostServices(capabilities()) {}
    ~ClipboardServices() override { shutdown(); }
    static HostCapabilities capabilities() {
        HostCapabilities result{};
        result.platform = "clipboard-fixture";
        result.available = HostCapability::lifecycle | HostCapability::clipboard;
        return result;
    }
    std::string written{};
    int writes{};

protected:
    HostMonitorResult query_monitors_impl() override { return {{HostServiceError::unsupported}, {}}; }
    HostServiceStatus set_cursor_impl(CursorKind) override { return {}; }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view text) override {
        written = std::string(text);
        ++writes;
        return {};
    }
    HostDialogResult show_dialog_impl(const HostDialogRequest&) override { return {}; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {HostServiceError::unsupported}; }
};

void clipboard() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.clipboard");
    Window window(grid, {360.0, 160.0});
    ClipboardServices services{};
    HostSession session(window, ClipboardServices::capabilities(), &services);
    window.perform_layout();
    require((*grid).clipboard_copy_mode.read() == DataGridViewClipboardCopyMode::enable_with_auto_header_text,
            "copies take header texts automatically by default");
    (*grid).clear_selection();
    require(!(*grid).clipboard_content(), "with nothing selected there is nothing to copy");
    (*grid).set_cell_selected(row_at(0U), 0U, true);
    (*grid).set_cell_selected(row_at(1U), 2U, true);
    require((*grid).clipboard_content() == std::string("Alpha\t\r\n\tAM\r\n"),
            "one line per row and one field per column with a selected cell, empty where unselected");
    (*grid).clipboard_copy_mode = DataGridViewClipboardCopyMode::enable_always_include_header_text;
    require((*grid).clipboard_content() == std::string("\tName\tMode\r\n\tAlpha\t\r\n\t\tAM\r\n"),
            "always includes the column headers and the (empty) row headers");
    (*grid).row_headers_visible = false;
    require((*grid).clipboard_content() == std::string("Name\tMode\r\nAlpha\t\r\n\tAM\r\n"),
            "hidden row headers are left out");
    (*grid).row_headers_visible = true;
    (*grid).clipboard_copy_mode = DataGridViewClipboardCopyMode::enable_without_header_text;
    require((*grid).clipboard_content() == std::string("Alpha\t\r\n\tAM\r\n"), "without header text, none");
    (*grid).clipboard_copy_mode = DataGridViewClipboardCopyMode::disable;
    require(!(*grid).clipboard_content(), "a disabled copy has no content");

    (*grid).clipboard_copy_mode = DataGridViewClipboardCopyMode::enable_with_auto_header_text;
    require(window.request_focus(grid), "the grid takes focus");
    (*grid).selection_mode = DataGridViewSelectionMode::full_row_select;
    (*grid).clear_selection();
    (*grid).set_cell_selected(row_at(1U), 0U, true);
    require(key(window, PhysicalKey::c, Modifier::control) && services.writes == 1 &&
                services.written == "\tBravo\tfalse\tAM\tTune\r\n",
            "Ctrl+C copies the selected row, with its header text in full-row selection");
    require(key(window, PhysicalKey::insert, Modifier::control) && services.writes == 2,
            "Ctrl+Insert copies too");
    (*grid).clear_selection();
    require(!key(window, PhysicalKey::c, Modifier::control) && services.writes == 2,
            "with nothing selected Ctrl+C is not handled");
}

// A deterministic sequence for the row height tests (a 64-bit LCG).
struct HeightSequence final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t result = state >> 33U;
        return result;
    }
};

struct SetRowHeightTooSmall final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_row_height_at(row_at(0U), 3.0); }
};
struct SetRowHeightPastEnd final {
    DataGridView* grid{};
    void operator()() const { (*grid).set_row_height_at(row_at(99U), 30.0); }
};

double seconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

void row_height_index() {
    // Against plain prefix sums: random heights, then random changes.
    const std::size_t count = 1000U;
    std::vector<double> heights(count, 22.0);
    DataGridViewRowHeights index;
    index.reset(count, 22.0);
    require(index.uniform() && index.total() == 22000.0 && index.position_at(43.9) == 1U &&
                index.position_at(44.0) == 2U && index.position_at(-1.0) == 0U && index.position_at(22000.0) == count,
            "uniform rows are arithmetic");
    HeightSequence sequence;
    std::vector<DataGridViewRowHeight> given;
    for (std::size_t step = 0; step < 300U; ++step) {
        const std::size_t position = static_cast<std::size_t>(sequence.next() % count);
        const double height = 4.0 + static_cast<double>(sequence.next() % 120U) * 0.5;
        given.push_back({position, height});
        heights[position] = height;
    }
    index.reset(count, 22.0, given);
    for (std::size_t round = 0; round < 2U; ++round) {
        bool tops = true;
        bool positions = true;
        double top = 0.0;
        for (std::size_t position = 0; position < count; ++position) {
            if (index.top(position) != top || index.height(position) != heights[position]) tops = false;
            if (index.position_at(top) != position) positions = false;
            if (index.position_at(top + heights[position] * 0.5) != position) positions = false;
            top += heights[position];
        }
        require(tops && index.total() == top && !index.uniform(), "tops and heights match the prefix sums");
        require(positions && index.position_at(top) == count, "each offset finds the row that holds it");
        // Then the same through set_height, one row at a time.
        for (std::size_t step = 0; step < 200U; ++step) {
            const std::size_t position = static_cast<std::size_t>(sequence.next() % count);
            const double height = 4.0 + static_cast<double>(sequence.next() % 120U) * 0.5;
            index.set_height(position, height);
            heights[position] = height;
        }
    }
    for (std::size_t position = 0; position < count; ++position) index.set_height(position, 22.0);
    require(index.uniform() && index.total() == 22000.0, "rows back at the default release the tree");
}

void row_heights() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.heights");
    Window window(grid, {360.0, 200.0});
    window.perform_layout();
    std::vector<DataGridViewRowEvent> changed;
    SubscriptionToken changed_token = (*grid).row_height_changed().subscribe(RecordRow{&changed});
    require((*grid).row_height_at(row_at(1U)) == 22.0, "a row without its own height has the grid's");
    (*grid).set_row_height_at(row_at(1U), 50.0);
    window.perform_layout();
    require((*grid).row_height_at(row_at(1U)) == 50.0 && changed.size() == 1U && changed[0].row == row_at(1U) &&
                changed[0].row_id == id("bravo"),
            "a row takes its own height and reports it");
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows.size() == 3U && rows[1].parts.row_header.bounds.height == 50.0 &&
                rows[2].parts.row_header.bounds.y == rows[1].parts.row_header.bounds.y + 50.0,
            "the rows below a taller row start below it");
    bool cells_follow = true;
    for (const DataGridView::Cell& cell : (*grid).realized_cells()) {
        const Rect bounds = cell.parts.cell.bounds;
        const Rect header = rows[cell.address.row.value].parts.row_header.bounds;
        if (bounds.y != header.y || bounds.height != header.height) cells_follow = false;
    }
    require(cells_follow, "cells take their row's top and height");
    const Rect tall = rows[1].parts.row_header.bounds;
    click(window, {(*grid).parts().well.bounds.x + 120.0, tall.y + tall.height - 3.0});
    require((*grid).current_cell() && (*(*grid).current_cell()).row == row_at(1U),
            "a click low in a taller row hits that row");

    // The height stays with its row's ID.
    require((*grid).sort(0U, DataGridViewSortDirection::descending), "the rows sort");
    require((*grid).row_height_at(row_at(1U)) == 50.0 && (*grid).row_id(row_at(1U)) == id("bravo") &&
                (*grid).row_height_at(row_at(0U)) == 22.0,
            "sorting keeps the height on its row");
    (*grid).set_row_height_at(row_at(0U), 30.0);
    require((*grid).sort(0U, DataGridViewSortDirection::ascending) && (*grid).row_height_at(row_at(2U)) == 30.0,
            "both own heights follow their rows");
    (*grid).rows.insert(0U, {.stable_id = {"delta"}, .cells = {BindingValue{std::string("Delta")}}});
    require((*grid).row_height_at(row_at(0U)) == 22.0 && (*grid).row_height_at(row_at(2U)) == 50.0,
            "an inserted row has the default and shifts the others");
    (*grid).rows.erase(2U, 1U);
    (*grid).rows.insert(2U, {.stable_id = {"bravo"}, .cells = {BindingValue{std::string("Bravo")}}});
    require((*grid).row_height_at(row_at(2U)) == 22.0, "a removed row's height ends with it");
    changed.clear();
    (*grid).reset_row_height_at(row_at(3U));
    require((*grid).row_height_at(row_at(3U)) == 22.0 && changed.size() == 1U, "a reset row has the default again");
    (*grid).row_height = 26.0;
    require((*grid).row_height_at(row_at(0U)) == 26.0, "rows without their own follow the grid's height");
    require(throws<std::out_of_range>(SetRowHeightTooSmall{grid.get()}) &&
                throws<std::out_of_range>(SetRowHeightPastEnd{grid.get()}),
            "heights outside [4, 4096] and rows past the end are refused");

    // Scrolling by rows of mixed heights: the last row ends at the bottom.
    (*grid).set_row_count(40U);
    for (std::size_t position = 0; position < 40U; position += 3U) (*grid).set_row_height_at(row_at(position), 41.0);
    window.perform_layout();
    require(window.request_focus(grid), "the grid takes focus");
    require(key(window, PhysicalKey::end, Modifier::control), "Ctrl+End moves to the last row");
    window.perform_layout();
    const std::vector<DataGridView::Row> last = (*grid).realized_rows();
    const Rect well = (*grid).parts().well.bounds;
    const DataGridView::Parts parts = (*grid).parts();
    const Rect bottom_row = last.back().parts.row_header.bounds;
    require(last.back().position == row_at(39U) &&
                bottom_row.y + bottom_row.height <= parts.row_header_band.bounds.y + parts.row_header_band.bounds.height &&
                last.front().parts.row_header.bounds.y == parts.row_header_band.bounds.y,
            "the last row is shown whole, and the first shown starts at the top");
    static_cast<void>(well);
    (*grid).set_first_displayed_row(row_at(0U));
    window.perform_layout();
    require(key(window, PhysicalKey::up, Modifier::control) && (*grid).first_displayed_row() == row_at(0U),
            "Ctrl+Up returns to the top");
}

// Scrolls to `rounds` places in a grid of `count` rows, laying out, hit
// testing and painting at each; returns the seconds taken, and the most
// cells any one paint asked the provider for in `asked_most`.
double scroll_rounds(Window& window, DataGridView& grid, CountingProvider& provider, std::size_t count,
                     std::size_t rounds, std::size_t& asked_most) {
    HeightSequence sequence;
    const Rect well = grid.parts().well.bounds;
    const Point probe{well.x + 20.0, well.y + well.height * 0.5};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::size_t round = 0; round < rounds; ++round) {
        const std::size_t first = static_cast<std::size_t>(sequence.next() % count);
        grid.set_first_displayed_row(row_at(first));
        window.perform_layout();
        const PointerEvent hover{PointerAction::move, PointerButton::none, probe};
        static_cast<void>(window.dispatch_pointer(hover));
        provider.asked = 0U;
        CountingPainter painter;
        grid.on_paint(painter, grid.client_rectangle());
        asked_most = std::max(asked_most, provider.asked);
    }
    const double result = seconds_since(start);
    return result;
}

void row_resizing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.row.resize");
    Window window(grid, {360.0, 200.0});
    window.perform_layout();
    require((*grid).allow_user_to_resize_rows.read(), "the user resizes rows by default");
    const Rect grip = (*grid).realized_rows()[0].parts.row_grip.bounds;
    const Rect header = (*grid).realized_rows()[0].parts.row_header.bounds;
    require((*grid).realized_rows()[0].parts.row_grip.present && grip.y < header.y + header.height &&
                grip.y + grip.height > header.y + header.height,
            "a row's grip straddles its header's bottom edge");
    std::vector<DataGridViewRowEvent> changed;
    SubscriptionToken changed_token = (*grid).row_height_changed().subscribe(RecordRow{&changed});
    const Point start = centre(*grid, grip);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}), "the grip takes the press");
    require((*grid).realized_rows()[0].states.captured, "a resizing row publishes captured");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x, start.y + 20.0}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {start.x, start.y + 20.0}}));
    window.perform_layout();
    require((*grid).row_height_at(row_at(0U)) == 42.0 && !changed.empty() && changed.back().row_id == id("alpha"),
            "dragging the grip gives the row its own height");
    require(!(*grid).realized_rows()[0].states.captured && (*grid).row_height_at(row_at(1U)) == 22.0,
            "the release ends the drag, and other rows keep theirs");
    require(!(*grid).current_cell() || (*(*grid).current_cell()).row == row_at(0U),
            "a resize selects no other row");
    const Rect lower = (*grid).realized_rows()[0].parts.row_grip.bounds;
    const Point up_start = centre(*grid, lower);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, up_start}));
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {up_start.x, up_start.y - 60.0}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {up_start.x, up_start.y - 60.0}}));
    require((*grid).row_height_at(row_at(0U)) == 4.0, "a row is never dragged below 4 units");
    (*grid).allow_user_to_resize_rows = false;
    window.perform_layout();
    require(!(*grid).realized_rows()[0].parts.row_grip.present, "without row resizing there is no grip");
}

void row_auto_sizing() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.row.auto");
    Window window(grid, {360.0, 220.0});
    static_cast<void>((*grid).set_cell_value(row_at(0U), 0U, BindingValue{std::string("Kilo Lima Mike November")}));
    (*grid).set_column_default_cell_style(0U, {.wrap_mode = true});
    window.perform_layout();
    require((*grid).auto_size_rows_mode.read() == DataGridViewAutoSizeRowsMode::none &&
                (*grid).row_height_at(row_at(0U)) == 22.0,
            "rows do not size themselves by default");
    const double wrapped = (*grid).preferred_cell_height(row_at(0U), 0U);
    const double single = (*grid).preferred_cell_height(row_at(1U), 0U);
    require(wrapped > single * 1.5 && single > 0.0, "a wrapped cell asks for its lines' height");
    std::vector<DataGridViewRowEvent> changed;
    SubscriptionToken changed_token = (*grid).row_height_changed().subscribe(RecordRow{&changed});

    (*grid).auto_size_rows_mode = DataGridViewAutoSizeRowsMode::all_cells;
    window.perform_layout();
    const double header = (*grid).preferred_row_header_height(row_at(1U));
    double tallest = header;
    for (std::size_t column = 0U; column < 4U; ++column) {
        tallest = std::max(tallest, (*grid).preferred_cell_height(row_at(1U), column));
    }
    require((*grid).row_height_at(row_at(0U)) == std::max(wrapped, header) &&
                (*grid).row_height_at(row_at(1U)) == tallest && changed.empty(),
            "all_cells gives each row its tallest cell's or header's height, raising nothing");
    require(!(*grid).realized_rows()[0].parts.row_grip.present, "auto-sized rows are not resized by the user");
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows[1].parts.row_header.bounds.y == rows[0].parts.row_header.bounds.y + (*grid).row_height_at(row_at(0U)),
            "the rows below a wrapped row start below its lines");

    // Values and widths measure the rows again.
    (*grid).set_column_width(0U, 400.0);
    window.perform_layout();
    require((*grid).row_height_at(row_at(0U)) < std::max(wrapped, header) + 0.5 &&
                (*grid).row_height_at(row_at(0U)) == std::max(single, tallest),
            "a wider column takes the lines back to one");
    (*grid).set_column_width(0U, 100.0);
    static_cast<void>((*grid).set_cell_value(row_at(0U), 0U, BindingValue{std::string("Kilo")}));
    window.perform_layout();
    require((*grid).row_height_at(row_at(0U)) == tallest, "a shorter value shrinks its row");

    // Own heights wait for the mode to return to none.
    (*grid).set_row_height_at(row_at(2U), 60.0);
    window.perform_layout();
    require((*grid).row_height_at(row_at(2U)) != 60.0 && changed.empty(), "an own height waits while rows auto size");
    (*grid).auto_size_rows_mode = DataGridViewAutoSizeRowsMode::none;
    window.perform_layout();
    require((*grid).row_height_at(row_at(2U)) == 60.0 && (*grid).row_height_at(row_at(0U)) == 22.0,
            "back at none, own heights and the default return");

    // One-time sizing gives the row its own height.
    static_cast<void>((*grid).set_cell_value(row_at(1U), 0U, BindingValue{std::string("Oscar Papa Quebec Romeo")}));
    (*grid).auto_resize_row(row_at(1U));
    window.perform_layout();
    require((*grid).row_height_at(row_at(1U)) > tallest && changed.size() == 1U && changed[0].row_id == id("bravo"),
            "auto_resize_row sizes a row once and reports it");
    (*grid).auto_resize_rows(DataGridViewAutoSizeRowsMode::all_headers);
    require((*grid).row_height_at(row_at(1U)) == header, "auto_resize_rows sizes every row by its header");
}

struct GiveCellError final {
    int* calls{};
    void operator()(DataGridViewCellErrorTextNeededEvent& event) const {
        ++*calls;
        if (event.row_id.value == "v3" && event.column == 0U) event.error_text = "Out of range";
    }
};
struct GiveRowError final {
    void operator()(DataGridViewRowErrorTextNeededEvent& event) const {
        if (event.row_id.value == "v4") event.error_text = event.error_text + "Duplicate";
    }
};

void error_text_needed() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.errors.needed"));
    (*grid).set_columns(virtual_columns());
    (*grid).set_virtual_rows(std::make_shared<CountingProvider>(), virtual_order(10U));
    Window window(grid, {300.0, 200.0});
    int calls = 0;
    SubscriptionToken cell_token = (*grid).cell_error_text_needed().subscribe(GiveCellError{&calls});
    SubscriptionToken row_token = (*grid).row_error_text_needed().subscribe(GiveRowError{});
    window.perform_layout();
    require((*grid).cell_error_text(row_at(3U), 0U) == "Out of range" && (*grid).cell_error_text(row_at(2U), 0U).empty() &&
                calls >= 2,
            "virtual rows ask the application for cell error text");
    require((*grid).row_error_text(row_at(4U)) == "Duplicate" && (*grid).row_error_text(row_at(5U)).empty(),
            "and for row error text");
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* errored = find_cell(cells, 3U, 0U);
    require(errored != nullptr && (*errored).parts.cell_error.present, "an asked error shows its glyph");
    (*grid).set_cell_error_text(row_at(3U), 1U, "Held");
    require((*grid).cell_error_text(row_at(3U), 1U) == "Held", "held text reaches the handler and stays");

    std::shared_ptr<DataGridView> unbound = make_grid("grid.errors.unbound");
    int unbound_calls = 0;
    SubscriptionToken unbound_token = (*unbound).cell_error_text_needed().subscribe(GiveCellError{&unbound_calls});
    static_cast<void>((*unbound).cell_error_text(row_at(0U), 0U));
    require(unbound_calls == 0, "unbound rows hold their own error text and ask for none");
}

void focus_loss_validation() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("grid.leave.root"));
    (*root).set_requested_bounds({0.0, 0.0, 400.0, 240.0});
    std::shared_ptr<DataGridView> grid = make_grid("grid.leave");
    (*grid).set_requested_bounds({0.0, 0.0, 360.0, 160.0});
    std::shared_ptr<Button> button = make_control<Button>(StableId("grid.leave.button"), "Apply");
    (*button).set_requested_bounds({0.0, 180.0, 100.0, 30.0});
    (*root).add_child(grid);
    (*root).add_child(button);
    Window window(root, {400.0, 240.0});
    window.perform_layout();
    EventLog log;
    bool cancel_row = false;
    SubscriptionToken row_validated = (*grid).row_validated().subscribe(LogRow{&log, "row_validated"});
    SubscriptionToken row_validating = (*grid).row_validating().subscribe(LogValidating{&log, &cancel_row});
    require(window.request_focus(grid) && (*grid).current_cell() == cell_at(0U, 0U), "the grid takes focus");
    require(window.dispatch_text({"Zulu"}) && (*grid).is_current_cell_in_edit_mode(), "typing begins an edit");
    log.names.clear();
    require(window.request_focus(button), "focus moves to the button");
    require(!(*grid).is_current_cell_in_edit_mode() &&
                (*grid).cell_value(row_at(0U), 0U) == BindingValue{std::string("Zulu")} &&
                log.names == std::vector<std::string>{"row_validating", "row_validated"},
            "leaving the grid commits the edit and validates the current row");
    log.names.clear();
    require(window.request_focus(grid) && log.names.empty(), "returning validates nothing");
    require(key(window, PhysicalKey::f2) && (*grid).is_current_cell_in_edit_mode(), "an edit starts");
    require(log.names.empty(), "moving focus into the editor is not leaving the grid");
    cancel_row = true;
    require(window.request_focus(button) && log.names == std::vector<std::string>{"row_validating"},
            "a cancelled validation raises no row_validated");
    require((*grid).current_cell() == cell_at(0U, 0U), "the current cell stays");
}

// Rows by ID from a map; adds rows the user enters, as "added-1" and on,
// unless refusing.
class AddingProvider final : public DataGridViewCellValueProvider {
public:
    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        const std::string key = row.value + "/" + std::to_string(column);
        const std::unordered_map<std::string, BindingValue>::const_iterator found = values.find(key);
        if (found != values.end()) return (*found).second;
        return BindingValue{"label " + row.value};
    }
    bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) override {
        values.insert_or_assign(row.value + "/" + std::to_string(column), value);
        return true;
    }
    [[nodiscard]] bool can_add_rows() const override { return true; }
    [[nodiscard]] std::optional<DataGridViewRowId> add_row(std::span<const BindingValue> given) override {
        ++asked;
        given_count = given.size();
        if (refuse) return std::nullopt;
        ++added;
        DataGridViewRowId row{"added-" + std::to_string(added)};
        return row;
    }

    std::unordered_map<std::string, BindingValue> values{};
    std::size_t asked{};
    std::size_t added{};
    std::size_t given_count{};
    bool refuse{};
};

void virtual_new_row() {
    std::shared_ptr<DataGridView> plain = make_control<DataGridView>(StableId("grid.virtual.no.new"));
    (*plain).set_columns(virtual_columns());
    (*plain).set_virtual_rows(std::make_shared<CountingProvider>(), virtual_order(3U));
    require((*plain).row_count() == 3U, "a provider that adds no rows shows no new row");

    std::shared_ptr<AddingProvider> provider = std::make_shared<AddingProvider>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.virtual.new"));
    (*grid).set_columns(virtual_columns());
    (*grid).set_virtual_rows(provider, virtual_order(3U));
    Window window(grid, {300.0, 200.0});
    window.perform_layout();
    require((*grid).row_count() == 4U, "a provider that adds rows shows the new row last");
    require(window.request_focus(grid) && (*grid).set_current_cell(cell_at(3U, 0U)), "the new row becomes current");
    (*provider).refuse = true;
    require(window.dispatch_text({"Zulu"}) && key(window, PhysicalKey::enter), "an entry commits");
    require((*provider).asked == 1U && (*grid).row_count() == 4U, "a refused row is not added");
    (*provider).refuse = false;
    (*grid).cancel_edit();
    require((*grid).set_current_cell(cell_at(3U, 0U)) && window.dispatch_text({"Yankee"}) &&
                key(window, PhysicalKey::enter),
            "a second entry commits");
    window.perform_layout();
    require((*provider).added == 1U && (*provider).given_count == 2U && (*grid).row_count() == 5U &&
                (*grid).row_id(row_at(3U)) == id("added-1"),
            "the application adds the row and names it; a new row follows");
    require((*grid).cell_text(row_at(3U), 0U) == "Yankee", "the committed value reaches the added row");
    require((*grid).current_row_id() == id("added-1") || (*grid).current_row_id() != id("v0"),
            "the current cell follows the added row's ID");
}

void error_glyph_tool_tips() {
    std::shared_ptr<DataGridView> grid = make_grid("grid.error.tips");
    Window window(grid, {360.0, 200.0});
    (*grid).set_cell_error_text(row_at(0U), 0U, "Name is required");
    (*grid).set_row_error_text(row_at(1U), "Duplicate row");
    window.perform_layout();
    require((*grid).publishes_item_tool_tips(), "the grid publishes its error glyphs' tooltips");
    const std::vector<SemanticNode> nodes = (*grid).semantic_virtual_children();
    const SemanticNode* cell_glyph = nullptr;
    const SemanticNode* row_glyph = nullptr;
    const SemanticNode* plain_cell = nullptr;
    std::size_t tipped = 0U;
    for (const SemanticNode& row : nodes) {
        for (const SemanticNode& child : row.children) {
            if (!child.tool_tip.empty()) ++tipped;
            if (child.role == SemanticRole::cell && child.children.empty()) plain_cell = &child;
            for (const SemanticNode& glyph : child.children) {
                if (glyph.tool_tip == "Name is required") cell_glyph = &glyph;
                if (glyph.tool_tip == "Duplicate row") row_glyph = &glyph;
            }
        }
    }
    require(cell_glyph != nullptr && (*cell_glyph).role == SemanticRole::image &&
                (*cell_glyph).name == "Name is required" && (*cell_glyph).bounds.width > 0.0,
            "a cell's error glyph is an image tooltipped by its error text");
    require(row_glyph != nullptr, "a row header's error glyph is tooltipped too");
    require(tipped == 0U, "cells and headers carry no tooltip");

    // Over a glyph its error text shows; over a cell without one, the grid's
    // own tooltip, as before.
    const Rect glyph = (*cell_glyph).bounds;
    (*grid).tool_tip_text = "Orders";
    ToolTipSettings settings = window.tool_tip_settings();
    settings.initial_delay = std::chrono::milliseconds{1};
    settings.reshow_delay = std::chrono::milliseconds{1};
    window.set_tool_tip_settings(settings);
    window.perform_layout();
    const Point over_glyph{glyph.x + glyph.width / 2.0, glyph.y + glyph.height / 2.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, over_glyph}));
    std::optional<FrameTime> wake = window.next_wake();
    require(wake.has_value(), "resting over the glyph schedules its tooltip");
    static_cast<void>(window.poll_frame_schedule(*wake));
    require(window.tool_tip_visible() && window.tool_tip_text() == "Name is required" &&
                window.tool_tip_item_id() == (*cell_glyph).stable_id,
            "the glyph shows its error text");
    require(plain_cell != nullptr, "a cell without an error is published");
    const Rect plain = (*plain_cell).bounds;
    const Point over_cell{plain.x + plain.width / 2.0, plain.y + plain.height / 2.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, over_cell}));
    wake = window.next_wake();
    if (wake) static_cast<void>(window.poll_frame_schedule(*wake));
    require(window.tool_tip_visible() && window.tool_tip_text() == "Orders" && window.tool_tip_item_id().empty() &&
                window.tool_tip_target() == grid,
            "a cell without an error shows the grid's own tooltip");
}

void million_row_heights() {
    std::shared_ptr<CountingProvider> provider = std::make_shared<CountingProvider>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.million.heights"));
    (*grid).set_columns(virtual_columns());
    const std::size_t count = 1000000U;
    (*grid).set_virtual_rows(provider, virtual_order(count));
    Window window(grid, {300.0, 400.0});
    window.perform_layout();
    // The same work over uniform rows first, for comparison.
    const std::size_t rounds = 2000U;
    std::size_t asked_most = 0U;
    const double uniform_seconds = scroll_rounds(window, *grid, *provider, count, rounds, asked_most);
    // A thousand rows taller than the rest, set one by one in place.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::size_t position = 500U; position < count; position += 1000U) {
        (*grid).set_row_height_at(row_at(position), 66.0);
    }
    const double set_seconds = seconds_since(start);
    require((*grid).row_height_at(row_at(500U)) == 66.0 && (*grid).row_height_at(row_at(501U)) == 22.0,
            "virtual rows take their own heights");

    // Scroll to a row: the rows shown follow, by their offsets.
    (*grid).set_first_displayed_row(row_at(700490U));
    window.perform_layout();
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    bool stacked = !rows.empty() && rows.front().stable_id == "v700490";
    bool tall_shown = false;
    for (std::size_t index = 1; index < rows.size(); ++index) {
        const Rect above = rows[index - 1U].parts.row_header.bounds;
        if (rows[index].parts.row_header.bounds.y != above.y + above.height) stacked = false;
        const bool tall = rows[index].position.value % 1000U == 500U;
        const bool cut = index + 1U == rows.size();
        if (tall && !cut) tall_shown = true;
        if (!cut && rows[index].parts.row_header.bounds.height != (tall ? 66.0 : 22.0)) stacked = false;
    }
    require(stacked && tall_shown, "a million rows of mixed heights stack where they are scrolled to");

    // Scroll, hit test and paint at many places: each costs the index's
    // logarithmic descent, and the provider is asked only for rows shown.
    const double scroll_seconds = scroll_rounds(window, *grid, *provider, count, rounds, asked_most);
    require(asked_most <= 40U * 2U * 3U, "each paint asks only for the rows shown");

    // A change to the rows rebuilds the index once, in one pass.
    start = std::chrono::steady_clock::now();
    (*grid).row_height = 24.0;
    window.perform_layout();
    const double rebuild_seconds = seconds_since(start);
    require((*grid).row_height_at(row_at(1500U)) == 66.0 && (*grid).row_height_at(row_at(1501U)) == 24.0,
            "the rebuilt index keeps the own heights");
    require(window.request_focus(grid) && key(window, PhysicalKey::end, Modifier::control),
            "Ctrl+End moves to the last of a million rows");
    window.perform_layout();
    require((*grid).displayed_row_end() == row_at(count), "the last of a million rows scrolls into view");
    // The displayed modes measure only the rows shown, as they are placed.
    (*grid).auto_size_rows_mode = DataGridViewAutoSizeRowsMode::displayed_cells;
    window.perform_layout();
    std::size_t displayed_asked = 0U;
    const double displayed_seconds = scroll_rounds(window, *grid, *provider, count, rounds, displayed_asked);
    require(displayed_asked <= 40U * 2U * 3U, "displayed_cells asks only for the rows shown");
    const double per_round = 1000000.0 / static_cast<double>(rounds);
    std::cout << "million-row displayed_cells: scroll+measure+layout+hit+paint " << displayed_seconds * per_round
              << " us each" << std::endl;
    std::cout << "million-row heights: scroll+layout+hit+paint " << uniform_seconds * per_round
              << " us uniform, " << scroll_seconds * per_round << " us with 1000 own heights; 1000 set_row_height_at "
              << set_seconds * 1000.0 << " ms; default-height change + rebuild + layout " << rebuild_seconds * 1000.0
              << " ms" << std::endl;
}

int main() {
    columns_and_rows();
    layout_and_parts();
    selection_and_navigation();
    text_editing();
    check_button_and_combo_cells();
    virtual_rows();
    virtual_row_identity();
    unbound_row_identity();
    sorting_headers_and_resizing();
    bound_rows();
    scrolling();
    semantics();
    image_cells();
    link_cells();
    data_errors_and_dirty_state();
    enter_leave_and_validation();
    edit_modes();
    new_row_unbound();
    new_row_bound();
    deleting_rows();
    error_text();
    cell_styles();
    formatting_and_parsing();
    borders_and_owner_drawing();
    sort_modes();
    clipboard();
    authored_colours();
    line_wrapping();
    wrapped_cells();
    column_auto_sizing();
    column_order();
    column_visibility();
    row_height_index();
    row_heights();
    row_resizing();
    row_auto_sizing();
    error_text_needed();
    focus_loss_validation();
    virtual_new_row();
    error_glyph_tool_tips();
    million_row_heights();
    std::cout << "DataGridView: columns, rows, parts, selection, keys, editing, cell kinds, virtual rows, "
                 "row identity, sorting, resizing, binding, scrolling, semantics, image and link cells, "
                 "styles, formatting, borders, owner drawing, sort modes, the clipboard, authored colours, wrapped text, column auto sizing, column order and row heights passed\n";
    return 0;
}
