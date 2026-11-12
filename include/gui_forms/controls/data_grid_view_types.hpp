#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace gui_forms {

// DataGridView's value types (its kinds, modes, styles and cell style),
// apart from the control so its property capabilities can name them.

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
struct GUI_FORMS_API DataGridViewCellStyle final {
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

} // namespace gui_forms
