#pragma once

#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view_row_order.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

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

// Where a cell's text sits across the cell.
enum class DataGridViewCellAlignment : std::uint8_t { start, center, end };

// The sort glyph a column header shows. The grid draws the glyph; ordering
// rows is the application's work, or sort() for unbound rows.
enum class DataGridViewSortDirection : std::uint8_t { none, ascending, descending };

// Whether a click or arrow key selects one cell or the whole row.
enum class DataGridViewSelectionMode : std::uint8_t { cell_select, full_row_select };

// Where the rows come from. Unbound rows are records the grid owns; virtual
// rows are counted by the grid and their values asked of a provider as they
// become visible; bound rows are a BindingSource's records.
enum class DataGridViewRowSource : std::uint8_t { unbound, virtual_rows, binding_source };

// One column. `name` is the column's identity: unique within the grid, not
// empty, and without '.', since a cell's stable ID is its row's ID, '.' and
// the column's name. `width` and `minimum_width` are logical units at text
// scale 1.
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
    DataGridViewCellAlignment alignment{DataGridViewCellAlignment::start};
    BindingValueKind value_kind{BindingValueKind::text};
    std::vector<std::string> items{};
    std::string button_text{};
    DataGridViewImageLayout image_layout{DataGridViewImageLayout::normal};
    std::string image_description{};
    std::string link_text{};
    bool track_visited_state{true};
    std::string data_property_name{};
    DataGridViewSortDirection sort_glyph{DataGridViewSortDirection::none};
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
    // for a width that is not finite or below the column's minimum.
    void set_column_width(std::size_t column, double width);
    void set_column_sort_glyph(std::size_t column, DataGridViewSortDirection direction);
    void set_column_read_only(std::size_t column, bool read_only);

    // -- rows ------------------------------------------------------------
    [[nodiscard]] DataGridViewRowSource row_source() const noexcept { return row_source_; }
    [[nodiscard]] std::size_t row_count() const noexcept;
    // Unbound rows grow with generated rows or are cut from the end; cutting
    // the current row makes the new last row current. Throws
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
    // position, or the new last row, becomes current.
    void remove_row_at(DataGridViewRowPosition position);
    // Removes every row of any source and returns to unbound rows.
    void clear_rows();
    // The ID of the row at a position. Throws std::out_of_range past the end.
    [[nodiscard]] DataGridViewRowId row_id(DataGridViewRowPosition position) const;
    // The position of the row with an ID, or none.
    [[nodiscard]] std::optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const;
    // Throws std::out_of_range for a cell past the rows or columns.
    [[nodiscard]] BindingValue cell_value(DataGridViewRowPosition row, std::size_t column) const;
    // The text a cell shows: its value as text, a button or link column's
    // text for an empty button or link cell, an image column's description.
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
    // first, then booleans, numbers and text) and shows the sort glyph on
    // that column alone. The current cell and the selection stay on their
    // rows. Returns false, changing nothing, for virtual or bound rows or
    // when an edit in progress does not commit.
    bool sort(std::size_t column, DataGridViewSortDirection direction);

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

    // -- appearance ------------------------------------------------------
    [[nodiscard]] bool column_headers_visible() const noexcept { return column_headers_visible_; }
    void set_column_headers_visible(bool visible);
    [[nodiscard]] bool row_headers_visible() const noexcept { return row_headers_visible_; }
    void set_row_headers_visible(bool visible);
    [[nodiscard]] double column_headers_height() const noexcept { return column_headers_height_; }
    void set_column_headers_height(double height);
    [[nodiscard]] double row_headers_width() const noexcept { return row_headers_width_; }
    void set_row_headers_width(double width);
    // Every row has this height (Windows Forms' RowTemplate.Height).
    [[nodiscard]] double row_height() const noexcept { return row_height_; }
    void set_row_height(double height);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    // A read-only grid edits no cell.
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }
    void set_read_only(bool read_only);
    [[nodiscard]] bool allow_user_to_resize_columns() const noexcept { return allow_column_resize_; }
    void set_allow_user_to_resize_columns(bool allow);

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
    // Raised when a column header is clicked, with the column's index.
    [[nodiscard]] Event<std::size_t>& column_header_click() noexcept { return column_header_click_; }
    // Raised after a column's width changes, with the column's index.
    [[nodiscard]] Event<std::size_t>& column_width_changed() noexcept { return column_width_changed_; }

    // -- Control ---------------------------------------------------------
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
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
        PartGeometry row_glyph;
        PartGeometry row_rule;
    };
    struct RowStates final {
        bool selected{};
        bool current{};
        bool read_only{};
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
    };
    struct CellStates final {
        bool selected{};
        bool current{};
        bool read_only{};
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
    [[nodiscard]] Rect row_header_bounds(DataGridViewRowPosition row, const GridLayout& layout) const noexcept;
    [[nodiscard]] Rect cell_bounds(DataGridViewRowPosition row, std::size_t column,
                                   const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t first_visible_column(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t visible_column_end(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t visible_row_end(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t fully_visible_rows(const GridLayout& layout) const noexcept;
    [[nodiscard]] std::size_t maximum_first_row(const GridLayout& layout) const noexcept;
    [[nodiscard]] double maximum_horizontal_offset(const GridLayout& layout) const noexcept;
    [[nodiscard]] Parts grid_parts(const GridLayout& layout) const;
    [[nodiscard]] RowParts row_parts(DataGridViewRowPosition row, const GridLayout& layout) const;
    [[nodiscard]] CellParts cell_parts(DataGridViewRowPosition row, std::size_t column,
                                       const GridLayout& layout) const;
    // A link cell's link within the cell's content area: its text's extent,
    // placed by the column's alignment and cut at the content area's end,
    // and its underline, one unit high just below the text's baseline.
    struct CellLink final {
        Rect text{};
        Rect underline{};
    };
    // None for another kind of cell or a link cell with no text.
    [[nodiscard]] std::optional<CellLink> cell_link(DataGridViewRowPosition row, std::size_t column,
                                                    Rect content) const;
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
    void paint_cell(Painter& painter, DataGridViewRowPosition row, std::size_t column,
                    const GridLayout& layout) const;
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
    void apply_current(std::optional<CellKey> cell);
    void clear_selection_state() noexcept;
    void select_current_alone();
    // Raises selection_changed when the selection revision moved past `before`.
    void publish_selection(std::uint64_t before);

    // Editing and activation.
    bool begin_text_edit(std::string initial_text, bool select_all);
    bool open_choices();
    void on_choice_invoked(const MenuItemInvocation& invocation);
    void on_choices_open_changed(bool open);
    void on_editor_focus(bool focused);
    void finish_edit();
    bool toggle_check(DataGridViewCellAddress cell);
    void click_content(DataGridViewCellAddress cell);
    bool activate_current();

    // Navigation.
    [[nodiscard]] bool navigation_key(KeyEvent& event);
    [[nodiscard]] bool tab_key(bool backward);

    // Semantics.
    [[nodiscard]] std::string cell_stable_id(DataGridViewRowPosition row, std::size_t column) const;
    [[nodiscard]] std::string column_stable_id(std::size_t column) const;
    [[nodiscard]] std::string row_header_prefix() const;
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
    // Counts selection changes, so an operation raises selection_changed once.
    std::uint64_t selection_revision_{};

    EditKind edit_kind_{EditKind::none};
    bool editing_{};
    bool ending_edit_{};
    bool edit_invalid_{};
    bool focused_{};
    std::optional<std::size_t> hot_column_{};
    std::optional<std::size_t> pressed_column_{};
    std::optional<std::size_t> resizing_column_{};
    double resize_start_x_{};
    double resize_start_width_{};

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
};

} // namespace gui_forms
