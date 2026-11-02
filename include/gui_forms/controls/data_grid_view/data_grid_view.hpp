#pragma once

#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view_row_heights.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view_row_order.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/theme/owner_draw/owner_draw.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gui_forms {

struct FocusedControlChange;

class BindingSource;

// The kind of cell a column shows (Windows Forms' DataGridViewTextBoxColumn,
// DataGridViewCheckBoxColumn, DataGridViewComboBoxColumn,
// DataGridViewButtonColumn, DataGridViewImageColumn and
// DataGridViewLinkColumn).
enum class DataGridViewColumnKind : std::uint8_t {
    text,
    check_box,
    combo_box,
    button,
    image,
    link,
};

// How an image cell places its image (Windows Forms'
// DataGridViewImageCellLayout): at its own size, centred and cut at the
// cell's edges; stretched to the cell; or as large as fits with its shape
// kept, centred.
enum class DataGridViewImageLayout : std::uint8_t { normal, stretch, zoom };

// Where a cell's content sits in the cell (Windows Forms'
// DataGridViewContentAlignment): at the top, middle or bottom, and at the
// start, centre or end across, mirrored with the layout direction. Windows
// Forms' NotSet is an empty optional in DataGridViewCellStyle.
enum class DataGridViewContentAlignment : std::uint8_t {
    top_start,
    top_center,
    top_end,
    middle_start,
    middle_center,
    middle_end,
    bottom_start,
    bottom_center,
    bottom_end,
};

// Which lines the cells draw between them (Windows Forms'
// DataGridViewCellBorderStyle, without Custom). Raised and sunken lines are
// the same divider parts as single ones and draw as the theme's divider (T4
// style mapping): a recorded difference from Windows Forms, whose raised and
// sunken lines are bevels.
enum class DataGridViewCellBorderStyle : std::uint8_t {
    single,
    raised,
    sunken,
    none,
    single_vertical,
    raised_vertical,
    sunken_vertical,
    single_horizontal,
    raised_horizontal,
    sunken_horizontal,
};

// Whether a header band draws the lines between its headers (Windows Forms'
// DataGridViewHeaderBorderStyle, without Custom); raised and sunken draw as
// single, as for cells.
enum class DataGridViewHeaderBorderStyle : std::uint8_t { single, raised, sunken, none };

// A cell style (Windows Forms' DataGridViewCellStyle). Every property is
// optional: an empty one is "not set" and is taken from the next style in
// the cell's inheritance chain (see DataGridView::inherited_cell_style);
// one no style sets is the theme's (T4 style mapping). Styles are values:
// a grid stores a copy, and changing a style the program holds changes no
// cell until it is set again.
//
// `font` is the content's font. `alignment` places the content: across
// through the text's alignment, up and down within the cell. `padding`, in
// logical units at text scale 1, is kept clear around the content in place
// of the grid's own margins (4 units at the start and end). `wrap_mode` true
// wraps a text cell's text at the content's width, breaking at the text
// engine's line-break opportunities and at line ends; the lines are placed
// up and down as a block, cut at the content's bottom (rows do not grow to
// fit them). Other kinds and the headers keep one line. `format` formats a
// number value under a standard numeric format ("F2", "N0", "P1", "C",
// "E3", "G"; gui_forms/binding/number_format), the formats Binding accepts,
// and text typed in such a cell converts as the number it shows
// ("1,234.50"). `null_value` is shown for an empty
// value, and typed text equal to it stores `data_source_null_value` (an
// empty value when that is not set). The colours are authored colours
// (owner decision T4-1): over the theme's ordinary colours, under its
// important ones, and not in high contrast. `back_color` fills the cell,
// `fore_color` draws the text of a text, combo box or button cell (a link
// keeps its link colours, a check box its theme's), and the selection
// colours stand in for both while the cell is selected; an unset selection
// colour leaves the theme's selection look. A disabled grid stays greyed.
struct DataGridViewCellStyle final {
    std::optional<FontSpec> font{};
    std::optional<DataGridViewContentAlignment> alignment{};
    std::optional<Insets> padding{};
    std::optional<bool> wrap_mode{};
    std::optional<std::string> format{};
    std::optional<BindingValue> null_value{};
    std::optional<BindingValue> data_source_null_value{};
    std::optional<Color> back_color{};
    std::optional<Color> fore_color{};
    std::optional<Color> selection_back_color{};
    std::optional<Color> selection_fore_color{};

    friend bool operator==(const DataGridViewCellStyle& left, const DataGridViewCellStyle& right) noexcept {
        return left.font == right.font && left.alignment == right.alignment && left.padding == right.padding &&
               left.wrap_mode == right.wrap_mode && left.format == right.format &&
               left.null_value == right.null_value && left.data_source_null_value == right.data_source_null_value &&
               left.back_color == right.back_color && left.fore_color == right.fore_color &&
               left.selection_back_color == right.selection_back_color &&
               left.selection_fore_color == right.selection_fore_color;
    }
};

// The sort glyph a column header shows. The grid draws the glyph; ordering
// rows is the application's work, or sort() for unbound rows.
enum class DataGridViewSortDirection : std::uint8_t { none, ascending, descending };

// What a click on a column's header does to the order (Windows Forms'
// DataGridViewColumnSortMode). automatic sorts unbound rows by the column,
// ascending first and then toggling, and shows the glyph; programmatic
// leaves the order and the glyph to the application (sort,
// set_column_sort_glyph); not_sortable does neither. The click raises
// column_header_click in every mode, before an automatic sort. A column
// whose sort_mode is empty takes its kind's Windows Forms default:
// automatic for a text column, not_sortable for the others.
enum class DataGridViewColumnSortMode : std::uint8_t { not_sortable, automatic, programmatic };

// How a column's width follows its content (Windows Forms'
// DataGridViewAutoSizeColumnMode). not_set takes the grid's
// auto_size_columns_mode; none keeps the width the program or the user set.
// The cell modes size the column to the widest of its cells' preferred
// widths (all rows, or the rows shown), with or without its header's;
// column_header to its header's; fill shares the width the other columns
// leave by fill_weight. An auto-sized column is never narrower than its
// minimum_width, and the user cannot resize it.
enum class DataGridViewAutoSizeColumnMode : std::uint8_t {
    not_set,
    none,
    all_cells_except_header,
    all_cells,
    displayed_cells_except_header,
    displayed_cells,
    column_header,
    fill,
};

// The grid's default for columns whose mode is not_set (Windows Forms'
// DataGridViewAutoSizeColumnsMode): the same choices less not_set.
enum class DataGridViewAutoSizeColumnsMode : std::uint8_t {
    none,
    all_cells_except_header,
    all_cells,
    displayed_cells_except_header,
    displayed_cells,
    column_header,
    fill,
};

// How the grid sizes its rows' heights (Windows Forms'
// DataGridViewAutoSizeRowsMode): not at all, or to the preferred heights of
// the row headers, the cells, or both, over every row or the rows shown.
enum class DataGridViewAutoSizeRowsMode : std::uint8_t {
    none,
    all_headers,
    all_cells_except_headers,
    all_cells,
    displayed_headers,
    displayed_cells_except_headers,
    displayed_cells,
};

// What auto_resize_row measures (Windows Forms' DataGridViewAutoSizeRowMode).
enum class DataGridViewAutoSizeRowMode : std::uint8_t {
    all_cells_except_header,
    all_cells,
    row_header,
};

// Which header texts a copy includes (Windows Forms'
// DataGridViewClipboardCopyMode). With auto header text, the row headers'
// texts come along in full-row selection; always includes the visible
// column and row headers; disable copies nothing.
enum class DataGridViewClipboardCopyMode : std::uint8_t {
    disable,
    enable_with_auto_header_text,
    enable_without_header_text,
    enable_always_include_header_text,
};

// Whether a click or arrow key selects one cell or the whole row.
enum class DataGridViewSelectionMode : std::uint8_t { cell_select, full_row_select };

// Where the rows come from. Unbound rows are records the grid owns; virtual
// rows are counted by the grid and their values asked of a provider as they
// become visible; bound rows are a BindingSource's records.
enum class DataGridViewRowSource : std::uint8_t { unbound, virtual_rows, binding_source };

// Which of the user's keys and clicks begin an edit of the current cell
// (Windows Forms' DataGridViewEditMode):
//
//   mode                     typing  F2   on becoming current
//   edit_on_enter            yes     yes  yes (a text cell, while focused)
//   edit_on_keystroke        yes     no   no
//   edit_on_keystroke_or_f2  yes     yes  no
//   edit_on_f2               no      yes  no
//   edit_programmatically    no      no   no
//
// A double click, a combo box cell's drop-down button, F4 and Alt+Down begin
// an edit in every mode but edit_programmatically; begin_edit always does.
enum class DataGridViewEditMode : std::uint8_t {
    edit_on_enter,
    edit_on_keystroke,
    edit_on_keystroke_or_f2,
    edit_on_f2,
    edit_programmatically,
};

// Why a value did not reach its cell (the two of Windows Forms'
// DataGridViewDataErrorContexts the grid raises): the typed text does not
// convert to the column's value kind, or the row's store (a provider or a
// binding source) refused the value.
enum class DataGridViewDataErrorContext : std::uint8_t { parsing, commit };

// One column. `name` is the column's identity: unique within the grid, not
// empty, and without '.', since a cell's stable ID is its row's ID, '.' and
// the column's name. `width` and `minimum_width` are logical units at text
// scale 1. `default_cell_style` is the column's cells' style (Windows Forms'
// DefaultCellStyle) and `header_cell_style` its header's (HeaderCell.Style).
//
// `value_kind` is the kind a committed text edit converts to (text by
// default; a number column takes signed_integer or number). `items` are a
// combo box column's choices and must be empty for other kinds.
// `button_text` is shown on a button cell whose value is empty. An image
// column's cells hold ImageId values, images of the grid's window, placed by
// `image_layout`; `image_description` is the text an image cell gives in
// place of its image (Windows Forms' Description), as its cell text and its
// semantic name.
// A link column's cells show their text as a link; `link_text` is shown on
// a link cell whose value is empty (Windows Forms' Text with
// UseColumnTextForLinkValue). With `track_visited_state` a link the user
// clicks becomes visited (Windows Forms' TrackVisitedState).
// `data_property_name` names the BindingSource field a bound column shows.
struct DataGridViewColumn final {
    std::string name{};
    GUI_FORMS_LOCALIZABLE(header) std::string header_text{};
    DataGridViewColumnKind kind{DataGridViewColumnKind::text};
    double width{100.0};
    double minimum_width{20.0};
    bool read_only{};
    DataGridViewCellStyle default_cell_style{};
    BindingValueKind value_kind{BindingValueKind::text};
    std::vector<std::string> items{};
    std::string button_text{};
    DataGridViewImageLayout image_layout{DataGridViewImageLayout::normal};
    std::string image_description{};
    std::string link_text{};
    bool track_visited_state{true};
    std::string data_property_name{};
    DataGridViewSortDirection sort_glyph{DataGridViewSortDirection::none};
    DataGridViewCellStyle header_cell_style{};
    std::optional<DataGridViewColumnSortMode> sort_mode{};
    DataGridViewAutoSizeColumnMode auto_size_mode{DataGridViewAutoSizeColumnMode::not_set};
    // A fill column's share of the width the other columns leave, against
    // the other fill columns' weights; finite and positive.
    double fill_weight{100.0};
};

// One unbound row. `stable_id` is unique within the grid; an empty ID is
// replaced by a generated one ("row.1", "row.2", ...). `cells` holds one value
// per column in column order; a shorter list is padded with empty values.
struct DataGridViewRow final {
    DataGridViewRowId stable_id{};
    std::vector<BindingValue> cells{};
    std::string header_text{};
    bool read_only{};
};

// A column's place in the order the grid shows its columns (Windows Forms'
// DisplayIndex), apart from its index in columns(), which never changes
// with the order. 0 is the first column shown.
struct DataGridViewDisplayIndex final {
    std::size_t value{};

    friend bool operator==(const DataGridViewDisplayIndex& left, const DataGridViewDisplayIndex& right) noexcept {
        return left.value == right.value;
    }
};

// A cell by row position and column index, as the rows are presented now.
// Inserting, removing or sorting rows moves the row a position names.
struct DataGridViewCellAddress final {
    DataGridViewRowPosition row{};
    std::size_t column{};

    friend constexpr bool operator==(const DataGridViewCellAddress& left,
                                     const DataGridViewCellAddress& right) noexcept {
        return left.row == right.row && left.column == right.column;
    }
};

// The cell an event concerns: its row's identity, the row's position when
// the event is raised (the position it had, for a row just removed), and the
// column.
struct DataGridViewCellEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
};

// Raised before an edit begins; setting `cancel` keeps the cell out of edit.
struct DataGridViewCellCancelEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    bool cancel{};
};

// Raised before an edited text cell commits, with the text as typed. Setting
// `cancel` refuses the text: the cell stays in edit and is marked invalid.
struct DataGridViewCellValidatingEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    std::string formatted_value{};
    bool cancel{};
};

// Raised when a value does not reach its cell (Windows Forms' DataError).
// `formatted_value` is the text the user gave (a typed text, a choice, or
// "true"/"false" for a check box). For a text edit, `cancel` true (the
// default, and the outcome when no handler is subscribed: owner decision
// B1-1) keeps the cell in edit, marked invalid, with its old value stored;
// false discards the edit, leaving the old value. A check box or choice has
// no edit to keep: its cell keeps its old value either way.
struct DataGridViewDataErrorEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    DataGridViewDataErrorContext context{DataGridViewDataErrorContext::parsing};
    std::string formatted_value{};
    bool cancel{true};
};

// Raised when the grid needs a cell's text (Windows Forms' CellFormatting):
// to paint and lay it out, for its semantic value, and as the text an edit
// begins with. `value` is the cell's value and `cell_style` its inherited
// style. A handler that sets `formatted_value` and `formatting_applied`
// gives the text; otherwise the grid formats `value`, which the handler may
// change, with `cell_style`'s format and null value. The style the handler
// leaves (font, alignment, padding) places and draws the content that
// time. Handlers must not change the grid; a paint stops at a change.
struct DataGridViewCellFormattingEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    BindingValue value{};
    DataGridViewCellStyle cell_style{};
    std::string formatted_value{};
    bool formatting_applied{};
};

// Raised when an edited text cell stores its text (Windows Forms'
// CellParsing), after cell_validating and before the text converts to the
// column's value kind. `formatted_value` is the typed text and `cell_style`
// the cell's inherited style. A handler that sets `value` and
// `parsing_applied` gives the value stored; otherwise text equal to the
// style's null value stores its data source null value, and other text
// converts as without the event.
struct DataGridViewCellParsingEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    std::string formatted_value{};
    DataGridViewCellStyle cell_style{};
    BindingValue value{};
    bool parsing_applied{};
};

// Raised for each comparison while sort(column, direction) orders unbound
// rows, when it has a handler (Windows Forms' SortCompare). `cell_value1`
// and `cell_value2` are the two rows' values in `column`, `row_id1` and
// `row_id2` their IDs and `row1` and `row2` their positions before the
// sort; all are borrowed for the call. A handler that sets `sort_result`
// (negative, zero or positive: the first row before, with or after the
// second, in ascending order) and `handled` decides; otherwise the grid's
// value order does. A descending sort reverses the result. Handlers may
// not change the grid's rows, columns or values (std::logic_error).
struct DataGridViewSortCompareEvent final {
    std::size_t column{};
    const BindingValue& cell_value1;
    const BindingValue& cell_value2;
    std::string_view row_id1{};
    std::string_view row_id2{};
    DataGridViewRowPosition row1{};
    DataGridViewRowPosition row2{};
    int sort_result{};
    bool handled{};
};

// Orders unbound rows for DataGridView::sort(comparer) (Windows Forms'
// Sort(IComparer)). compare returns a negative number, zero or a positive
// number when `left` goes before, with or after `right`. It need not be a
// consistent order: the grid's merge sort stays within the rows whatever it
// returns. It may not change the grid (std::logic_error).
class DataGridViewRowComparer {
public:
    virtual ~DataGridViewRowComparer();
    [[nodiscard]] virtual int compare(const DataGridViewRow& left, const DataGridViewRow& right) const = 0;
};

// A row an event concerns: its identity and its position when the event is
// raised (the position it had, for a row just removed).
struct DataGridViewRowEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
};

// Raised when the grid reads a cell's error text in virtual or bound rows
// (Windows Forms' CellErrorTextNeeded): `error_text` starts as the text the
// grid holds (set_cell_error_text, or the record's error) and a handler may
// replace it with valid UTF-8 (other text is ignored). Raised from const
// reads (layout, paint, semantics); handlers may not change the grid.
struct DataGridViewCellErrorTextNeededEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::size_t column{};
    std::string error_text{};
};

// As DataGridViewCellErrorTextNeededEvent, for a row's own error text
// (Windows Forms' RowErrorTextNeeded).
struct DataGridViewRowErrorTextNeededEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::string error_text{};
};

// Raised before a row is left or deleted; setting `cancel` keeps it.
struct DataGridViewRowCancelEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    bool cancel{};
};

// Raised when the new row becomes current (Windows Forms'
// DefaultValuesNeeded): `values` holds one value per column, empty, for the
// handler to fill with the values the new row starts with.
struct DataGridViewRowValuesEvent final {
    DataGridViewRowId row_id{};
    DataGridViewRowPosition row{};
    std::vector<BindingValue> values{};
};

// The application side of virtual mode (Windows Forms' CellValueNeeded and
// CellValuePushed), on ObjectView's stable-ID model: the application owns the
// rows and their order and gives the order to the grid as row IDs (in full
// with set_virtual_rows and set_virtual_row_order, by change with
// insert_virtual_rows, remove_virtual_rows and move_virtual_row, or on demand
// through a DataGridViewRowWindowSource); the grid asks for values by ID.
// It asks for the cells it shows, the current cell, and cells semantics or the
// application read through the grid; it never asks for any other row. Calls
// arrive on the grid's UI thread, from paint, layout, input and the grid's own
// methods. The provider may not mutate the grid from inside a call.
class DataGridViewCellValueProvider {
public:
    virtual ~DataGridViewCellValueProvider();
    // The value of one cell; `row` is in the grid's current order.
    [[nodiscard]] virtual BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const = 0;
    // Stores a committed value. Returning false refuses it: the cell keeps its
    // old value and no cell_value_changed is raised.
    virtual bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) = 0;
    // Whether the grid shows a new row for the user to add rows through
    // add_row (with allow_user_to_add_rows). The default is false.
    [[nodiscard]] virtual bool can_add_rows() const;
    // Adds the row the user entered in the new row (owner decision,
    // 2026-11-02), when its first value commits: `values` holds one value per
    // column, in column order, empty for cells the user left. Returns the new
    // row's ID (not empty, valid UTF-8, not a present row's), or none to
    // refuse, which leaves the new row as it was. The default refuses. In
    // edit mode the grid then inserts the ID after the last row; in window
    // mode the application reports the insertion (insert_virtual_rows) as
    // soon as add_row returns. The committed value then arrives through
    // push_cell_value. The provider may not change the grid from inside it.
    // The C# facade can raise Windows Forms' NewRowNeeded and
    // CellValuePushed on top of it, for compatibility.
    [[nodiscard]] virtual std::optional<DataGridViewRowId> add_row(std::span<const BindingValue> values);
};

// A grid of rows and typed columns (Windows Forms' DataGridView): text,
// check box, combo box, button and image columns over unbound rows the grid owns,
// virtual rows whose values an application provider supplies on demand, or a
// BindingSource's records. Every row has a stable ID (DataGridViewRowId), and
// the grid keeps its selection, current cell and edit by ID; row numbers in
// the API are presentation positions (DataGridViewRowPosition). The column
// header row and the row header column stay in place while the cells scroll
// beneath them. Cells are records, not controls: only the visible rows and
// columns are realized, as the "row", "cell" and "column" item systems, and
// every part is painted through the by-role paint path. A text cell edits in
// a hosted TextBox placed over it; a combo box cell chooses from a
// ContextMenu of its column's items.
class DataGridView final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit DataGridView(StableId stable_id);
    ~DataGridView() override;
    // Creates the editor and the scroll bars. make_control calls it.
    void initialize_control_tree();

    // -- columns ---------------------------------------------------------
    [[nodiscard]] std::span<const DataGridViewColumn> columns() const noexcept { return columns_; }
    // Replaces the columns. Unbound rows keep their values by position:
    // values past the new column count are dropped and new columns start
    // empty. Ends any edit and clears the selection and current cell. Throws
    // std::invalid_argument for an empty, duplicate or dotted name, invalid
    // UTF-8, a width that is not finite or below the minimum, items on a
    // column that is not a combo box, or an image layout outside the
    // enumeration.
    void set_columns(std::vector<DataGridViewColumn> columns);
    void add_column(DataGridViewColumn column);
    // Throws std::out_of_range for an index past the end.
    void remove_column_at(std::size_t column);
    [[nodiscard]] std::optional<std::size_t> column_index(std::string_view name) const noexcept;
    // Throws std::out_of_range for an unknown column and std::invalid_argument
    // for a width that is not finite or below the column's minimum. An
    // auto-sized column takes its automatic width again at the next layout.
    void set_column_width(std::size_t column, double width);

    // -- automatic column sizing -----------------------------------------
    // The grid's default auto size mode (none by default). Automatic widths
    // are applied when the grid lays out: after a change to the values,
    // rows, columns, styles or text scale, and for the displayed modes after
    // a scroll. Applying them does not raise column_width_changed. The cell
    // modes measure every row's cell (each a formatted cell: virtual rows
    // ask their provider for every row), so they cost a pass over the rows.
    [[nodiscard]] DataGridViewAutoSizeColumnsMode auto_size_columns_mode() const noexcept {
        return auto_size_columns_mode_;
    }
    void set_auto_size_columns_mode(DataGridViewAutoSizeColumnsMode mode);
    // A column's mode, the grid's when its own is not_set (Windows Forms'
    // InheritedAutoSizeMode); never not_set.
    [[nodiscard]] DataGridViewAutoSizeColumnMode column_auto_size_mode(std::size_t column) const;
    void set_column_auto_size_mode(std::size_t column, DataGridViewAutoSizeColumnMode mode);
    // Throws std::invalid_argument for a weight that is not finite and
    // positive.
    void set_column_fill_weight(std::size_t column, double weight);
    // Sizes a column once, as `mode` would (a cell mode or column_header;
    // Windows Forms' AutoResizeColumn), raising column_width_changed when the
    // width changes. Throws std::invalid_argument for not_set, none or fill.
    void auto_resize_column(std::size_t column,
                            DataGridViewAutoSizeColumnMode mode = DataGridViewAutoSizeColumnMode::all_cells);
    // Sizes every column once (AutoResizeColumns). Throws
    // std::invalid_argument for none or fill.
    void auto_resize_columns(DataGridViewAutoSizeColumnsMode mode = DataGridViewAutoSizeColumnsMode::all_cells);
    // The width, in logical units at text scale 1, a cell or a column header
    // asks for: its content, its padding and its grid line (Windows Forms'
    // GetPreferredWidth).
    [[nodiscard]] double preferred_cell_width(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] double preferred_column_header_width(std::size_t column) const;
    void set_column_sort_glyph(std::size_t column, DataGridViewSortDirection direction);
    void set_column_read_only(std::size_t column, bool read_only);

    // -- rows ------------------------------------------------------------
    [[nodiscard]] DataGridViewRowSource row_source() const noexcept { return row_source_; }
    // Every row shown, the new row included (Windows Forms' RowCount).
    [[nodiscard]] std::size_t row_count() const noexcept;
    // Unbound rows grow with generated rows or are cut from the end; cutting
    // the current row makes the new last row current. `count` is the number
    // of rows before the new row, which stays last. Throws
    // std::logic_error for virtual rows (counted by their order) and bound
    // rows (counted by their source).
    void set_row_count(std::size_t count);
    // Unbound rows only (std::logic_error otherwise). Throws
    // std::invalid_argument for a duplicate stable ID, invalid UTF-8 or more
    // values than columns, and std::out_of_range for an insertion position
    // past the end. add_row returns the new row's position.
    DataGridViewRowPosition add_row(DataGridViewRow row);
    void insert_row(DataGridViewRowPosition position, DataGridViewRow row);
    void set_rows(std::vector<DataGridViewRow> rows);
    // When the removed row held the current cell, the row that takes its
    // position, or the new last row, becomes current. Throws
    // std::invalid_argument for the new row, which is not a row of the grid's
    // until a value is committed to it.
    void remove_row_at(DataGridViewRowPosition position);
    // Removes every row of any source and returns to unbound rows.
    void clear_rows();
    // The ID of the row at a position. Throws std::out_of_range past the end.
    [[nodiscard]] DataGridViewRowId row_id(DataGridViewRowPosition position) const;
    // The position of the row with an ID, or none.
    [[nodiscard]] std::optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const;
    // Throws std::out_of_range for a cell past the rows or columns.
    [[nodiscard]] BindingValue cell_value(DataGridViewRowPosition row, std::size_t column) const;
    // The text a cell shows (Windows Forms' FormattedValue): its value
    // formatted by cell_formatting or its inherited style's format and null
    // value, a button or link column's text for an empty button or link
    // cell, an image column's description. Raises cell_formatting.
    [[nodiscard]] std::string cell_text(DataGridViewRowPosition row, std::size_t column) const;
    // Whether a link cell's link is visited (Windows Forms'
    // DataGridViewLinkCell.LinkVisited). The visited state stays on its row
    // when rows move; it ends with its row, when the rows or columns are
    // replaced and when a column is removed. The setter repaints the cell. Both throw std::out_of_range for a cell past
    // the rows or columns and std::invalid_argument for a cell that is not
    // in a link column.
    [[nodiscard]] bool link_visited(DataGridViewRowPosition row, std::size_t column) const;
    void set_link_visited(DataGridViewRowPosition row, std::size_t column, bool visited);
    // Stores a value from the program (read-only does not apply) and raises
    // cell_value_changed when it differs. Returns false when a provider or
    // binding source refuses it.
    bool set_cell_value(DataGridViewRowPosition row, std::size_t column, BindingValue value);
    // Orders unbound rows stably by one column's values (empty values
    // first, then booleans, numbers and text, unless sort_compare decides)
    // and shows the sort glyph on that column alone; it becomes the sorted
    // column. The current cell and the selection stay on their rows. Raises
    // sorted. Returns false, changing nothing, for virtual or bound rows, no
    // direction, or when an edit in progress does not commit. Throws
    // std::out_of_range for an unknown column.
    bool sort(std::size_t column, DataGridViewSortDirection direction);
    // Orders unbound rows stably by `comparer` (borrowed for the call); no
    // column is then the sorted column and every sort glyph is cleared.
    // Raises sorted. Returns false as the other sort does.
    bool sort(const DataGridViewRowComparer& comparer);
    // The column the last sort ordered by, and its direction; none (and
    // DataGridViewSortDirection::none) when the rows are not sorted by a
    // column: before any sort, after sort(comparer), and once the rows or
    // columns are replaced or the sorted column removed.
    [[nodiscard]] std::optional<std::size_t> sorted_column() const noexcept { return sorted_column_; }
    [[nodiscard]] DataGridViewSortDirection sort_order() const noexcept { return sort_order_; }
    // A column's sort mode, its kind's default when its sort_mode is empty.
    // Throws std::out_of_range for an unknown column.
    [[nodiscard]] DataGridViewColumnSortMode column_sort_mode(std::size_t column) const;
    void set_column_sort_mode(std::size_t column, std::optional<DataGridViewColumnSortMode> mode);

    // -- clipboard -------------------------------------------------------
    // Ctrl+C (Command+C on macOS) and Ctrl+Insert, while no cell is in
    // edit, put clipboard_content on the host's clipboard as text.
    [[nodiscard]] DataGridViewClipboardCopyMode clipboard_copy_mode() const noexcept {
        return clipboard_copy_mode_;
    }
    void set_clipboard_copy_mode(DataGridViewClipboardCopyMode mode);
    // The selection as text (Windows Forms' GetClipboardContent, its text
    // format): one line per row with a selected cell, in order, ending
    // "\r\n"; one tab-separated field per column with a selected cell, the
    // cell's text (cell_text) where the cell is selected and empty
    // otherwise; the header texts the copy mode includes first. None when
    // copying is disabled or no cell is selected.
    [[nodiscard]] std::optional<std::string> clipboard_content() const;

    // Switches to virtual rows: `order` lists the rows' IDs in presentation
    // order, and their values are asked of `provider` by ID. Unbound rows are
    // discarded and any edit, the selection and the current cell end. Throws
    // std::invalid_argument, changing nothing, for a null provider or an
    // empty, repeated or invalid UTF-8 ID.
    void set_virtual_rows(std::shared_ptr<DataGridViewCellValueProvider> provider,
                          std::vector<DataGridViewRowId> order);
    // Replaces the virtual rows' order after the application inserted,
    // removed, filtered or sorted rows (ObjectView's complete replacement).
    // Selected rows and cells, the current cell, an edit in progress and the
    // first displayed row stay on their IDs. A selected row that is gone
    // leaves the selection; when the current row is gone, its edit is
    // discarded and the row now at its position, or the new last row, becomes
    // current. Throws std::logic_error unless the rows are virtual, and
    // std::invalid_argument, changing nothing, for an empty, repeated or
    // invalid UTF-8 ID.
    void set_virtual_row_order(std::vector<DataGridViewRowId> order);

    // -- the virtual row order (owner decision W-5) ----------------------
    // Virtual rows whose order a window source keeps: the grid starts in the
    // mode the order policy gives (window, unless it is pinned to edit or the
    // automatic policy finds few rows) and asks the source for IDs. Throws
    // std::invalid_argument for a null provider or source.
    void set_virtual_rows(std::shared_ptr<DataGridViewCellValueProvider> provider,
                          std::shared_ptr<DataGridViewRowWindowSource> source);
    // Gives the virtual rows a window source, which window mode needs, or
    // takes it away (null): in window mode the grid first fetches every ID
    // from the old source and goes to edit mode. Throws std::logic_error
    // unless the rows are virtual.
    void set_virtual_row_window_source(std::shared_ptr<DataGridViewRowWindowSource> source);
    [[nodiscard]] std::shared_ptr<DataGridViewRowWindowSource> virtual_row_window_source() const noexcept {
        return window_source_;
    }
    // Reports changes the application made to its order, each costing in
    // proportion to the change (see DataGridViewRowOrder). The rows are
    // already in their new places: the inserted IDs stand at [position,
    // position + ids.size()), a moved row at `position`. Selected rows and
    // cells, the current cell, an edit and the first displayed row stay on
    // their IDs; a removed selected row leaves the selection, and when the
    // current row is removed its edit is discarded and the row now at its
    // position, or the new last row, becomes current. In edit mode an
    // invalid report (an empty, repeated or invalid UTF-8 ID, a removed or
    // moved row that is not there) throws std::invalid_argument and a position
    // past the end std::out_of_range, changing nothing; in window mode the
    // source is trusted. Throws std::logic_error unless the rows are virtual.
    void insert_virtual_rows(DataGridViewRowPosition position, std::vector<DataGridViewRowId> ids);
    void remove_virtual_rows(std::vector<DataGridViewRowId> ids);
    void move_virtual_row(const DataGridViewRowId& row, DataGridViewRowPosition position);
    // The order's mode, and who chooses it. The automatic policy switches by
    // the thresholds after each report, reset and policy or source change
    // (never during layout), raising row_order_mode_changed; pinning a mode
    // switches to it at once. Pinning window throws std::logic_error without
    // a window source.
    [[nodiscard]] DataGridViewRowOrderMode row_order_mode() const noexcept { return virtual_order_.mode(); }
    [[nodiscard]] DataGridViewRowOrderPolicy row_order_policy() const noexcept { return row_order_policy_; }
    void set_row_order_policy(DataGridViewRowOrderPolicy policy);
    [[nodiscard]] const DataGridViewRowOrderThresholds& row_order_thresholds() const noexcept {
        return row_order_thresholds_;
    }
    // Throws std::invalid_argument when edit_rows exceeds window_rows or
    // edit_changes exceeds window_changes.
    void set_row_order_thresholds(DataGridViewRowOrderThresholds thresholds);
    [[nodiscard]] const DataGridViewRowOrderStatistics& row_order_statistics() const noexcept {
        return virtual_order_.statistics();
    }
    // Repaints after the provider's values changed outside the grid.
    void refresh_rows();
    // Shows a BindingSource's records as rows: a column shows the field its
    // data_property_name names. The current row follows the source's
    // position and moves it; a committed edit moves the position to its row
    // and sets the field. A null source returns to unbound rows.
    void set_data_source(std::shared_ptr<BindingSource> source);
    [[nodiscard]] std::shared_ptr<BindingSource> data_source() const noexcept { return data_source_; }

    // -- adding and deleting rows (owner decision B1-2) ------------------
    // With allow_user_to_add_rows (on by default) unbound and bound grids
    // show a new row after the last row (Windows Forms' new row); virtual
    // grids do not. A bound grid shows it while its source allows new
    // records; a read-only grid or one without columns never does. The new
    // row has a generated ID and empty values; when it becomes current,
    // default_values_needed gives it its first values. The first value the
    // user commits to it (an edit, a toggle or a choice) makes it a row of the
    // grid's, or a record of the source's, with the same ID: user_added_row
    // is raised, and a new row appears after it. Until the current cell
    // leaves it, the added row can be discarded with cancel_row_edit (Escape
    // when no cell is in edit).
    [[nodiscard]] bool allow_user_to_add_rows() const noexcept { return allow_add_rows_; }
    void set_allow_user_to_add_rows(bool allow);
    // The new row's position, while it is shown.
    [[nodiscard]] std::optional<DataGridViewRowPosition> new_row_index() const noexcept;
    // With allow_user_to_delete_rows (on by default) Delete, pressed while no
    // cell is in edit, deletes the selected rows (selected_rows(), never the
    // new row) of an unbound grid or of a source that allows removal: each
    // raises user_deleting_row, which may keep it, then is removed and raises
    // user_deleted_row.
    [[nodiscard]] bool allow_user_to_delete_rows() const noexcept { return allow_delete_rows_; }
    void set_allow_user_to_delete_rows(bool allow);
    // Discards the row the user added, while it is the current row and the
    // current cell has not left it since; any edit is discarded first.
    // Raises row_edit_cancelled. Returns false when there is no such row.
    bool cancel_row_edit();

    // -- error text ------------------------------------------------------
    // A cell's or row's error text (Windows Forms' ErrorText), kept by row ID
    // for every row source; it ends with its row, when the rows or columns
    // are replaced and when a column is removed. A bound row with no text of
    // the grid's shows its record's error for the column's field, or for the
    // record as a whole. A cell with error text shows the error glyph at its
    // end (with show_cell_errors), a row with error text one in its header
    // (with show_row_errors); both publish it as their semantic description
    // and are invalid. The setters raise cell_error_text_changed and
    // row_error_text_changed. Throw std::out_of_range past the rows or
    // columns and std::invalid_argument for invalid UTF-8.
    [[nodiscard]] std::string cell_error_text(DataGridViewRowPosition row, std::size_t column) const;
    void set_cell_error_text(DataGridViewRowPosition row, std::size_t column, std::string text);
    [[nodiscard]] std::string row_error_text(DataGridViewRowPosition row) const;
    void set_row_error_text(DataGridViewRowPosition row, std::string text);
    [[nodiscard]] bool show_cell_errors() const noexcept { return show_cell_errors_; }
    void set_show_cell_errors(bool show);
    [[nodiscard]] bool show_row_errors() const noexcept { return show_row_errors_; }
    void set_show_row_errors(bool show);
    // The pencil the current row's header shows while the row is dirty
    // (Windows Forms' ShowEditingIcon); on by default.
    [[nodiscard]] bool show_editing_icon() const noexcept { return show_editing_icon_; }
    void set_show_editing_icon(bool show);

    // -- cell styles (T4 style mapping) ----------------------------------
    // The grid's styles (Windows Forms' DefaultCellStyle,
    // RowsDefaultCellStyle, AlternatingRowsDefaultCellStyle,
    // ColumnHeadersDefaultCellStyle and RowHeadersDefaultCellStyle). All are
    // empty by default: an unstyled grid draws as its theme does. The
    // setters throw std::invalid_argument for an invalid style (see
    // set_cell_style) and repaint.
    [[nodiscard]] const DataGridViewCellStyle& default_cell_style() const noexcept { return default_cell_style_; }
    void set_default_cell_style(DataGridViewCellStyle style);
    [[nodiscard]] const DataGridViewCellStyle& rows_default_cell_style() const noexcept {
        return rows_default_cell_style_;
    }
    void set_rows_default_cell_style(DataGridViewCellStyle style);
    [[nodiscard]] const DataGridViewCellStyle& alternating_rows_default_cell_style() const noexcept {
        return alternating_rows_default_cell_style_;
    }
    void set_alternating_rows_default_cell_style(DataGridViewCellStyle style);
    [[nodiscard]] const DataGridViewCellStyle& column_headers_default_cell_style() const noexcept {
        return column_headers_default_cell_style_;
    }
    void set_column_headers_default_cell_style(DataGridViewCellStyle style);
    [[nodiscard]] const DataGridViewCellStyle& row_headers_default_cell_style() const noexcept {
        return row_headers_default_cell_style_;
    }
    void set_row_headers_default_cell_style(DataGridViewCellStyle style);
    // A column's cells' style and its header's style. Throw std::out_of_range
    // for an unknown column.
    void set_column_default_cell_style(std::size_t column, DataGridViewCellStyle style);
    void set_column_header_cell_style(std::size_t column, DataGridViewCellStyle style);
    // A row's and a cell's own styles (Windows Forms' Row.DefaultCellStyle
    // and Cell.Style), kept by row ID for every row source as error text is:
    // a style ends with its row, when the rows or columns are replaced and
    // when a column is removed. The getters and setters throw
    // std::out_of_range past the rows or columns; every style setter throws
    // std::invalid_argument for a padding that is negative or not finite, a
    // format that is not a standard numeric format, an alignment outside
    // the enumeration, or a text value that is not valid UTF-8.
    [[nodiscard]] DataGridViewCellStyle row_default_cell_style(DataGridViewRowPosition row) const;
    void set_row_default_cell_style(DataGridViewRowPosition row, DataGridViewCellStyle style);
    [[nodiscard]] DataGridViewCellStyle cell_style(DataGridViewRowPosition row, std::size_t column) const;
    void set_cell_style(DataGridViewRowPosition row, std::size_t column, DataGridViewCellStyle style);
    // The style a cell draws with (Windows Forms' InheritedStyle): each
    // property from the first style that sets it, in the order the cell's
    // style, its row's, the alternating rows' (on odd positions), the rows',
    // its column's, the grid's. Properties none sets stay empty: the
    // theme's. Throws std::out_of_range past the rows or columns.
    [[nodiscard]] DataGridViewCellStyle inherited_cell_style(DataGridViewRowPosition row, std::size_t column) const;
    // A column header's style: its own, the column headers', the grid's.
    // Throws std::out_of_range for an unknown column.
    [[nodiscard]] DataGridViewCellStyle inherited_column_header_style(std::size_t column) const;
    // A row header's style: the row headers', the grid's.
    [[nodiscard]] DataGridViewCellStyle inherited_row_header_style() const;
    // With enable_headers_visual_styles (the default) the theme draws the
    // headers and their styles' colours do not apply, as Windows Forms
    // ignores them under visual styles (turned off, the header styles'
    // colours paint the header, its label and its glyph, the selection
    // colours while selected); their font, alignment and padding
    // still do.
    [[nodiscard]] bool enable_headers_visual_styles() const noexcept { return enable_headers_visual_styles_; }
    void set_enable_headers_visual_styles(bool enable);
    // The grid lines' colour (Windows Forms' GridColor) and the colour of the
    // area no cell covers (BackgroundColor); none is the theme's. Both are
    // authored colours (owner decision T4-1); under the cells the theme's
    // ground stays.
    [[nodiscard]] std::optional<Color> grid_color() const noexcept { return grid_color_; }
    void set_grid_color(std::optional<Color> color);
    [[nodiscard]] std::optional<Color> background_color() const noexcept { return background_color_; }
    void set_background_color(std::optional<Color> color);

    // -- borders ---------------------------------------------------------
    // The grid's frame (Windows Forms' BorderStyle; line by default, as
    // FixedSingle): none publishes no edge part. sunken (Fixed3D) is the
    // theme's recess, as line is.
    [[nodiscard]] BorderStyle border_style() const noexcept { return border_style_; }
    void set_border_style(BorderStyle style);
    // Which lines the cells draw (single by default).
    [[nodiscard]] DataGridViewCellBorderStyle cell_border_style() const noexcept { return cell_border_style_; }
    void set_cell_border_style(DataGridViewCellBorderStyle style);
    // The lines between column headers and between row headers (raised by
    // default, as in Windows Forms). With none a column header keeps its
    // resize grip.
    [[nodiscard]] DataGridViewHeaderBorderStyle column_headers_border_style() const noexcept {
        return column_headers_border_style_;
    }
    void set_column_headers_border_style(DataGridViewHeaderBorderStyle style);
    [[nodiscard]] DataGridViewHeaderBorderStyle row_headers_border_style() const noexcept {
        return row_headers_border_style_;
    }
    void set_row_headers_border_style(DataGridViewHeaderBorderStyle style);

    // -- owner drawing (owner decision W-Q6) -----------------------------
    // In ItemDrawMode::owner_draw the grid raises draw_item_content for each
    // data cell it paints (Windows Forms' CellPainting, content only): the
    // grid paints the cell's selection plate, grid lines, error glyph and
    // focus cue by role, and the handler paints inside the cell's content
    // box (the cell less its padding). `index` is the row's position,
    // `column` the column and `stable_id` the cell's stable ID; `text` and
    // `font` are those of the cell's default content. Unless a handler sets
    // `draw_default`, the grid draws none of the cell's content parts (text,
    // link, image, check box, button); a combo box cell keeps its drop-down
    // button. Handlers must not change the grid; a paint stops at a change.
    [[nodiscard]] ItemDrawMode draw_mode() const noexcept { return draw_mode_; }
    void set_draw_mode(ItemDrawMode mode);
    [[nodiscard]] Event<ItemContentPaint&>& draw_item_content() noexcept { return draw_item_content_; }

    // -- appearance ------------------------------------------------------
    [[nodiscard]] bool column_headers_visible() const noexcept { return column_headers_visible_; }
    void set_column_headers_visible(bool visible);
    [[nodiscard]] bool row_headers_visible() const noexcept { return row_headers_visible_; }
    void set_row_headers_visible(bool visible);
    [[nodiscard]] double column_headers_height() const noexcept { return column_headers_height_; }
    void set_column_headers_height(double height);
    [[nodiscard]] double row_headers_width() const noexcept { return row_headers_width_; }
    void set_row_headers_width(double width);
    // The height of every row without its own (Windows Forms'
    // RowTemplate.Height). Throws std::out_of_range outside [4, 256].
    [[nodiscard]] double row_height() const noexcept { return row_height_; }
    void set_row_height(double height);
    // A row's height (Windows Forms' DataGridViewRow.Height): its own, or
    // row_height(). Any row may have its own, of any source, virtual rows
    // included; it stays with the row's ID while the row moves and ends with
    // the row. Rows are found by offset through an index (see
    // DataGridViewRowHeights), so scrolling, hit testing and painting stay
    // logarithmic in the row count; a change to the rows rebuilds the index
    // in one pass over them while any row has its own height. Both throw
    // std::out_of_range for a row past the end; the setter throws
    // std::out_of_range for a height outside [4, 4096] and raises
    // row_height_changed when the height changes.
    [[nodiscard]] double row_height_at(DataGridViewRowPosition row) const;
    void set_row_height_at(DataGridViewRowPosition row, double height);
    // Returns a row to row_height(), raising row_height_changed when that
    // changes its height. Throws std::out_of_range for a row past the end.
    void reset_row_height_at(DataGridViewRowPosition row);
    // Automatic row heights (Windows Forms' AutoSizeRowsMode), default none.
    // Applied when the grid lays out: the all modes after a change to the
    // values, rows, columns, column widths, styles or text scale, measuring
    // every row (virtual rows ask their provider for every cell); the
    // displayed modes on every pass, measuring from the first displayed row
    // down until the viewport is full, the rows keeping their measured
    // heights once scrolled away. Automatic heights raise no
    // row_height_changed and override own heights, which set_row_height_at
    // still records for when the mode returns to none; the user does not
    // resize rows meanwhile.
    [[nodiscard]] DataGridViewAutoSizeRowsMode auto_size_rows_mode() const noexcept { return auto_size_rows_mode_; }
    void set_auto_size_rows_mode(DataGridViewAutoSizeRowsMode mode);
    // Gives a row its preferred height once, as its own (Windows Forms'
    // AutoResizeRow), raising row_height_changed when it changes. Throws
    // std::out_of_range for a row past the end.
    void auto_resize_row(DataGridViewRowPosition row,
                         DataGridViewAutoSizeRowMode mode = DataGridViewAutoSizeRowMode::all_cells);
    // Sizes every row once, or the rows shown for a displayed mode
    // (AutoResizeRows). Throws std::invalid_argument for none.
    void auto_resize_rows(DataGridViewAutoSizeRowsMode mode = DataGridViewAutoSizeRowsMode::all_cells);
    // The height, in logical units at text scale 1, a cell or a row header
    // asks for: its content (a wrapped text cell's lines at its column's
    // width), its padding and its grid line (Windows Forms'
    // GetPreferredHeight).
    [[nodiscard]] double preferred_cell_height(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] double preferred_row_header_height(DataGridViewRowPosition row) const;
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    // A read-only grid edits no cell.
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }
    void set_read_only(bool read_only);
    [[nodiscard]] bool allow_user_to_resize_columns() const noexcept { return allow_column_resize_; }
    void set_allow_user_to_resize_columns(bool allow);
    // Whether the user drags a row header's bottom edge to give the row its
    // own height (Windows Forms' AllowUserToResizeRows), on by default. Each
    // move sets the height, as set_row_height_at does.
    [[nodiscard]] bool allow_user_to_resize_rows() const noexcept { return allow_row_resize_; }
    void set_allow_user_to_resize_rows(bool allow);

    // -- column order ------------------------------------------------------
    // The grid shows its columns in display order: index order once the
    // columns are set; an added column goes last and a removed one leaves
    // no gap. Display order rules what the user sees and walks: the
    // columns' places, Left and Right, Tab, range selection, the semantic
    // column indices and the clipboard's fields. Column indices (cell
    // addresses, events) never change with it.
    [[nodiscard]] DataGridViewDisplayIndex column_display_index(std::size_t column) const;
    // Moves a column to a display index, the columns between shifting by one
    // (Windows Forms' DisplayIndex setter), and raises
    // column_display_index_changed for every column whose display index
    // changed, in their new order. Throws std::out_of_range for an unknown
    // column or an index past the last.
    void set_column_display_index(std::size_t column, DataGridViewDisplayIndex display_index);
    // The column shown at a display index. Throws std::out_of_range past the
    // last.
    [[nodiscard]] std::size_t column_at_display_index(DataGridViewDisplayIndex display_index) const;
    // Whether the user may drag a column header to a new place (Windows
    // Forms' AllowUserToOrderColumns; off by default). The drag starts once
    // the pointer moves 4 units from the press; the column_drop_indicator
    // part shows where the column will go, and the release moves it without
    // a header click.
    [[nodiscard]] bool allow_user_to_order_columns() const noexcept { return allow_column_order_; }
    void set_allow_user_to_order_columns(bool allow);

    // -- scrolling -------------------------------------------------------
    // The position of the first row shown under the column headers.
    [[nodiscard]] DataGridViewRowPosition first_displayed_row() const noexcept {
        const DataGridViewRowPosition first{first_row_};
        return first;
    }
    void set_first_displayed_row(DataGridViewRowPosition row);
    // How far the cells are scrolled left, in logical units at text scale 1.
    [[nodiscard]] double horizontal_scroll_offset() const noexcept { return horizontal_offset_; }
    void set_horizontal_scroll_offset(double offset);
    // The rows a paint shows, partly visible ones included: positions
    // [first_displayed_row, displayed_row_end).
    [[nodiscard]] DataGridViewRowPosition displayed_row_end() const;
    [[nodiscard]] std::shared_ptr<VScrollBar> vertical_scroll_bar() const noexcept { return vertical_bar_; }
    [[nodiscard]] std::shared_ptr<HScrollBar> horizontal_scroll_bar() const noexcept { return horizontal_bar_; }

    // -- selection -------------------------------------------------------
    [[nodiscard]] DataGridViewSelectionMode selection_mode() const noexcept { return selection_mode_; }
    // Clears the selection.
    void set_selection_mode(DataGridViewSelectionMode mode);
    [[nodiscard]] bool multi_select() const noexcept { return multi_select_; }
    // Turning it off keeps only the current cell (or row) selected.
    void set_multi_select(bool multi_select);
    // The current cell at its row's present position, or none. The grid keeps
    // it by row ID, so it stays on its row when rows move.
    [[nodiscard]] std::optional<DataGridViewCellAddress> current_cell() const;
    // The current cell's row ID, or none.
    [[nodiscard]] std::optional<DataGridViewRowId> current_row_id() const;
    // Moves the current cell, committing any edit first, and selects it alone.
    // Returns false, changing nothing, when the edit does not commit. Throws
    // std::out_of_range for a cell past the rows or columns.
    bool set_current_cell(std::optional<DataGridViewCellAddress> cell);
    [[nodiscard]] bool is_cell_selected(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] bool is_row_selected(DataGridViewRowPosition row) const;
    // In full-row mode a cell's selection is its row's. Throws
    // std::out_of_range for a cell past the rows or columns.
    void set_cell_selected(DataGridViewRowPosition row, std::size_t column, bool selected);
    void set_row_selected(DataGridViewRowPosition row, bool selected);
    // The selected cells at their rows' present positions, by row then
    // column; in full-row mode every cell of every selected row.
    [[nodiscard]] std::vector<DataGridViewCellAddress> selected_cells() const;
    // The selected rows' present positions in order: full-row selections, or
    // in cell mode the rows whose every cell is selected.
    [[nodiscard]] std::vector<DataGridViewRowPosition> selected_rows() const;
    void clear_selection();
    // Selects every cell (or row); allowed only with multi_select.
    void select_all();

    // -- editing ---------------------------------------------------------
    [[nodiscard]] bool is_current_cell_in_edit_mode() const noexcept { return editing_; }
    // Puts the current text cell in edit, or opens the current combo box
    // cell's choices. Returns false for no current cell, a read-only cell, a
    // check box or button cell, or a cancelled cell_begin_edit.
    bool begin_edit(bool select_all = true);
    // Commits the edit in progress: cell_validating, then conversion to the
    // column's value kind, then the value. Returns true when no edit is in
    // progress or it committed; false leaves the cell in edit, marked invalid.
    bool end_edit();
    // Discards the edit in progress.
    void cancel_edit();
    [[nodiscard]] DataGridViewEditMode edit_mode() const noexcept { return edit_mode_; }
    void set_edit_mode(DataGridViewEditMode mode);
    // Stores the text edit's value without leaving edit (Windows Forms'
    // CommitEdit): conversion, then the value; cell_validating is not
    // raised. Returns true when no text edit is in progress or it stored;
    // false (after data_error) when it did not.
    bool commit_edit();
    // Shows the cell's stored value in the editor again, discarding what was
    // typed (Windows Forms' RefreshEdit). False when no text edit is in
    // progress.
    bool refresh_edit();
    // Whether the edit in progress holds a value the cell does not: the user
    // changed the editor's text, or notify_current_cell_dirty said so.
    [[nodiscard]] bool is_current_cell_dirty() const noexcept { return cell_dirty_; }
    // Whether the current row holds changes the user made since it became
    // current: a dirty cell, a committed value, or a row the user added.
    [[nodiscard]] bool is_current_row_dirty() const noexcept;
    // Marks the edit in progress dirty or clean, raising
    // current_cell_dirty_state_changed when that changes; ignored when no
    // edit is in progress.
    void notify_current_cell_dirty(bool dirty);
    // The hosted editor. It is laid out over the current cell only while a
    // text cell is in edit.
    [[nodiscard]] std::shared_ptr<TextBox> editing_control() const noexcept { return editor_; }
    [[nodiscard]] bool drop_down_open() const noexcept;
    void close_drop_down() noexcept;

    // -- events ----------------------------------------------------------
    // Raised once after an operation changes which cells or rows are selected.
    [[nodiscard]] Event<>& selection_changed() noexcept { return selection_changed_; }
    // Raised after the current cell moves to another cell, with the new cell
    // or none; not when only its row's position changes.
    [[nodiscard]] Event<std::optional<DataGridViewCellAddress>>& current_cell_changed() noexcept {
        return current_cell_changed_;
    }
    [[nodiscard]] Event<DataGridViewCellCancelEvent&>& cell_begin_edit() noexcept { return cell_begin_edit_; }
    // Raised when an edit ends, committed or discarded.
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_end_edit() noexcept { return cell_end_edit_; }
    [[nodiscard]] Event<DataGridViewCellValidatingEvent&>& cell_validating() noexcept {
        return cell_validating_;
    }
    // Raised after a cell's value changes: an edit, a toggle, a choice or
    // set_cell_value.
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_value_changed() noexcept {
        return cell_value_changed_;
    }
    // Raised when a button cell, an image cell's image or a link cell's
    // link is clicked, or a check box cell toggled, by the user.
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_content_click() noexcept {
        return cell_content_click_;
    }
    // Raised after the virtual row order switched mode, with the reason.
    [[nodiscard]] Event<const DataGridViewRowOrderModeChange&>& row_order_mode_changed() noexcept {
        return row_order_mode_changed_;
    }
    // Raised when a value does not reach its cell (owner decision B1-1).
    [[nodiscard]] Event<DataGridViewDataErrorEvent&>& data_error() noexcept { return data_error_; }
    // Raised after an edit's value is validated and stored, before
    // cell_end_edit.
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_validated() noexcept { return cell_validated_; }
    // Raised with the new state when is_current_cell_dirty changes.
    [[nodiscard]] Event<bool>& current_cell_dirty_state_changed() noexcept {
        return current_cell_dirty_state_changed_;
    }
    // Moving the current cell, by the user or the program, runs in this
    // order. The edit in progress commits (cell_validating, cell_validated,
    // cell_end_edit); a commit that fails stops the move. When the row
    // changes, row_validating may keep the current row. Then the current
    // cell moves and cell_leave, row_leave (when the row changes),
    // row_validated (when it was validated), row_enter, default_values_needed
    // (entering the new row), cell_enter and current_cell_changed are raised.
    // Handlers of these events may not call set_current_cell
    // (std::logic_error); a handler that moves rows or the current cell
    // otherwise ends the remaining enter events of the move. A move that
    // follows a binding source's position is not validated.
    [[nodiscard]] Event<DataGridViewRowCancelEvent&>& row_validating() noexcept { return row_validating_; }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_validated() noexcept { return row_validated_; }
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_enter() noexcept { return cell_enter_; }
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_leave() noexcept { return cell_leave_; }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_enter() noexcept { return row_enter_; }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_leave() noexcept { return row_leave_; }
    [[nodiscard]] Event<DataGridViewRowValuesEvent&>& default_values_needed() noexcept {
        return default_values_needed_;
    }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& user_added_row() noexcept { return user_added_row_; }
    [[nodiscard]] Event<DataGridViewRowCancelEvent&>& user_deleting_row() noexcept { return user_deleting_row_; }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& user_deleted_row() noexcept { return user_deleted_row_; }
    // Raised after cancel_row_edit discarded the row the user added.
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_edit_cancelled() noexcept { return row_edit_cancelled_; }
    [[nodiscard]] Event<const DataGridViewCellEvent&>& cell_error_text_changed() noexcept {
        return cell_error_text_changed_;
    }
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_error_text_changed() noexcept {
        return row_error_text_changed_;
    }
    // Raised by cell_error_text and row_error_text for virtual and bound
    // rows, so that the application can give error text it keeps itself.
    [[nodiscard]] Event<DataGridViewCellErrorTextNeededEvent&>& cell_error_text_needed() noexcept {
        return cell_error_text_needed_;
    }
    [[nodiscard]] Event<DataGridViewRowErrorTextNeededEvent&>& row_error_text_needed() noexcept {
        return row_error_text_needed_;
    }
    // Raised when a column header is clicked, with the column's index.
    [[nodiscard]] Event<std::size_t>& column_header_click() noexcept { return column_header_click_; }
    [[nodiscard]] Event<DataGridViewSortCompareEvent&>& sort_compare() noexcept { return sort_compare_; }
    // Raised after a sort ordered the rows.
    [[nodiscard]] Event<>& sorted() noexcept { return sorted_; }
    // Raised after a column's width changes, with the column's index.
    [[nodiscard]] Event<std::size_t>& column_width_changed() noexcept { return column_width_changed_; }
    // Raised after a column's display index changes, with the column's index.
    [[nodiscard]] Event<std::size_t>& column_display_index_changed() noexcept {
        return column_display_index_changed_;
    }
    // Raised after a row's height changes (set_row_height_at,
    // reset_row_height_at), with the row.
    [[nodiscard]] Event<const DataGridViewRowEvent&>& row_height_changed() noexcept { return row_height_changed_; }
    [[nodiscard]] Event<DataGridViewCellFormattingEvent&>& cell_formatting() noexcept { return cell_formatting_; }
    [[nodiscard]] Event<DataGridViewCellParsingEvent&>& cell_parsing() noexcept { return cell_parsing_; }

    // -- Control ---------------------------------------------------------
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_attachment_committed() noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members.
    struct Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry edge;
        PartGeometry column_header_band;
        PartGeometry row_header_band;
        PartGeometry top_left_header;
        PartGeometry horizontal_scroll_bar;
        PartGeometry vertical_scroll_bar;
        PartGeometry scroll_corner;
        PartGeometry editor;
        PartGeometry focus;
        // Where a dragged column header would go: a bar at that column
        // boundary, present only during the drag.
        PartGeometry column_drop_indicator;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool read_only{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

    // The "row" item system: one realized item per visible row.
    struct RowParts final {
        PartGeometry row_header;
        PartGeometry row_header_label;
        // The current row's arrow, the pencil while it is dirty, or the new
        // row's star (owner decision B1-3).
        PartGeometry row_glyph;
        // The error glyph of a row with error text, at the header's end.
        PartGeometry row_error;
        PartGeometry row_rule;
        // The resize hit region on the header's bottom edge, while the user
        // may resize rows.
        PartGeometry row_grip;
    };
    struct RowStates final {
        bool selected{};
        bool current{};
        bool read_only{};
        // A row with error text.
        bool invalid{};
        // A row whose height the user is dragging.
        bool captured{};
    };
    struct Row final {
        DataGridViewRowPosition position{};
        std::string stable_id;
        RowParts parts;
        RowStates states;
    };
    [[nodiscard]] std::vector<Row> realized_rows() const;

    // The "cell" item system: one realized item per visible cell. A part a
    // cell's kind does not show is absent.
    struct CellParts final {
        PartGeometry cell;
        PartGeometry cell_selection;
        PartGeometry cell_content;
        PartGeometry cell_image;
        // A link cell's link: its text's extent, which a click must hit.
        PartGeometry cell_link;
        // The link's underline: a one-unit rule just below the baseline.
        PartGeometry cell_link_underline;
        PartGeometry cell_check_well;
        PartGeometry cell_check_indicator;
        PartGeometry cell_button_face;
        PartGeometry cell_button_label;
        PartGeometry cell_drop_down;
        PartGeometry cell_drop_down_glyph;
        PartGeometry cell_vertical_rule;
        PartGeometry cell_horizontal_rule;
        PartGeometry cell_focus;
        // The error glyph of a cell with error text, at the cell's end.
        PartGeometry cell_error;
    };
    struct CellStates final {
        bool selected{};
        bool current{};
        bool read_only{};
        // A cell whose edit did not commit, or with error text.
        bool invalid{};
        bool checked{};
        bool pressed{};
        bool open{};
        bool focus_visible{};
        // A link cell whose link is visited; a cell state of the grid's own
        // (as LinkLabel's).
        bool visited{};
    };
    struct Cell final {
        DataGridViewCellAddress address{};
        std::string stable_id;
        CellParts parts;
        CellStates states;
    };
    [[nodiscard]] std::vector<Cell> realized_cells() const;

    // The "column" item system: one realized item per visible column header.
    struct ColumnParts final {
        PartGeometry column_header;
        PartGeometry column_label;
        PartGeometry sort_glyph;
        PartGeometry column_divider;
        PartGeometry column_grip;
    };
    struct ColumnStates final {
        bool hot{};
        bool pressed{};
        bool captured{};
        // Application-defined column states, as ObjectView's.
        bool sorted_ascending{};
        bool sorted_descending{};
    };
    struct Column final {
        std::size_t index{};
        std::string stable_id;
        ColumnParts parts;
        ColumnStates states;
    };
    [[nodiscard]] std::vector<Column> realized_columns() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // Where the grid's bands, cells and bars sit, from one layout pass over
    // the client rectangle. Every rectangle is local; sizes are scaled.
    struct GridLayout final {
        Rect client{};
        Rect column_header_band{};
        Rect row_header_band{};
        Rect top_left{};
        // The cell viewport: below the column headers, right of the row
        // headers, and short of the scroll bars.
        Rect data{};
        Rect horizontal_bar{};
        Rect vertical_bar{};
        Rect scroll_corner{};
        double row_height{};
        double scale{1.0};
    };
    // What a local point is over.
    enum class HitZone : std::uint8_t {
        none,
        cell,
        column_header,
        column_divider,
        row_header,
        row_divider,
        top_left,
    };
    struct HitResult final {
        HitZone zone{HitZone::none};
        DataGridViewRowPosition row{};
        std::size_t column{};
    };
    // How a key or click moves the current cell and the selection.
    enum class SelectionGesture : std::uint8_t { replace, extend, toggle };
    // The kind of edit in progress.
    enum class EditKind : std::uint8_t { none, text, choice };

    // Row identity, as ObjectView keeps it: lookups by ID text without
    // building a string.
    struct RowIdHash final {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view id) const noexcept {
            const std::size_t result = std::hash<std::string_view>{}(id);
            return result;
        }
    };
    // Row ID text to the row's position in presentation order.
    using RowPositions = std::unordered_map<std::string, std::size_t, RowIdHash, std::equal_to<>>;
    using RowIdSet = std::unordered_set<std::string, RowIdHash, std::equal_to<>>;
    // The column indices of one row's selected cells, sorted and unique.
    using SelectedColumns = std::vector<std::size_t>;
    using SelectedCells = std::unordered_map<std::string, SelectedColumns, RowIdHash, std::equal_to<>>;
    // A cell by its row's identity: the current cell, the selection anchor
    // and a pressed button or image cell, which stay on their row when rows
    // move.
    struct CellKey final {
        std::string row_id{};
        std::size_t column{};

        friend bool operator==(const CellKey& left, const CellKey& right) {
            const bool equal = left.row_id == right.row_id && left.column == right.column;
            return equal;
        }
    };

    // Model access over the three row sources, by present position. The ID
    // is a copy: a virtual order in window mode may replace its window
    // while the caller still uses it.
    [[nodiscard]] std::string row_id_text(DataGridViewRowPosition row) const;
    [[nodiscard]] bool row_read_only(DataGridViewRowPosition row) const;
    [[nodiscard]] bool cell_read_only(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] BindingValue stored_value(DataGridViewRowPosition row, std::size_t column) const;
    bool store_value(DataGridViewRowPosition row, std::size_t column, const BindingValue& value);
    void validate_column(const DataGridViewColumn& column) const;
    // Throws std::invalid_argument for invalid UTF-8 or more values than
    // columns.
    void validate_row_text(const DataGridViewRow& row) const;
    [[nodiscard]] std::string generated_row_id(const RowPositions& used);
    // Throws std::invalid_argument for an empty, repeated or invalid UTF-8
    // ID in a virtual order.
    static void validate_virtual_order(const std::vector<DataGridViewRowId>& order);
    void require_virtual_rows(const char* what) const;
    // Before a reported change: the first displayed row's ID, when held
    // without asking a source.
    [[nodiscard]] std::optional<std::string> held_top_row() const;
    // After a reported change: the first displayed row back on its ID, the
    // change counted, and the mode chosen again.
    void after_reported_change(const std::optional<std::string>& top, std::size_t changed);
    // Chooses the mode by the policy and the thresholds, switching and
    // raising row_order_mode_changed when it changes. `cause` is the reason
    // reported for a switch the policy or a source made.
    void update_row_order_mode(DataGridViewRowOrderSwitchReason cause);
    void switch_row_order(DataGridViewRowOrderMode mode, DataGridViewRowOrderSwitchReason reason);
    void require_cell(DataGridViewRowPosition row, std::size_t column) const;
    void structure_changed();
    void reset_row_state();
    // Automatic column sizing: the width `mode` gives a column (logical
    // units; none for none and fill), applied by arrange to every auto-sized
    // column, fill columns last. values_changed is the invalidation of a
    // value change, which lays the grid out again when a column sizes by
    // its cells.
    [[nodiscard]] std::optional<double> automatic_width(std::size_t column, DataGridViewAutoSizeColumnMode mode,
                                                        const GridLayout& layout) const;
    void apply_auto_sizes();
    void distribute_fill_width();
    // A row's preferred height under a mode, from 4 to 4096; none when a
    // formatting handler changed the grid while it was measured. Applied by
    // arrange after the columns' widths (apply_row_auto_sizes).
    [[nodiscard]] std::optional<double> automatic_height(std::size_t position, DataGridViewAutoSizeRowMode mode) const;
    void apply_row_auto_sizes();
    [[nodiscard]] bool column_resizable(std::size_t column) const;
    [[nodiscard]] bool row_resizable() const noexcept;
    void values_changed();
    // Sorting: the comparison sort_compare's handlers decide (a functor
    // over row indices), the mutation refusal while comparisons run, the
    // reorder both sorts end with, and the automatic sort of a header click.
    struct SortCompareOrder;
    struct SortingScope;
    void require_not_sorting() const;
    void apply_row_order(const std::vector<std::size_t>& order);
    void sort_from_header(std::size_t column);
    bool copy_selection_to_clipboard();
    [[nodiscard]] std::string row_header_text(DataGridViewRowPosition row) const;
    void detach_data_source() noexcept;
    void on_source_list_changed(const BindingListChange& change);
    void on_source_position_changed(std::ptrdiff_t position);

    // The identity index. row_positions_ maps every row's ID to its position
    // and is rebuilt whenever rows are added, removed or reordered; a lookup
    // is checked against the row at the position it finds.
    void rebuild_row_index();
    // Writes the positions of the rows from `first` to the end (unbound).
    void index_rows_from(std::size_t first);
    [[nodiscard]] std::optional<std::size_t> find_row(std::string_view row_id) const;
    // The position an event reports for a row: its indexed position, which
    // for a row being removed is the position it had.
    [[nodiscard]] DataGridViewRowPosition event_position(std::string_view row_id) const;
    [[nodiscard]] DataGridViewCellEvent cell_event(const CellKey& cell) const;
    [[nodiscard]] CellKey key_of(DataGridViewCellAddress cell) const;
    [[nodiscard]] std::optional<DataGridViewCellAddress> address_of(const CellKey& cell) const;
    [[nodiscard]] std::optional<std::size_t> current_position() const;
    [[nodiscard]] bool is_current_row(DataGridViewRowPosition row) const;
    [[nodiscard]] bool is_current_cell(DataGridViewRowPosition row, std::size_t column) const;
    // After rows left the index: drops the selections, anchor and pressed
    // button of rows that are gone, and moves a current cell whose row is
    // gone to the row now at `former_current` (its row's old position), the
    // new last row, or none.
    void drop_missing_rows(std::optional<std::size_t> former_current);

    // Layout and geometry: one function per part, used by paint and by the
    // published anatomy.
    [[nodiscard]] GridLayout grid_layout() const;
    [[nodiscard]] double content_width() const noexcept;
    [[nodiscard]] double column_left(std::size_t column, const GridLayout& layout) const noexcept;
    [[nodiscard]] Rect column_header_bounds(std::size_t column, const GridLayout& layout) const noexcept;
    [[nodiscard]] Rect row_header_bounds(DataGridViewRowPosition row, const GridLayout& layout) const;
    [[nodiscard]] Rect cell_bounds(DataGridViewRowPosition row, std::size_t column,
                                   const GridLayout& layout) const;
    // The display indices of the first column shown and one past the last.
    [[nodiscard]] std::size_t first_visible_display(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t visible_display_end(const GridLayout& layout) const noexcept;
    // Display order upkeep: the order reset to index order, a column
    // appended or removed, and the display index a drag at `x` would give.
    void reset_display_order();
    void append_display_column();
    void remove_display_column(std::size_t column);
    [[nodiscard]] DataGridViewDisplayIndex order_target_at(double x, const GridLayout& layout) const;
    void end_column_order();
    // The row height index, rebuilt first when the rows changed since it
    // was built.
    [[nodiscard]] const DataGridViewRowHeights& row_heights() const;
    // A change to the rows: structure_changed, and the index rebuilt.
    void rows_changed();
    // A row's top relative to the first displayed row's, in layout units.
    [[nodiscard]] double row_offset(std::size_t position, const GridLayout& layout) const;
    // The first row that would show `last` whole at the viewport's bottom
    // (`last` itself when it is taller than the viewport).
    [[nodiscard]] std::size_t first_row_ending_at(std::size_t last, const GridLayout& layout) const;
    [[nodiscard]] std::size_t visible_row_end(const GridLayout& layout) const;
    // The rows shown whole from the first displayed row down.
    [[nodiscard]] std::size_t fully_visible_rows(const GridLayout& layout) const;
    [[nodiscard]] std::size_t maximum_first_row(const GridLayout& layout) const;
    [[nodiscard]] double maximum_horizontal_offset(const GridLayout& layout) const noexcept;
    [[nodiscard]] Parts grid_parts(const GridLayout& layout) const;
    [[nodiscard]] RowParts row_parts(DataGridViewRowPosition row, const GridLayout& layout) const;
    // A cell's formatted content: the text it shows (cell_text) and the style
    // it draws with, as cell_formatting left them.
    struct FormattedCell final {
        std::string text{};
        DataGridViewCellStyle style{};
    };
    [[nodiscard]] FormattedCell formatted_cell(DataGridViewRowPosition row, std::size_t column) const;
    // The text a cell shows for its formatted text: a button or link
    // column's text when it is empty, an image column's description.
    [[nodiscard]] std::string shown_text(DataGridViewRowPosition row, std::size_t column, std::string formatted) const;
    // The height of one line of text in `font` (already effective), as the
    // content role sets it.
    [[nodiscard]] double text_line_height(FontSpec font) const;
    // A cell's parts, laid out for its formatted content, with that content
    // and the content box (the cell less its padding) owner drawing paints
    // in. cell_parts is its parts alone.
    // One wrapped line of a cell's text: a byte range of it (trailing
    // spaces and line end excluded) and its width.
    struct WrappedTextLine final {
        std::size_t begin_byte{};
        std::size_t end_byte{};
        double width{};
    };
    // A wrapped text cell also has its lines, the box they take (the
    // content part, before it is cut to the cell) and their line height;
    // `lines` is empty when the text is not wrapped.
    struct CellLayout final {
        CellParts parts{};
        FormattedCell content{};
        Rect content_box{};
        std::vector<WrappedTextLine> lines{};
        Rect lines_box{};
        double line_height{};
    };
    // A style's colours for a part, selected or not; none where the theme's
    // stand.
    struct AuthoredColours final {
        std::optional<Color> background{};
        std::optional<Color> foreground{};
    };
    [[nodiscard]] static AuthoredColours authored_colours(const DataGridViewCellStyle& style, bool selected);
    [[nodiscard]] CellLayout cell_layout(DataGridViewRowPosition row, std::size_t column,
                                         const GridLayout& layout) const;
    [[nodiscard]] CellParts cell_parts(DataGridViewRowPosition row, std::size_t column,
                                       const GridLayout& layout) const;
    // A link cell's link within the cell's content area: its text's extent,
    // placed by the cell's alignment and cut at the content area's end, and
    // its underline, one unit high just below the text's baseline.
    struct CellLink final {
        Rect text{};
        Rect underline{};
    };
    // None for empty text or an empty content area.
    [[nodiscard]] std::optional<CellLink> cell_link(std::string_view text, PartTextAlignment alignment,
                                                    FontSpec font, Rect content) const;
    // Throws std::out_of_range for a cell past the rows or columns and
    // std::invalid_argument for one outside a link column.
    void require_link_cell(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] bool is_link_visited(std::string_view row_id, std::size_t column) const;
    // Where an image cell draws its image, before the cut at the cell's
    // edges, and the image; none for another kind of cell, an empty value or
    // an image the window does not hold.
    struct CellImage final {
        ImageId image{};
        Rect bounds{};
    };
    [[nodiscard]] std::optional<CellImage> cell_image(DataGridViewRowPosition row, std::size_t column,
                                                      Rect inside) const;
    [[nodiscard]] ColumnParts column_parts(std::size_t column, const GridLayout& layout) const;
    [[nodiscard]] AnatomyStateSet row_states(DataGridViewRowPosition row) const;
    [[nodiscard]] AnatomyStateSet cell_states(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] AnatomyStateSet column_states(std::size_t column) const;
    [[nodiscard]] HitResult hit_test(Point local) const;
    // Realized items in paint order: rows, then cells row by row, then
    // column headers. A slot names one: its item system, the row of a row or
    // cell, and the column of a cell or header.
    struct RealizedSlot final {
        ItemSystemIndex system{};
        DataGridViewRowPosition row{};
        std::size_t column{};
    };
    [[nodiscard]] RealizedSlot realized_slot(const GridLayout& layout, std::size_t item) const;
    [[nodiscard]] std::size_t realized_row_count(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t realized_column_count(const GridLayout& layout) const noexcept;
    void paint_row(Painter& painter, DataGridViewRowPosition row, const GridLayout& layout) const;
    // False when a cell_formatting or draw_item_content handler changed the
    // grid's structure: the paint then stops.
    [[nodiscard]] bool paint_cell(Painter& painter, DataGridViewRowPosition row, std::size_t column,
                                  const GridLayout& layout);
    // Throws std::invalid_argument for a style the setters refuse.
    static void validate_cell_style(const DataGridViewCellStyle& style);
    void require_column(std::size_t column) const;
    void paint_column_header(Painter& painter, std::size_t column, const GridLayout& layout) const;
    void synchronize_scroll_bars(const GridLayout& layout);
    void on_vertical_scroll(double value);
    void on_horizontal_scroll(double value);
    void scroll_into_view(DataGridViewCellAddress cell);

    // Selection, kept by row ID.
    [[nodiscard]] bool cell_in_selection(std::string_view row_id, std::size_t column) const;
    [[nodiscard]] bool row_in_selection(std::string_view row_id) const;
    [[nodiscard]] std::size_t selected_cell_count() const noexcept;
    void insert_selected_cell(const std::string& row_id, std::size_t column);
    void erase_selected_cell(std::string_view row_id, std::size_t column);
    void insert_selected_row(const std::string& row_id);
    void erase_selected_row(std::string_view row_id);
    void select_range(DataGridViewCellAddress from, DataGridViewCellAddress to);
    bool move_current(DataGridViewCellAddress target, SelectionGesture gesture);
    // A row header's click: makes `row` current in the current column and,
    // in cell mode with a plain click, selects the whole row. False when the
    // row could not become current.
    bool click_row_header(DataGridViewRowPosition row, SelectionGesture gesture);
    // Sets the current cell and raises the leave and enter events of the
    // move; `validated` says row_validating ran for a row change.
    void apply_current(std::optional<CellKey> cell, bool validated = false);
    // Before the current cell leaves its row for `target` (none: no cell):
    // row_validating. False when a handler kept the row.
    [[nodiscard]] bool validate_row_change(const std::optional<CellKey>& target);
    // Focus moved in the window: when it left the grid and its editor, the
    // edit ends and the current row is validated (Windows Forms' Leave).
    void on_window_focus_moved(const FocusedControlChange& change);
    // The error texts the grid holds, without raising the needed events.
    [[nodiscard]] std::string held_cell_error_text(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] std::string held_row_error_text(DataGridViewRowPosition row) const;
    [[nodiscard]] DataGridViewRowEvent row_event(std::string_view row_id) const;
    void require_settled_current() const;
    void clear_selection_state() noexcept;
    void select_current_alone();
    // Raises selection_changed when the selection revision moved past `before`.
    void publish_selection(std::uint64_t before);

    // Editing and activation.
    enum class EditTrigger : std::uint8_t { keystroke, f2, entering, pointer };
    [[nodiscard]] bool edit_trigger_allowed(EditTrigger trigger) const noexcept;
    bool begin_text_edit(std::string initial_text, bool select_all);
    // Converts and stores a text edit's value, raising data_error when it does
    // not reach the cell: stored (a changed value), unchanged (the cell held
    // it already), kept_invalid (the edit stays, marked invalid) or discarded
    // (a data_error handler gave the edit up).
    enum class CommitOutcome : std::uint8_t { stored, unchanged, kept_invalid, discarded };
    [[nodiscard]] CommitOutcome commit_text(const CellKey& key, DataGridViewCellAddress at, const std::string& text);
    // Stores a value the user gave, the new row becoming a row first. When
    // the value does not reach the cell, data_error is raised (commit) and
    // `keep_edit` is its handler's cancel.
    struct UserStore final {
        bool stored{};
        bool keep_edit{};
    };
    [[nodiscard]] UserStore store_user_value(DataGridViewCellAddress cell, const BindingValue& value,
                                             const std::string& formatted);
    void set_cell_dirty(bool dirty);
    void on_editor_text_changed(const std::string& text);
    bool open_choices();
    void on_choice_invoked(const MenuItemInvocation& invocation);
    void on_choices_open_changed(bool open);
    void on_editor_focus(bool focused);
    void finish_edit();
    bool toggle_check(DataGridViewCellAddress cell);
    void click_content(DataGridViewCellAddress cell);
    bool activate_current();

    // The new row (owner decision B1-2).
    [[nodiscard]] std::size_t data_row_count() const noexcept;
    [[nodiscard]] bool new_row_shown() const noexcept;
    [[nodiscard]] bool is_new_row(DataGridViewRowPosition row) const noexcept;
    // Gives the new row an ID when it has none, or when a row of the grid's
    // or a record took its ID (the current cell, anchor and selection follow).
    void renew_new_row_id(bool force);
    void rekey_row(const std::string& from, const std::string& to);
    // Makes the new row a row of the grid's or a record of the source's.
    bool add_new_row();
    void fill_default_values();
    bool delete_selected_rows();
    void remove_row_by_id(const std::string& row_id);

    // Navigation.
    [[nodiscard]] bool navigation_key(KeyEvent& event);
    [[nodiscard]] bool tab_key(bool backward);

    // Semantics.
    [[nodiscard]] std::string cell_stable_id(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] std::string column_stable_id(std::size_t column) const;
    [[nodiscard]] std::string row_header_prefix() const;
    [[nodiscard]] std::string cell_error_prefix() const;
    [[nodiscard]] std::string row_error_prefix() const;
    [[nodiscard]] SemanticNode error_glyph_node(std::string stable_id, const std::string& text,
                                                const PartGeometry& glyph, const Rect& absolute) const;
    [[nodiscard]] std::optional<DataGridViewCellAddress> cell_from_stable_id(std::string_view id) const;

    std::vector<DataGridViewColumn> columns_{};
    DataGridViewRowSource row_source_{DataGridViewRowSource::unbound};
    // Unbound rows, in presentation order.
    std::vector<DataGridViewRow> rows_{};
    std::uint64_t next_row_number_{1U};
    std::shared_ptr<DataGridViewCellValueProvider> provider_{};
    // Virtual rows: the application's order of row IDs (owner decision W-5),
    // the window source window mode asks, the policy and its thresholds, the
    // changes reported since the last layout pass and in the pass before,
    // and the reason the order last went to window mode.
    DataGridViewRowOrder virtual_order_{};
    std::shared_ptr<DataGridViewRowWindowSource> window_source_{};
    DataGridViewRowOrderPolicy row_order_policy_{DataGridViewRowOrderPolicy::automatic};
    DataGridViewRowOrderThresholds row_order_thresholds_{};
    std::size_t changes_this_pass_{};
    std::size_t changes_last_pass_{};
    DataGridViewRowOrderSwitchReason window_reason_{DataGridViewRowOrderSwitchReason::row_count};
    std::shared_ptr<BindingSource> data_source_{};
    SubscriptionToken source_list_changed_{};
    SubscriptionToken source_position_changed_{};
    bool synchronizing_position_{};
    // The identity index of whichever source holds the rows.
    RowPositions row_positions_{};

    std::shared_ptr<TextBox> editor_{};
    std::shared_ptr<VScrollBar> vertical_bar_{};
    std::shared_ptr<HScrollBar> horizontal_bar_{};
    SubscriptionToken editor_focus_{};
    // The window whose focus changes the grid follows, and the subscription.
    Window* followed_window_{};
    SubscriptionToken window_focus_{};
    SubscriptionToken vertical_scrolled_{};
    SubscriptionToken horizontal_scrolled_{};
    bool synchronizing_scroll_{};
    std::unique_ptr<ContextMenu> choices_{};
    // One command per combo box choice row, grown as columns need more and
    // reused across openings.
    std::vector<std::shared_ptr<Command>> choice_commands_{};
    SubscriptionToken choice_invoked_{};
    SubscriptionToken choices_changed_{};

    bool column_headers_visible_{true};
    bool row_headers_visible_{true};
    double column_headers_height_{23.0};
    double row_headers_width_{41.0};
    double row_height_{22.0};
    FontSpec font_{FontRole::content, 11.0, 400, false};
    bool read_only_{};
    bool allow_column_resize_{true};
    bool allow_row_resize_{true};

    // The position of the first displayed row.
    std::size_t first_row_{};
    double horizontal_offset_{};

    DataGridViewSelectionMode selection_mode_{DataGridViewSelectionMode::cell_select};
    bool multi_select_{true};
    // The current cell, the anchor of an extended selection and a button or
    // image cell held by the pointer, by row ID. Each names a row in the
    // index or is empty.
    std::optional<CellKey> current_{};
    std::optional<CellKey> anchor_{};
    std::optional<CellKey> pressed_content_{};
    // Cell-mode selections by row ID; a row with no selected cell has no
    // entry.
    SelectedCells selected_cells_{};
    // Full-row selections by row ID.
    RowIdSet selected_rows_{};
    // Visited link cells by row ID, as selected_cells_ keeps cells: sorted
    // unique column indices, and no entry for a row with none.
    SelectedCells visited_links_{};
    // Error text by row ID: one row's cells' texts by column (no entry for a
    // cell without text), and rows' own texts.
    using CellErrors = std::unordered_map<std::size_t, std::string>;
    using CellErrorRows = std::unordered_map<std::string, CellErrors, RowIdHash, std::equal_to<>>;
    using RowErrors = std::unordered_map<std::string, std::string, RowIdHash, std::equal_to<>>;
    CellErrorRows cell_errors_{};
    RowErrors row_errors_{};
    bool show_cell_errors_{true};
    bool show_row_errors_{true};
    bool show_editing_icon_{true};
    // The new row: its ID (given at construction) and the values it
    // shows, one per column; a row the user added that the current cell has
    // not left yet; and whether add_new_row is adding a record.
    DataGridViewRow new_row_{};
    std::optional<std::string> added_row_{};
    bool adding_row_{};
    // Both on by default, as in Windows Forms (owner, 2026-11-01).
    bool allow_add_rows_{true};
    bool allow_delete_rows_{true};
    // Rows' own heights by row ID, and the index of every row's height by
    // position built from them. The index is a cache: const queries rebuild
    // it when row_heights_stale_ (set by rows_changed) or when the row count
    // or default height no longer match it; set_row_height_at updates it in
    // place. The scratch list is reused by each rebuild. Single UI thread.
    using RowHeightsById = std::unordered_map<std::string, double, RowIdHash, std::equal_to<>>;
    RowHeightsById own_row_heights_{};
    mutable DataGridViewRowHeights row_heights_{};
    mutable bool row_heights_stale_{true};
    mutable std::vector<DataGridViewRowHeight> row_height_scratch_{};
    // Counts selection changes, so an operation raises selection_changed once.
    std::uint64_t selection_revision_{};
    // Counts structure_changed calls, so a paint can tell that a handler it
    // raised changed the grid.
    std::uint64_t structure_revision_{};
    // The last sort by a column, if the rows are still in its order, and
    // whether sort_compare's handlers are deciding comparisons now.
    std::optional<std::size_t> sorted_column_{};
    DataGridViewSortDirection sort_order_{DataGridViewSortDirection::none};
    bool sorting_{};
    DataGridViewAutoSizeColumnsMode auto_size_columns_mode_{DataGridViewAutoSizeColumnsMode::none};
    // Whether the cell modes' widths need measuring again: set by every
    // change to the values, rows, columns or styles, cleared by
    // apply_auto_sizes.
    bool auto_size_stale_{true};
    // The text scale the cell modes' widths were measured at.
    double auto_size_scale_{};
    DataGridViewAutoSizeRowsMode auto_size_rows_mode_{DataGridViewAutoSizeRowsMode::none};
    // Whether the all modes' heights need measuring again (set with
    // auto_size_stale_ and by rows_changed, cleared by
    // apply_row_auto_sizes), and the text scale and column widths they were
    // measured at.
    bool row_auto_size_stale_{true};
    double row_auto_size_scale_{};
    std::vector<double> row_auto_size_widths_{};
    DataGridViewClipboardCopyMode clipboard_copy_mode_{DataGridViewClipboardCopyMode::enable_with_auto_header_text};

    // Cell styles (T4 style mapping). Rows' and cells' own styles are kept
    // by row ID, as error text is: one row's cells' styles by column (no
    // entry for a cell without one), and rows' own styles.
    DataGridViewCellStyle default_cell_style_{};
    DataGridViewCellStyle rows_default_cell_style_{};
    DataGridViewCellStyle alternating_rows_default_cell_style_{};
    DataGridViewCellStyle column_headers_default_cell_style_{};
    DataGridViewCellStyle row_headers_default_cell_style_{};
    using CellStyles = std::unordered_map<std::size_t, DataGridViewCellStyle>;
    using CellStyleRows = std::unordered_map<std::string, CellStyles, RowIdHash, std::equal_to<>>;
    using RowStyles = std::unordered_map<std::string, DataGridViewCellStyle, RowIdHash, std::equal_to<>>;
    CellStyleRows cell_styles_{};
    RowStyles row_styles_{};
    bool enable_headers_visual_styles_{true};
    std::optional<Color> grid_color_{};
    std::optional<Color> background_color_{};
    BorderStyle border_style_{BorderStyle::line};
    DataGridViewCellBorderStyle cell_border_style_{DataGridViewCellBorderStyle::single};
    DataGridViewHeaderBorderStyle column_headers_border_style_{DataGridViewHeaderBorderStyle::raised};
    DataGridViewHeaderBorderStyle row_headers_border_style_{DataGridViewHeaderBorderStyle::raised};
    ItemDrawMode draw_mode_{ItemDrawMode::normal};
    // The stable ID draw_item_content's handlers borrow, kept for the whole
    // raise even when a handler replaces the rows.
    std::string paint_stable_id_{};

    EditKind edit_kind_{EditKind::none};
    DataGridViewEditMode edit_mode_{DataGridViewEditMode::edit_on_keystroke_or_f2};
    bool editing_{};
    bool cell_dirty_{};
    // A value the user committed to the current row since it became current.
    bool row_dirty_{};
    // The grid writes the editor's text; its text_changed is not the user's.
    bool writing_editor_{};
    // The leave and enter events of a move are being raised.
    bool changing_current_{};
    SubscriptionToken editor_text_changed_{};
    bool ending_edit_{};
    bool edit_invalid_{};
    bool focused_{};
    std::optional<std::size_t> hot_column_{};
    std::optional<std::size_t> pressed_column_{};
    std::optional<std::size_t> resizing_column_{};
    // Display order: display_order_[display index] is a column index and
    // display_indices_[column index] its display index; both have one entry
    // per column.
    std::vector<std::size_t> display_order_{};
    std::vector<std::size_t> display_indices_{};
    bool allow_column_order_{};
    // A header drag in progress: where the press was, whether it moved far
    // enough to be a drag, and the display index the column would take.
    double order_start_x_{};
    bool ordering_{};
    std::optional<DataGridViewDisplayIndex> order_target_{};
    double resize_start_x_{};
    double resize_start_width_{};
    // A row header edge drag: the row, by position (rows do not move while
    // the pointer is captured), where the press was and the height then.
    std::optional<std::size_t> resizing_row_{};
    double resize_start_y_{};
    double resize_start_height_{};

    Event<> selection_changed_{};
    Event<std::optional<DataGridViewCellAddress>> current_cell_changed_{};
    Event<DataGridViewCellCancelEvent&> cell_begin_edit_{};
    Event<const DataGridViewCellEvent&> cell_end_edit_{};
    Event<DataGridViewCellValidatingEvent&> cell_validating_{};
    Event<const DataGridViewCellEvent&> cell_value_changed_{};
    Event<const DataGridViewCellEvent&> cell_content_click_{};
    Event<const DataGridViewRowOrderModeChange&> row_order_mode_changed_{};
    Event<std::size_t> column_header_click_{};
    Event<std::size_t> column_width_changed_{};
    Event<std::size_t> column_display_index_changed_{};
    Event<const DataGridViewRowEvent&> row_height_changed_{};
    // Raised from const reads (cell_text, layout, semantics), as the
    // provider is asked from them; its handlers may not change the grid.
    mutable Event<DataGridViewCellFormattingEvent&> cell_formatting_{};
    Event<DataGridViewCellParsingEvent&> cell_parsing_{};
    Event<DataGridViewSortCompareEvent&> sort_compare_{};
    Event<> sorted_{};
    Event<ItemContentPaint&> draw_item_content_{};
    Event<DataGridViewDataErrorEvent&> data_error_{};
    Event<const DataGridViewCellEvent&> cell_validated_{};
    Event<bool> current_cell_dirty_state_changed_{};
    Event<DataGridViewRowCancelEvent&> row_validating_{};
    Event<const DataGridViewRowEvent&> row_validated_{};
    Event<const DataGridViewCellEvent&> cell_enter_{};
    Event<const DataGridViewCellEvent&> cell_leave_{};
    Event<const DataGridViewRowEvent&> row_enter_{};
    Event<const DataGridViewRowEvent&> row_leave_{};
    Event<DataGridViewRowValuesEvent&> default_values_needed_{};
    Event<const DataGridViewRowEvent&> user_added_row_{};
    Event<DataGridViewRowCancelEvent&> user_deleting_row_{};
    Event<const DataGridViewRowEvent&> user_deleted_row_{};
    Event<const DataGridViewRowEvent&> row_edit_cancelled_{};
    Event<const DataGridViewCellEvent&> cell_error_text_changed_{};
    Event<const DataGridViewRowEvent&> row_error_text_changed_{};
    // Raised from const reads, as cell_formatting_ is.
    mutable Event<DataGridViewCellErrorTextNeededEvent&> cell_error_text_needed_{};
    mutable Event<DataGridViewRowErrorTextNeededEvent&> row_error_text_needed_{};
};

} // namespace gui_forms
