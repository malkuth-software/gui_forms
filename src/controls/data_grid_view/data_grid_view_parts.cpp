// DataGridView layout, part geometry, painting and anatomy. Every part has
// one geometry function, used both by on_paint and by the published anatomy.

#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"

#include "../text_block/line_wrapping.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {
namespace {

// Geometry in logical units at text scale 1 (Windows Forms' 17-unit scroll
// bars and combo box buttons, 13-unit check boxes).
constexpr double scroll_bar_thickness = 17.0;
constexpr double cell_padding = 4.0;
constexpr double check_box_size = 13.0;
constexpr double check_mark_inset = 3.0;
constexpr double drop_down_width = 17.0;
constexpr double glyph_size = 7.0;
constexpr double button_inset = 2.0;
constexpr double sort_glyph_size = 8.0;
constexpr double sort_glyph_margin = 14.0;
constexpr double row_glyph_size = 8.0;
constexpr double row_glyph_margin = 4.0;
// The error glyph of a cell or row with error text.
constexpr double error_glyph_size = 11.0;
constexpr double grip_half_width = 4.0;
// Half the width of the bar that shows where a dragged column goes.
constexpr double drop_indicator_half_width = 1.0;
// Content overlapping the viewport by less than this does not scroll a bar
// into view.
constexpr double fit_tolerance = 0.5;

// A square of `side` centred in `area`, on whole units.
Rect centred_square(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect square{std::floor(area.x + (area.width - side) * 0.5),
                      std::floor(area.y + (area.height - side) * 0.5), side, side};
    return square;
}

Rect inset_rect(Rect area, double inset) noexcept {
    const Rect inner{area.x + inset, area.y + inset, std::max(0.0, area.width - inset * 2.0),
                     std::max(0.0, area.height - inset * 2.0)};
    return inner;
}

PartGeometry visible_part(Rect bounds, Rect clip) noexcept {
    const PartGeometry geometry = PartGeometry::from_bounds(Rect::intersection(bounds, clip));
    return geometry;
}

// A cell's own area, which stops short of its right and bottom grid lines.
Rect cell_inside(Rect cell) noexcept {
    const Rect result{cell.x, cell.y, std::max(0.0, cell.width - 1.0), std::max(0.0, cell.height - 1.0)};
    return result;
}

// The text's alignment across its box for a style's alignment; start when
// none is set.
PartTextAlignment text_alignment(std::optional<DataGridViewContentAlignment> alignment) noexcept {
    if (!alignment) return PartTextAlignment::start;
    switch (*alignment) {
    case DataGridViewContentAlignment::top_center:
    case DataGridViewContentAlignment::middle_center:
    case DataGridViewContentAlignment::bottom_center:
        return PartTextAlignment::center;
    case DataGridViewContentAlignment::top_end:
    case DataGridViewContentAlignment::middle_end:
    case DataGridViewContentAlignment::bottom_end:
        return PartTextAlignment::end;
    case DataGridViewContentAlignment::top_start:
    case DataGridViewContentAlignment::middle_start:
    case DataGridViewContentAlignment::bottom_start:
        return PartTextAlignment::start;
    }
    // Validated styles hold only the enumerators above.
    return PartTextAlignment::start;
}

// Where a line of text sits up and down in its content box.
enum class LinePlacement : std::uint8_t { top, middle, bottom };

LinePlacement line_placement(std::optional<DataGridViewContentAlignment> alignment) noexcept {
    if (!alignment) return LinePlacement::middle;
    switch (*alignment) {
    case DataGridViewContentAlignment::top_start:
    case DataGridViewContentAlignment::top_center:
    case DataGridViewContentAlignment::top_end:
        return LinePlacement::top;
    case DataGridViewContentAlignment::bottom_start:
    case DataGridViewContentAlignment::bottom_center:
    case DataGridViewContentAlignment::bottom_end:
        return LinePlacement::bottom;
    case DataGridViewContentAlignment::middle_start:
    case DataGridViewContentAlignment::middle_center:
    case DataGridViewContentAlignment::middle_end:
        return LinePlacement::middle;
    }
    return LinePlacement::middle;
}

// The space a style keeps around a cell's content, at `scale`: the style's
// padding, else the grid's own margins (cell_padding at the start and end).
Insets content_padding(const DataGridViewCellStyle& style, double scale) noexcept {
    if (!style.padding) {
        const Insets margins{cell_padding * scale, 0.0, cell_padding * scale, 0.0};
        return margins;
    }
    const Insets& padding = *style.padding;
    const Insets scaled{padding.left * scale, padding.top * scale, padding.right * scale, padding.bottom * scale};
    return scaled;
}

// `area` less `padding`, never negative.
Rect padded_rect(Rect area, Insets padding) noexcept {
    const Rect inner{area.x + padding.left, area.y + padding.top,
                     std::max(0.0, area.width - padding.left - padding.right),
                     std::max(0.0, area.height - padding.top - padding.bottom)};
    return inner;
}

// The box one line of text takes in `content`: the whole box in the middle
// (the content role centres the line), else a line at its top or bottom.
Rect line_box(Rect content, LinePlacement placement, double line_height) noexcept {
    if (placement == LinePlacement::middle || content.height <= line_height) return content;
    if (placement == LinePlacement::top) {
        const Rect top{content.x, content.y, content.width, line_height};
        return top;
    }
    const Rect bottom{content.x, content.y + content.height - line_height, content.width, line_height};
    return bottom;
}

// The box `line_count` lines of `line_height` take in `content`: placed as
// one line is, except that a block taller than the content starts at its
// top and is cut at its bottom.
Rect lines_box(Rect content, LinePlacement placement, double line_height, std::size_t line_count) noexcept {
    const double height = line_height * static_cast<double>(line_count);
    double top = content.y;
    if (placement == LinePlacement::middle) {
        top = content.y + std::max(0.0, (content.height - height) * 0.5);
    } else if (placement == LinePlacement::bottom) {
        top = content.y + std::max(0.0, content.height - height);
    }
    const Rect result{content.x, top, content.width, height};
    return result;
}

bool draws_vertical_lines(DataGridViewCellBorderStyle style) noexcept {
    return style == DataGridViewCellBorderStyle::single || style == DataGridViewCellBorderStyle::raised ||
           style == DataGridViewCellBorderStyle::sunken || style == DataGridViewCellBorderStyle::single_vertical ||
           style == DataGridViewCellBorderStyle::raised_vertical ||
           style == DataGridViewCellBorderStyle::sunken_vertical;
}

bool draws_horizontal_lines(DataGridViewCellBorderStyle style) noexcept {
    return style == DataGridViewCellBorderStyle::single || style == DataGridViewCellBorderStyle::raised ||
           style == DataGridViewCellBorderStyle::sunken || style == DataGridViewCellBorderStyle::single_horizontal ||
           style == DataGridViewCellBorderStyle::raised_horizontal ||
           style == DataGridViewCellBorderStyle::sunken_horizontal;
}

} // namespace

// -- automatic column sizing ----------------------------------------------------

namespace {

// The column mode a grid mode stands for.
DataGridViewAutoSizeColumnMode column_mode(DataGridViewAutoSizeColumnsMode mode) noexcept {
    switch (mode) {
    case DataGridViewAutoSizeColumnsMode::none:
        return DataGridViewAutoSizeColumnMode::none;
    case DataGridViewAutoSizeColumnsMode::all_cells_except_header:
        return DataGridViewAutoSizeColumnMode::all_cells_except_header;
    case DataGridViewAutoSizeColumnsMode::all_cells:
        return DataGridViewAutoSizeColumnMode::all_cells;
    case DataGridViewAutoSizeColumnsMode::displayed_cells_except_header:
        return DataGridViewAutoSizeColumnMode::displayed_cells_except_header;
    case DataGridViewAutoSizeColumnsMode::displayed_cells:
        return DataGridViewAutoSizeColumnMode::displayed_cells;
    case DataGridViewAutoSizeColumnsMode::column_header:
        return DataGridViewAutoSizeColumnMode::column_header;
    case DataGridViewAutoSizeColumnsMode::fill:
        return DataGridViewAutoSizeColumnMode::fill;
    }
    return DataGridViewAutoSizeColumnMode::none;
}

// Whether a mode sizes by measuring (cells or header), not by sharing.
bool measures(DataGridViewAutoSizeColumnMode mode) noexcept {
    const bool result = mode != DataGridViewAutoSizeColumnMode::not_set &&
                        mode != DataGridViewAutoSizeColumnMode::none && mode != DataGridViewAutoSizeColumnMode::fill;
    return result;
}

// Whether a measuring mode reads only the rows shown, so that a scroll
// changes it.
bool displayed_only(DataGridViewAutoSizeColumnMode mode) noexcept {
    const bool result = mode == DataGridViewAutoSizeColumnMode::displayed_cells ||
                        mode == DataGridViewAutoSizeColumnMode::displayed_cells_except_header;
    return result;
}

} // namespace

void DataGridView::prepare_write(const decltype(auto_size_columns_mode)&, DataGridViewAutoSizeColumnsMode& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewAutoSizeColumnsMode::fill)) {
        throw std::invalid_argument("DataGridView auto size columns mode is outside the enumeration");
    }
}

void DataGridView::written(const decltype(auto_size_columns_mode)&) {
    structure_changed();
    on_auto_size_columns_mode_changed();
}

DataGridViewAutoSizeColumnMode DataGridView::column_auto_size_mode(std::size_t column) const {
    require_column(column);
    const DataGridViewAutoSizeColumnMode own = columns_[column].auto_size_mode;
    if (own != DataGridViewAutoSizeColumnMode::not_set) return own;
    return column_mode(auto_size_columns_mode.read());
}

void DataGridView::set_column_auto_size_mode(std::size_t column, DataGridViewAutoSizeColumnMode mode) {
    require_mutable();
    require_column(column);
    if (static_cast<std::uint8_t>(mode) > static_cast<std::uint8_t>(DataGridViewAutoSizeColumnMode::fill)) {
        throw std::invalid_argument("DataGridView column auto size mode is outside the enumeration");
    }
    if (columns_[column].auto_size_mode == mode) return;
    columns_[column].auto_size_mode = mode;
    structure_changed();
}

void DataGridView::set_column_fill_weight(std::size_t column, double weight) {
    require_mutable();
    require_column(column);
    if (!std::isfinite(weight) || weight <= 0.0) {
        throw std::invalid_argument("DataGridView column fill weight must be finite and positive");
    }
    if (columns_[column].fill_weight == weight) return;
    columns_[column].fill_weight = weight;
    structure_changed();
}

bool DataGridView::row_resizable() const noexcept {
    const bool result = allow_user_to_resize_rows.read() && row_headers_visible.read() &&
                        auto_size_rows_mode.read() == DataGridViewAutoSizeRowsMode::none;
    return result;
}

bool DataGridView::column_resizable(std::size_t column) const {
    const bool result = allow_user_to_resize_columns.read() && column < columns_.size() &&
                        column_auto_size_mode(column) == DataGridViewAutoSizeColumnMode::none;
    return result;
}

bool DataGridView::sized_by_cells() const {
    bool sized = auto_size_rows_mode.read() != DataGridViewAutoSizeRowsMode::none;
    for (std::size_t column = 0U; column < columns_.size() && !sized; ++column) {
        const DataGridViewAutoSizeColumnMode mode = column_auto_size_mode(column);
        sized = measures(mode) && mode != DataGridViewAutoSizeColumnMode::column_header;
    }
    return sized;
}

void DataGridView::values_changed() {
    auto_size_stale_ = true;
    row_auto_size_stale_ = true;
    // A column sized by its cells may change width, and rows sized by
    // their cells height: lay the grid out again.
    if (sized_by_cells()) {
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    } else {
        invalidate(Dirty::paint | Dirty::semantics);
    }
}

// One cell's value changed, from any row source. Unless the cells size
// rows or columns (values_changed lays the grid out again), the cell region
// notes the cell: no layout, its rectangle damaged after the next layout
// (gather_content_regions), and the semantics republished.
void DataGridView::cell_changed(const CellKey& key) {
    const std::optional<DataGridViewCellAddress> placed = address_of(key);
    if (!placed || sized_by_cells()) {
        values_changed();
        return;
    }
    auto_size_stale_ = true;
    row_auto_size_stale_ = true;
    const ContentItemKey cell{static_cast<std::uint64_t>((*placed).row.value),
                              static_cast<std::uint64_t>((*placed).column)};
    cells_region_.note_item(cell, Dirty::paint | Dirty::semantics);
}

// The noted cells shown, each damaged at its final geometry within the
// cell viewport; cells scrolled out of view or since removed damage
// nothing. An unbound row's store also left the row marked replaced in the
// rows collection, which no layout has taken: it is taken here, as arrange
// would (settle_rows_damage), so it does not damage the row again later. A
// failed damage mark drops the rest and rethrows (the region is cleared
// either way, so its next note asks for a gather again).
void DataGridView::gather_content_regions() {
    const std::span<const ContentItemKey> cells = cells_region_.pending();
    if (cells.empty()) return;
    const GridLayout layout = grid_layout();
    const std::size_t row_end = visible_row_end(layout);
    try {
        settle_rows_damage(false, layout);
        for (const ContentItemKey& cell : cells) {
            const bool shown = cell.major >= first_row_ && cell.major < row_end && cell.minor < columns_.size();
            if (!shown) continue;
            const DataGridViewRowPosition row{static_cast<std::size_t>(cell.major)};
            const Rect bounds = cell_bounds(row, static_cast<std::size_t>(cell.minor), layout);
            const Rect visible = Rect::intersection(bounds, layout.data);
            if (!visible.empty()) invalidate(visible);
        }
    } catch (...) {
        cells_region_.clear_pending();
        throw;
    }
    cells_region_.clear_pending();
}

Dirty DataGridView::fold_content_regions() noexcept {
    const Dirty folded = cells_region_.fold_pending();
    return folded;
}

double DataGridView::preferred_cell_width(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    const std::uint64_t revision = structure_revision_;
    FormattedCell formatted = formatted_cell(row, column);
    // A formatting handler that changed the grid leaves no width to measure.
    if (structure_revision_ != revision) return 0.0;
    const std::string text = shown_text(row, column, std::move(formatted.text));
    const DataGridViewCellStyle& style = formatted.style;
    const double scale = effective_text_scale();
    const FontSpec face = (style.font ? effective_font(*style.font) : text_font(font.read()));
    // Text is measured in the scaled font; widths are logical units.
    const double text_width = text.empty() ? 0.0 : resolve_text_layout_utf8(text, face).logical_size.width / scale;
    const Insets padding = content_padding(style, 1.0);
    const DataGridViewColumnKind kind = columns_[column].kind;
    double content = 0.0;
    if (kind == DataGridViewColumnKind::check_box) {
        content = check_box_size;
    } else if (kind == DataGridViewColumnKind::button) {
        // The face's inset and its label's margins.
        content = text_width + button_inset * 2.0 + cell_padding;
    } else if (kind == DataGridViewColumnKind::combo_box) {
        content = text_width + drop_down_width;
    } else if (kind == DataGridViewColumnKind::image) {
        // An image's pixels are logical units (cell_image).
        const BindingValue value = stored_value(row, column);
        const ImageId* const image = std::get_if<ImageId>(&value);
        const Window* const host = attached_window();
        if (image != nullptr && (*image).value != 0U && host != nullptr) {
            const std::optional<ImageResourceView> resource = (*host).image_resources().find(*image);
            if (resource) content = static_cast<double>((*resource).metadata.width);
        }
    } else {
        // Text and link cells: their text.
        content = text_width;
    }
    if (show_cell_errors.read() && !cell_error_text(row, column).empty()) content += error_glyph_size + cell_padding;
    // The cell's grid line is inside its width.
    const double result = content + padding.left + padding.right + 1.0;
    return result;
}

double DataGridView::preferred_column_header_width(std::size_t column) const {
    require_column(column);
    const DataGridViewCellStyle style = inherited_column_header_style(column);
    const DataGridViewColumn& definition = columns_[column];
    const double scale = effective_text_scale();
    const FontSpec face = (style.font ? effective_font(*style.font) : text_font(font.read()));
    const double text_width = definition.header_text.empty()
        ? 0.0
        : resolve_text_layout_utf8(definition.header_text, face).logical_size.width / scale;
    const Insets padding = content_padding(style, 1.0);
    // A sortable column keeps the glyph's lane, as Windows Forms does.
    const bool sortable = column_sort_mode(column) != DataGridViewColumnSortMode::not_sortable ||
                          definition.sort_glyph != DataGridViewSortDirection::none;
    const double glyph_lane = sortable ? sort_glyph_margin : 0.0;
    const double result = text_width + padding.left + padding.right + glyph_lane + 1.0;
    return result;
}

std::optional<double> DataGridView::automatic_width(std::size_t column, DataGridViewAutoSizeColumnMode mode,
                                                    const GridLayout& layout) const {
    if (!measures(mode) || column >= columns_.size()) return std::nullopt;
    const std::uint64_t revision = structure_revision_;
    double widest = 0.0;
    const bool header = mode == DataGridViewAutoSizeColumnMode::column_header ||
                        mode == DataGridViewAutoSizeColumnMode::all_cells ||
                        mode == DataGridViewAutoSizeColumnMode::displayed_cells;
    if (header && (column_headers_visible.read() || mode == DataGridViewAutoSizeColumnMode::column_header)) {
        widest = preferred_column_header_width(column);
    }
    if (mode != DataGridViewAutoSizeColumnMode::column_header) {
        std::size_t begin = 0U;
        std::size_t end = row_count();
        if (displayed_only(mode)) {
            end = visible_row_end(layout);
            begin = std::min(first_row_, end);
        }
        for (std::size_t position = begin; position < end; ++position) {
            widest = std::max(widest, preferred_cell_width(DataGridViewRowPosition{position}, column));
            // A formatting handler changed the grid: the rows are not these.
            if (structure_revision_ != revision) return std::nullopt;
        }
    }
    const double result = std::max(columns_[column].minimum_width, widest);
    return result;
}

void DataGridView::apply_auto_sizes() {
    const double scale = effective_text_scale();
    if (scale != auto_size_scale_) {
        auto_size_scale_ = scale;
        auto_size_stale_ = true;
    }
    const std::uint64_t revision = structure_revision_;
    const GridLayout layout = grid_layout();
    bool fills = false;
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        const DataGridViewAutoSizeColumnMode mode = column_auto_size_mode(column);
        if (mode == DataGridViewAutoSizeColumnMode::fill) fills = true;
        // Widths from every row or the header change only with the grid's
        // content; widths from the rows shown change with every scroll.
        if (!measures(mode) || (!auto_size_stale_ && !displayed_only(mode))) continue;
        const std::optional<double> width = automatic_width(column, mode, layout);
        if (structure_revision_ != revision) return;
        if (width) columns_[column].width = *width;
    }
    auto_size_stale_ = false;
    if (fills) distribute_fill_width();
}

void DataGridView::distribute_fill_width() {
    // The fill columns start at their minimums, so that their widths
    // cannot bring in the horizontal bar that narrows the space they share.
    double fixed = 0.0;
    double weights = 0.0;
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        DataGridViewColumn& definition = columns_[column];
        if (column_auto_size_mode(column) == DataGridViewAutoSizeColumnMode::fill) {
            definition.width = definition.minimum_width;
            weights += definition.fill_weight;
        } else {
            fixed += definition.width;
        }
    }
    const GridLayout layout = grid_layout();
    double available = std::max(0.0, layout.data.width / layout.scale - fixed);
    // A column whose share is under its minimum keeps the minimum and leaves
    // the sharing; the others share what is left, until every share holds.
    std::vector<bool> settled(columns_.size(), false);
    bool changed = true;
    while (changed && weights > 0.0) {
        changed = false;
        for (std::size_t column = 0U; column < columns_.size(); ++column) {
            if (settled[column] || column_auto_size_mode(column) != DataGridViewAutoSizeColumnMode::fill) continue;
            const DataGridViewColumn& definition = columns_[column];
            const double share = available * definition.fill_weight / weights;
            if (share < definition.minimum_width) {
                settled[column] = true;
                available = std::max(0.0, available - definition.minimum_width);
                weights -= definition.fill_weight;
                changed = true;
            }
        }
    }
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        if (settled[column] || column_auto_size_mode(column) != DataGridViewAutoSizeColumnMode::fill) continue;
        DataGridViewColumn& definition = columns_[column];
        if (weights > 0.0) definition.width = available * definition.fill_weight / weights;
    }
}

void DataGridView::auto_resize_column(std::size_t column, DataGridViewAutoSizeColumnMode mode) {
    require_mutable();
    require_column(column);
    if (!measures(mode)) {
        throw std::invalid_argument("DataGridView auto_resize_column takes a cell mode or column_header");
    }
    const std::optional<double> width = automatic_width(column, mode, grid_layout());
    // A formatting handler may have changed the columns.
    if (!width || !is_alive() || column >= columns_.size() || columns_[column].width == *width) return;
    columns_[column].width = *width;
    structure_changed();
    column_width_changed_.emit(column);
}

// -- automatic row heights -----------------------------------------------------

namespace {

DataGridViewAutoSizeRowMode row_mode(DataGridViewAutoSizeRowsMode mode) noexcept {
    DataGridViewAutoSizeRowMode result = DataGridViewAutoSizeRowMode::all_cells;
    if (mode == DataGridViewAutoSizeRowsMode::all_headers || mode == DataGridViewAutoSizeRowsMode::displayed_headers) {
        result = DataGridViewAutoSizeRowMode::row_header;
    } else if (mode == DataGridViewAutoSizeRowsMode::all_cells_except_headers ||
               mode == DataGridViewAutoSizeRowsMode::displayed_cells_except_headers) {
        result = DataGridViewAutoSizeRowMode::all_cells_except_header;
    }
    return result;
}

bool displayed_rows_only(DataGridViewAutoSizeRowsMode mode) noexcept {
    const bool result = mode == DataGridViewAutoSizeRowsMode::displayed_headers ||
                        mode == DataGridViewAutoSizeRowsMode::displayed_cells_except_headers ||
                        mode == DataGridViewAutoSizeRowsMode::displayed_cells;
    return result;
}

} // namespace

double DataGridView::preferred_cell_height(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    const std::uint64_t revision = structure_revision_;
    FormattedCell formatted = formatted_cell(row, column);
    // A formatting handler that changed the grid leaves no height to measure.
    if (structure_revision_ != revision) return 0.0;
    const std::string text = shown_text(row, column, std::move(formatted.text));
    const DataGridViewCellStyle& style = formatted.style;
    const double scale = effective_text_scale();
    const FontSpec face = (style.font ? effective_font(*style.font) : text_font(font.read()));
    // Lines are measured in the scaled font; heights are logical units.
    const double line = text_line_height(face) / scale;
    const Insets padding = content_padding(style, 1.0);
    const DataGridViewColumnKind kind = columns_[column].kind;
    double content = line;
    if (kind == DataGridViewColumnKind::check_box) {
        content = check_box_size;
    } else if (kind == DataGridViewColumnKind::button) {
        // The face's inset and its label's margins.
        content = line + button_inset * 2.0 + cell_padding;
    } else if (kind == DataGridViewColumnKind::image) {
        content = 0.0;
        const BindingValue value = stored_value(row, column);
        const ImageId* const image = std::get_if<ImageId>(&value);
        const Window* const host = attached_window();
        if (image != nullptr && (*image).value != 0U && host != nullptr) {
            const std::optional<ImageResourceView> resource = (*host).image_resources().find(*image);
            if (resource) content = static_cast<double>((*resource).metadata.height);
        }
    } else if (kind == DataGridViewColumnKind::text && style.wrap_mode.value_or(false) && !text.empty()) {
        // The lines at the content's width, as cell_layout wraps them.
        double reserved = 0.0;
        if (show_cell_errors.read() && !cell_error_text(row, column).empty()) reserved = error_glyph_size + cell_padding;
        const double width = std::max(0.0, columns_[column].width - 1.0 - padding.left - padding.right - reserved);
        std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
        static_cast<void>(fill_line_break_opportunities_utf8(text, std::span<LineBreakOpportunity>(opportunities)));
        std::vector<WrappedTextLine> lines;
        const line_wrapping_detail::ControlRunWidth measure{this, face};
        line_wrapping_detail::wrap_lines_greedy(std::string_view(text),
                                                std::span<const LineBreakOpportunity>(opportunities), width * scale,
                                                measure, lines);
        content = line * static_cast<double>(std::max<std::size_t>(1U, lines.size()));
    }
    // The cell's grid line is inside its height.
    const double result = content + padding.top + padding.bottom + 1.0;
    return result;
}

double DataGridView::preferred_row_header_height(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const DataGridViewCellStyle style = inherited_row_header_style();
    const double scale = effective_text_scale();
    const FontSpec face = (style.font ? effective_font(*style.font) : text_font(font.read()));
    const Insets padding = content_padding(style, 1.0);
    const double result = text_line_height(face) / scale + padding.top + padding.bottom + 1.0;
    return result;
}

std::optional<double> DataGridView::automatic_height(std::size_t position, DataGridViewAutoSizeRowMode mode) const {
    const DataGridViewRowPosition row{position};
    const std::uint64_t revision = structure_revision_;
    double tallest = 0.0;
    const bool header = mode == DataGridViewAutoSizeRowMode::row_header ||
                        (mode == DataGridViewAutoSizeRowMode::all_cells && row_headers_visible.read());
    if (header) tallest = preferred_row_header_height(row);
    if (mode != DataGridViewAutoSizeRowMode::row_header) {
        for (std::size_t column = 0U; column < columns_.size(); ++column) {
            tallest = std::max(tallest, preferred_cell_height(row, column));
            // A formatting handler changed the grid: the rows are not these.
            if (structure_revision_ != revision) return std::nullopt;
        }
    }
    if (tallest <= 0.0) tallest = row_height.read();
    const double result = std::clamp(tallest, 4.0, 4096.0);
    return result;
}

void DataGridView::apply_row_auto_sizes() {
    if (auto_size_rows_mode.read() == DataGridViewAutoSizeRowsMode::none) return;
    const double scale = effective_text_scale();
    if (scale != row_auto_size_scale_) {
        row_auto_size_scale_ = scale;
        row_auto_size_stale_ = true;
    }
    // Wrapped cells' heights follow their columns' widths, automatic ones
    // included.
    bool widths_changed = row_auto_size_widths_.size() != columns_.size();
    for (std::size_t column = 0U; column < columns_.size() && !widths_changed; ++column) {
        widths_changed = row_auto_size_widths_[column] != columns_[column].width;
    }
    if (widths_changed) {
        row_auto_size_widths_.resize(columns_.size());
        for (std::size_t column = 0U; column < columns_.size(); ++column) {
            row_auto_size_widths_[column] = columns_[column].width;
        }
        row_auto_size_stale_ = true;
    }
    const std::size_t count = row_count();
    const DataGridViewAutoSizeRowMode each = row_mode(auto_size_rows_mode.read());
    const std::uint64_t revision = structure_revision_;
    if (!displayed_rows_only(auto_size_rows_mode.read())) {
        if (!row_auto_size_stale_ && row_heights_.count() == count && row_heights_.default_height() == row_height.read()) {
            return;
        }
        // Every row measured, and the index built from the heights at once.
        row_height_scratch_.clear();
        row_height_scratch_.reserve(count);
        for (std::size_t position = 0U; position < count; ++position) {
            const std::optional<double> height = automatic_height(position, each);
            if (structure_revision_ != revision) return;
            if (height) row_height_scratch_.push_back(DataGridViewRowHeight{position, *height});
        }
        row_heights_.reset(count, row_height.read(), row_height_scratch_);
        row_heights_stale_ = false;
        row_auto_size_stale_ = false;
        return;
    }
    // The displayed modes: from the first displayed row down, each row
    // measured before the next is placed, until the viewport is full.
    const DataGridViewRowHeights& heights = row_heights();
    const GridLayout layout = grid_layout();
    const double viewport = layout.data.height / layout.scale;
    const std::size_t first = std::min(first_row_, count);
    const double first_top = heights.top(first);
    for (std::size_t position = first; position < count && heights.top(position) - first_top < viewport; ++position) {
        const std::optional<double> height = automatic_height(position, each);
        if (structure_revision_ != revision) return;
        if (height) row_heights_.set_height(position, *height);
    }
    row_auto_size_stale_ = false;
}

void DataGridView::prepare_write(const decltype(auto_size_rows_mode)&, DataGridViewAutoSizeRowsMode& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewAutoSizeRowsMode::displayed_cells)) {
        throw std::invalid_argument("DataGridView auto size rows mode is outside the enumeration");
    }
}

void DataGridView::written(const decltype(auto_size_rows_mode)&) {
    // The index is rebuilt from the own heights, and measured again.
    rows_changed();
    on_auto_size_rows_mode_changed();
}

void DataGridView::auto_resize_row(DataGridViewRowPosition row, DataGridViewAutoSizeRowMode mode) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    if (static_cast<std::uint8_t>(mode) > static_cast<std::uint8_t>(DataGridViewAutoSizeRowMode::row_header)) {
        throw std::invalid_argument("DataGridView auto size row mode is outside the enumeration");
    }
    const std::optional<double> height = automatic_height(row.value, mode);
    // A formatting handler may have changed the rows.
    if (!height || !is_alive() || row.value >= row_count()) return;
    set_row_height_at(row, *height);
}

void DataGridView::auto_resize_rows(DataGridViewAutoSizeRowsMode mode) {
    require_mutable();
    if (mode == DataGridViewAutoSizeRowsMode::none ||
        static_cast<std::uint8_t>(mode) > static_cast<std::uint8_t>(DataGridViewAutoSizeRowsMode::displayed_cells)) {
        throw std::invalid_argument("DataGridView auto_resize_rows takes a header or cell mode");
    }
    const DataGridViewAutoSizeRowMode each = row_mode(mode);
    std::size_t begin = 0U;
    std::size_t end = row_count();
    if (displayed_rows_only(mode)) {
        const GridLayout layout = grid_layout();
        end = visible_row_end(layout);
        begin = std::min(first_row_, end);
    }
    // Read the count each time: a handler may change the rows.
    for (std::size_t position = begin; is_alive() && position < end && position < row_count(); ++position) {
        auto_resize_row(DataGridViewRowPosition{position}, each);
    }
}

void DataGridView::auto_resize_columns(DataGridViewAutoSizeColumnsMode mode) {
    require_mutable();
    const DataGridViewAutoSizeColumnMode each = column_mode(mode);
    if (!measures(each)) {
        throw std::invalid_argument("DataGridView auto_resize_columns takes a cell mode or column_header");
    }
    // Read the count each time: a handler may change the columns.
    for (std::size_t column = 0U; is_alive() && column < columns_.size(); ++column) {
        auto_resize_column(column, each);
    }
}

// -- layout ------------------------------------------------------------------

double DataGridView::content_width() const noexcept {
    double width = 0.0;
    for (const DataGridViewColumn& column : columns_) width += column.width;
    return width;
}

DataGridView::GridLayout DataGridView::grid_layout() const {
    GridLayout layout;
    const double scale = effective_text_scale();
    layout.scale = scale;
    layout.client = client_rectangle();
    layout.row_height = row_height.read() * scale;
    const double width = layout.client.width;
    const double height = layout.client.height;
    const double header_height = column_headers_visible.read() ? column_headers_height.read() * scale : 0.0;
    const double header_width = row_headers_visible.read() ? row_headers_width.read() * scale : 0.0;
    const double bar = scroll_bar_thickness * scale;
    const double content_x = content_width() * scale;
    const double content_y = row_heights().total() * scale;
    // A bar on one axis narrows the other; three passes settle both.
    bool horizontal = false;
    bool vertical = false;
    for (int pass = 0; pass < 3; ++pass) {
        const double available_width = std::max(0.0, width - header_width - (vertical ? bar : 0.0));
        const double available_height = std::max(0.0, height - header_height - (horizontal ? bar : 0.0));
        const bool next_horizontal = content_x > available_width + fit_tolerance;
        const bool next_vertical = content_y > available_height + fit_tolerance;
        if (next_horizontal == horizontal && next_vertical == vertical) break;
        horizontal = next_horizontal;
        vertical = next_vertical;
    }
    const double horizontal_bar = horizontal ? bar : 0.0;
    const double vertical_bar = vertical ? bar : 0.0;
    layout.data = {header_width, header_height, std::max(0.0, width - header_width - vertical_bar),
                   std::max(0.0, height - header_height - horizontal_bar)};
    if (column_headers_visible.read()) {
        layout.column_header_band = {header_width, 0.0, std::max(0.0, width - header_width), header_height};
    }
    if (row_headers_visible.read()) {
        layout.row_header_band = {0.0, header_height, header_width, layout.data.height};
    }
    if (column_headers_visible.read() && row_headers_visible.read()) {
        layout.top_left = {0.0, 0.0, header_width, header_height};
    }
    // The vertical bar runs beside the rows, under the column headers; the
    // horizontal bar runs the full width short of the corner.
    if (vertical) layout.vertical_bar = {width - bar, header_height, bar, layout.data.height};
    if (horizontal) layout.horizontal_bar = {0.0, height - bar, std::max(0.0, width - vertical_bar), bar};
    if (vertical && horizontal) layout.scroll_corner = {width - bar, height - bar, bar, bar};
    return layout;
}

double DataGridView::column_left(std::size_t column, const GridLayout& layout) const noexcept {
    // The columns shown before this one, in display order.
    double x = layout.data.x - horizontal_offset_ * layout.scale;
    const std::size_t display = column < display_indices_.size() ? display_indices_[column] : display_order_.size();
    for (std::size_t position = 0; position < display; ++position) {
        x += columns_[display_order_[position]].width * layout.scale;
    }
    return x;
}

Rect DataGridView::column_header_bounds(std::size_t column, const GridLayout& layout) const noexcept {
    const Rect bounds{column_left(column, layout), 0.0, columns_[column].width * layout.scale,
                      layout.column_header_band.height};
    return bounds;
}

Rect DataGridView::row_header_bounds(DataGridViewRowPosition row, const GridLayout& layout) const {
    const double height = row_heights().height(row.value) * layout.scale;
    const Rect bounds{0.0, layout.data.y + row_offset(row.value, layout), layout.row_header_band.width, height};
    return bounds;
}

Rect DataGridView::cell_bounds(DataGridViewRowPosition row, std::size_t column, const GridLayout& layout) const {
    const double height = row_heights().height(row.value) * layout.scale;
    const Rect bounds{column_left(column, layout), layout.data.y + row_offset(row.value, layout),
                      columns_[column].width * layout.scale, height};
    return bounds;
}

std::size_t DataGridView::first_visible_display(const GridLayout& layout) const noexcept {
    double x = layout.data.x - horizontal_offset_ * layout.scale;
    for (std::size_t position = 0; position < display_order_.size(); ++position) {
        x += columns_[display_order_[position]].width * layout.scale;
        if (x > layout.data.x) return position;
    }
    return display_order_.size();
}

std::size_t DataGridView::visible_display_end(const GridLayout& layout) const noexcept {
    const double right = layout.data.x + layout.data.width;
    double x = layout.data.x - horizontal_offset_ * layout.scale;
    for (std::size_t position = 0; position < display_order_.size(); ++position) {
        if (x >= right) return position;
        x += columns_[display_order_[position]].width * layout.scale;
    }
    return display_order_.size();
}

DataGridViewDisplayIndex DataGridView::order_target_at(double x, const GridLayout& layout) const {
    // The boundary the pointer is nearest: past every column whose middle
    // is before it. Taking the dragged column out of its place shifts the
    // boundaries after it by one.
    const std::size_t from = pressed_column_ && *pressed_column_ < display_indices_.size()
        ? display_indices_[*pressed_column_]
        : 0U;
    std::size_t boundary = 0U;
    for (std::size_t position = 0; position < display_order_.size(); ++position) {
        const std::size_t column = display_order_[position];
        const double middle = column_left(column, layout) + columns_[column].width * layout.scale * 0.5;
        if (middle < x) boundary = position + 1U;
    }
    std::size_t target = boundary > from ? boundary - 1U : boundary;
    if (!display_order_.empty()) target = std::min(target, display_order_.size() - 1U);
    const DataGridViewDisplayIndex result{target};
    return result;
}

// -- row heights ---------------------------------------------------------------

const DataGridViewRowHeights& DataGridView::row_heights() const {
    const std::size_t count = row_count();
    const bool current = !row_heights_stale_ && row_heights_.count() == count &&
                         row_heights_.default_height() == row_height.read();
    if (current) return row_heights_;
    // Each own height at its row's present position; rows gone are skipped
    // (drop_missing_rows erases them).
    row_height_scratch_.clear();
    for (const RowHeightsById::value_type& own : own_row_heights_) {
        const std::optional<std::size_t> position = find_row(own.first);
        if (!position || *position >= count) continue;
        const DataGridViewRowHeight given{*position, own.second};
        row_height_scratch_.push_back(given);
    }
    row_heights_.reset(count, row_height.read(), row_height_scratch_);
    row_heights_stale_ = false;
    return row_heights_;
}

void DataGridView::rows_changed() {
    row_heights_stale_ = true;
    row_auto_size_stale_ = true;
    structure_changed();
}

double DataGridView::row_offset(std::size_t position, const GridLayout& layout) const {
    const DataGridViewRowHeights& heights = row_heights();
    const std::size_t first = std::min(first_row_, heights.count());
    const double result = (heights.top(position) - heights.top(first)) * layout.scale;
    return result;
}

std::size_t DataGridView::first_row_ending_at(std::size_t last, const GridLayout& layout) const {
    const DataGridViewRowHeights& heights = row_heights();
    const double viewport = layout.data.height / layout.scale;
    // The first row must start at or below `target` for `last` to end at
    // the viewport's bottom.
    const double target = heights.top(last + 1U) - viewport;
    if (target <= 0.0) return 0U;
    std::size_t first = heights.position_at(target);
    if (first < heights.count() && heights.top(first) < target) ++first;
    const std::size_t result = std::min(first, last);
    return result;
}

std::size_t DataGridView::visible_row_end(const GridLayout& layout) const {
    const DataGridViewRowHeights& heights = row_heights();
    const std::size_t count = heights.count();
    if (first_row_ >= count || layout.data.height <= 0.0) return std::min(first_row_, count);
    // Rows whose top lies inside the viewport, the last one perhaps cut off.
    const double bottom = heights.top(first_row_) + layout.data.height / layout.scale;
    const std::size_t last = heights.position_at(bottom);
    if (last >= count) return count;
    const std::size_t end = heights.top(last) < bottom ? last + 1U : last;
    return end;
}

DataGridViewRowPosition DataGridView::displayed_row_end() const {
    const DataGridViewRowPosition end{visible_row_end(grid_layout())};
    return end;
}

std::size_t DataGridView::fully_visible_rows(const GridLayout& layout) const {
    const DataGridViewRowHeights& heights = row_heights();
    const std::size_t count = heights.count();
    if (first_row_ >= count || layout.data.height <= 0.0) return 0U;
    // The row holding the viewport's bottom is cut off, or starts there.
    const double bottom = heights.top(first_row_) + layout.data.height / layout.scale;
    const std::size_t last = heights.position_at(bottom);
    const std::size_t result = last > first_row_ ? last - first_row_ : 0U;
    return result;
}

std::size_t DataGridView::maximum_first_row(const GridLayout& layout) const {
    const std::size_t count = row_count();
    if (count == 0U) return 0U;
    const std::size_t result = first_row_ending_at(count - 1U, layout);
    return result;
}

double DataGridView::maximum_horizontal_offset(const GridLayout& layout) const noexcept {
    const double viewport = layout.data.width / layout.scale;
    const double result = std::max(0.0, content_width() - viewport);
    return result;
}

// -- part geometry -------------------------------------------------------------

DataGridView::Parts DataGridView::grid_parts(const GridLayout& layout) const {
    Parts record;
    record.region = PartGeometry::from_bounds(layout.client);
    record.well = PartGeometry::from_bounds(layout.client);
    // The background colour stands where no cell is; on_paint keeps the
    // theme's ground under the cells.
    record.well.authored_background = background_color.read();
    if (border_style.read() != BorderStyle::none) record.edge = PartGeometry::from_bounds(layout.client);
    record.column_header_band = PartGeometry::from_bounds(layout.column_header_band);
    record.row_header_band = PartGeometry::from_bounds(layout.row_header_band);
    record.top_left_header = PartGeometry::from_bounds(layout.top_left);
    record.horizontal_scroll_bar = PartGeometry::from_bounds(layout.horizontal_bar);
    record.vertical_scroll_bar = PartGeometry::from_bounds(layout.vertical_bar);
    record.scroll_corner = PartGeometry::from_bounds(layout.scroll_corner);
    // The editor covers the current cell inside its grid lines, at its row's
    // present position.
    std::optional<DataGridViewCellAddress> current{};
    if (editing_ && edit_kind_ == EditKind::text) current = current_cell();
    if (current && (*current).column < columns_.size()) {
        const Rect cell = cell_bounds((*current).row, (*current).column, layout);
        const Rect inside{cell.x, cell.y, std::max(0.0, cell.width - 1.0), std::max(0.0, cell.height - 1.0)};
        record.editor = visible_part(inside, layout.data);
    }
    record.focus = PartGeometry::from_bounds(layout.client);
    // A dragged header's destination: a bar on the boundary the column
    // would take, before the column now at its target or, moving right,
    // after it.
    if (ordering_ && order_target_ && pressed_column_ && *pressed_column_ < columns_.size() &&
        (*order_target_).value < display_order_.size()) {
        const std::size_t from = display_indices_[*pressed_column_];
        const std::size_t target = (*order_target_).value;
        const std::size_t neighbour = display_order_[target];
        double x = column_left(neighbour, layout);
        if (target > from) x += columns_[neighbour].width * layout.scale;
        const double half = drop_indicator_half_width * layout.scale;
        const Rect bar{x - half, 0.0, half * 2.0, layout.column_header_band.height};
        const Rect clip{layout.data.x, 0.0, layout.column_header_band.width, layout.column_header_band.height};
        record.column_drop_indicator = visible_part(bar, clip);
    }
    return record;
}

DataGridView::RowParts DataGridView::row_parts(DataGridViewRowPosition row, const GridLayout& layout) const {
    RowParts record;
    if (!row_headers_visible.read()) return record;
    const Rect clip = layout.row_header_band;
    const Rect header = row_header_bounds(row, layout);
    record.row_header = visible_part(header, clip);
    const double glyph_side = row_glyph_size * layout.scale;
    const double glyph_margin = row_glyph_margin * layout.scale;
    // The row glyph sits at the header's start: the current row's arrow or
    // pencil, or the new row's star.
    if (is_current_row(row) || is_new_row(row)) {
        const Rect lane{header.x + glyph_margin, header.y, glyph_side, header.height};
        record.row_glyph = visible_part(centred_square(lane, glyph_side), clip);
    }
    if (show_row_errors.read() && !row_error_text(row).empty()) {
        const double error_side = error_glyph_size * layout.scale;
        const Rect lane{header.x + std::max(0.0, header.width - glyph_margin - error_side), header.y, error_side,
                        header.height};
        record.row_error = visible_part(centred_square(lane, error_side), clip);
    }
    if (row_source_ == DataGridViewRowSource::unbound && row.value < rows.read().size() &&
        !rows.read()[row.value].header_text.empty()) {
        const double start = glyph_margin * 2.0 + glyph_side;
        const Rect label{header.x + start, header.y, std::max(0.0, header.width - start - glyph_margin),
                         std::max(0.0, header.height - 1.0)};
        record.row_header_label = visible_part(label, clip);
    }
    if (row_headers_border_style.read() != DataGridViewHeaderBorderStyle::none) {
        const Rect rule{header.x, header.y + header.height - 1.0, header.width, 1.0};
        record.row_rule = visible_part(rule, clip);
    }
    if (row_resizable()) {
        const double half = grip_half_width * layout.scale;
        const double edge = header.y + header.height;
        record.row_grip = visible_part({header.x, edge - half, header.width, half * 2.0}, clip);
    }
    // Without header visual styles the row header style's colours stand on
    // the header, its label and its glyph (owner decision T4-1).
    if (!enable_headers_visual_styles.read()) {
        const DataGridViewCellStyle style = inherited_row_header_style();
        const AuthoredColours colours = authored_colours(style, is_row_selected(row));
        record.row_header.authored_background = colours.background;
        record.row_header_label.authored_foreground = colours.foreground;
        record.row_glyph.authored_foreground = colours.foreground;
    }
    return record;
}

DataGridView::CellParts DataGridView::cell_parts(DataGridViewRowPosition row, std::size_t column,
                                                 const GridLayout& layout) const {
    const CellLayout placed = cell_layout(row, column, layout);
    return placed.parts;
}

DataGridView::CellLayout DataGridView::cell_layout(DataGridViewRowPosition row, std::size_t column,
                                                   const GridLayout& layout) const {
    CellLayout result = cell_layout(row, column, layout, false);
    return result;
}

DataGridView::CellLayout DataGridView::cell_layout(DataGridViewRowPosition row, std::size_t column,
                                                   const GridLayout& layout, bool provide_text) const {
    CellLayout result;
    // A provided cell's text is the region's: its value is not asked for and
    // cell_formatting is not raised, so its style is the inherited one. A
    // wrapped cell needs its text here, for its lines.
    if (provide_text && column < columns_.size() && columns_[column].kind == DataGridViewColumnKind::text) {
        DataGridViewCellStyle inherited = inherited_cell_style(row, column);
        if (!inherited.wrap_mode.value_or(false)) {
            result.content.style = std::move(inherited);
            result.text_provided = true;
        }
    }
    if (!result.text_provided) {
        const std::uint64_t revision = structure_revision_;
        result.content = formatted_cell(row, column);
        // A formatting handler that changed the grid leaves no parts to lay out.
        if (structure_revision_ != revision) return result;
        result.content.text = shown_text(row, column, std::move(result.content.text));
    }
    const DataGridViewCellStyle& style = result.content.style;
    CellParts& record = result.parts;
    const Rect clip = layout.data;
    const Rect cell = cell_bounds(row, column, layout);
    const double scale = layout.scale;
    const Rect inside = cell_inside(cell);
    const Insets padding_insets = content_padding(style, scale);
    result.content_box = padded_rect(inside, padding_insets);
    record.cell = visible_part(cell, clip);
    record.cell_selection = visible_part(inside, clip);
    record.cell_focus = record.cell_selection;
    const double padding = cell_padding * scale;
    const DataGridViewColumnKind kind = columns_[column].kind;
    // The error glyph takes the cell's end; text content stops before it.
    double error_reserved = 0.0;
    if (show_cell_errors.read() && !cell_error_text(row, column).empty()) {
        const double error_side = error_glyph_size * scale;
        const Rect lane{inside.x + std::max(0.0, inside.width - padding - error_side), inside.y, error_side,
                        inside.height};
        record.cell_error = visible_part(centred_square(lane, error_side), clip);
        error_reserved = error_side + padding;
    }
    if (kind == DataGridViewColumnKind::check_box) {
        const Rect well = centred_square(inside, check_box_size * scale);
        record.cell_check_well = visible_part(well, clip);
        record.cell_check_indicator = visible_part(inset_rect(well, check_mark_inset * scale), clip);
    } else if (kind == DataGridViewColumnKind::button) {
        const Rect face = inset_rect(inside, button_inset * scale);
        record.cell_button_face = visible_part(face, clip);
        record.cell_button_label = visible_part(inset_rect(face, padding * 0.5), clip);
    } else if (kind == DataGridViewColumnKind::image) {
        // The part is the image as it is seen: cut at the cell's edges.
        const std::optional<CellImage> image = cell_image(row, column, inside);
        if (image) record.cell_image = visible_part((*image).bounds, Rect::intersection(inside, clip));
    } else {
        double reserved = 0.0;
        if (kind == DataGridViewColumnKind::combo_box) {
            reserved = drop_down_width * scale;
            const Rect button{inside.x + std::max(0.0, inside.width - reserved), inside.y,
                              std::min(reserved, inside.width), inside.height};
            record.cell_drop_down = visible_part(button, clip);
            record.cell_drop_down_glyph = visible_part(centred_square(button, glyph_size * scale), clip);
        }
        const Rect padded = result.content_box;
        const Rect content{padded.x, padded.y,
                           std::max(0.0, padded.width - reserved - error_reserved), padded.height};
        const FontSpec face = (style.font ? effective_font(*style.font) : text_font(font.read()));
        const LinePlacement placement = line_placement(style.alignment);
        if (kind == DataGridViewColumnKind::text && style.wrap_mode.value_or(false)) {
            // Wrapped text (WrapMode): lines at the content's width, breaking
            // at the text engine's opportunities and at line ends; the
            // content part is the lines' box, cut to the cell.
            const std::string_view text(result.content.text);
            std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
            if (!text.empty()) {
                static_cast<void>(fill_line_break_opportunities_utf8(
                    text, std::span<LineBreakOpportunity>(opportunities)));
            }
            const line_wrapping_detail::ControlRunWidth measure{this, face};
            line_wrapping_detail::wrap_lines_greedy(text, std::span<const LineBreakOpportunity>(opportunities),
                                                    content.width, measure, result.lines);
            result.line_height = text_line_height(face);
            result.lines_box = lines_box(content, placement, result.line_height, result.lines.size());
            record.cell_content = visible_part(result.lines_box, Rect::intersection(content, clip));
        } else {
            const double line_height = placement == LinePlacement::middle ? 0.0 : text_line_height(face);
            const Rect line = line_box(content, placement, line_height);
            record.cell_content = visible_part(line, clip);
            if (kind == DataGridViewColumnKind::link) {
                const std::optional<CellLink> link =
                    cell_link(result.content.text, text_alignment(style.alignment), face, line);
                if (link) {
                    record.cell_link = visible_part((*link).text, clip);
                    record.cell_link_underline = visible_part((*link).underline, clip);
                }
            }
        }
    }
    if (draws_vertical_lines(cell_border_style.read())) {
        const Rect vertical_rule{cell.x + cell.width - 1.0, cell.y, 1.0, cell.height};
        record.cell_vertical_rule = visible_part(vertical_rule, clip);
    }
    if (draws_horizontal_lines(cell_border_style.read())) {
        const Rect horizontal_rule{cell.x, cell.y + cell.height - 1.0, cell.width, 1.0};
        record.cell_horizontal_rule = visible_part(horizontal_rule, clip);
    }
    // The inherited style's colours (owner decision T4-1): the background
    // on the selection plate, the foreground on the text a text, combo box
    // or button cell shows, and the grid colour on the lines. A link keeps
    // its link colours, and a check box its theme's.
    const AuthoredColours colours = authored_colours(style, is_cell_selected(row, column));
    record.cell_selection.authored_background = colours.background;
    if (kind != DataGridViewColumnKind::link) record.cell_content.authored_foreground = colours.foreground;
    record.cell_button_label.authored_foreground = colours.foreground;
    record.cell_vertical_rule.authored_foreground = grid_color.read();
    record.cell_horizontal_rule.authored_foreground = grid_color.read();
    return result;
}

std::optional<DataGridView::CellLink> DataGridView::cell_link(std::string_view text, PartTextAlignment alignment,
                                                              FontSpec face, Rect content) const {
    if (content.empty() || text.empty()) return std::nullopt;
    // The link is as wide as its text, measured with the font it is drawn
    // in, and no wider than the content area, which cuts it as it cuts a
    // text cell's text.
    const ResolvedTextLayout measured = resolve_text_layout_utf8(text, face);
    const double width = std::min(content.width, measured.logical_size.width);
    double x = content.x;
    if (alignment == PartTextAlignment::center) {
        x = content.x + (content.width - width) * 0.5;
    } else if (alignment == PartTextAlignment::end) {
        x = content.x + content.width - width;
    }
    // The baseline is where the content role sets a line of text: its line
    // box centred across the content area. The underline lies 1.5 units
    // below it, as LinkLabel's does.
    const double line_height = std::max(measured.logical_size.height, face.size * 1.2);
    const double ascent = std::max(face.size, line_height * 0.82);
    const double baseline = content.y + (content.height - line_height) * 0.5 + ascent;
    const CellLink result{{x, content.y, width, content.height}, {x, baseline + 1.5, width, 1.0}};
    return result;
}

std::optional<DataGridView::CellImage> DataGridView::cell_image(DataGridViewRowPosition row, std::size_t column,
                                                                Rect inside) const {
    const DataGridViewColumn& definition = columns_[column];
    if (definition.kind != DataGridViewColumnKind::image) return std::nullopt;
    const BindingValue value = stored_value(row, column);
    const ImageId* const image = std::get_if<ImageId>(&value);
    const Window* const host = attached_window();
    if (image == nullptr || (*image).value == 0U || host == nullptr || inside.empty()) return std::nullopt;
    const std::optional<ImageResourceView> resource = (*host).image_resources().find(*image);
    if (!resource) return std::nullopt;
    // An image's pixels are logical units, as in PictureBox; the grid's text
    // scale does not apply to them.
    const double width = static_cast<double>((*resource).metadata.width);
    const double height = static_cast<double>((*resource).metadata.height);
    if (width <= 0.0 || height <= 0.0) return std::nullopt;
    CellImage result;
    result.image = *image;
    if (definition.image_layout == DataGridViewImageLayout::stretch) {
        result.bounds = inside;
        return result;
    }
    double placed_width = width;
    double placed_height = height;
    if (definition.image_layout == DataGridViewImageLayout::zoom) {
        const double fit = std::min(inside.width / width, inside.height / height);
        placed_width = width * fit;
        placed_height = height * fit;
    }
    result.bounds = {inside.x + (inside.width - placed_width) * 0.5, inside.y + (inside.height - placed_height) * 0.5,
                     placed_width, placed_height};
    return result;
}

DataGridView::ColumnParts DataGridView::column_parts(std::size_t column, const GridLayout& layout) const {
    ColumnParts record;
    if (!column_headers_visible.read()) return record;
    // Column headers scroll with the cells, between the row headers and the
    // grid's right edge.
    const Rect clip{layout.data.x, 0.0, layout.column_header_band.width, layout.column_header_band.height};
    const Rect header = column_header_bounds(column, layout);
    const double scale = layout.scale;
    record.column_header = visible_part(header, clip);
    const bool sorted = columns_[column].sort_glyph != DataGridViewSortDirection::none;
    // The header style's padding (else the grid's margins) surrounds the
    // label; the sort glyph's lane is kept clear at the end.
    const DataGridViewCellStyle style = inherited_column_header_style(column);
    const Insets padding = content_padding(style, scale);
    const double glyph_lane = sorted ? sort_glyph_margin * scale : 0.0;
    const Rect padded = padded_rect(header, padding);
    const Rect labelled{padded.x, padded.y, std::max(0.0, padded.width - glyph_lane), padded.height};
    const LinePlacement placement = line_placement(style.alignment);
    const double line_height =
        placement == LinePlacement::middle ? 0.0 : text_line_height((style.font ? effective_font(*style.font) : text_font(font.read())));
    record.column_label = visible_part(line_box(labelled, placement, line_height), clip);
    if (sorted) {
        const Rect lane{header.x + header.width - sort_glyph_margin * scale, header.y,
                        sort_glyph_size * scale, header.height};
        record.sort_glyph = visible_part(centred_square(lane, sort_glyph_size * scale), clip);
    }
    const double edge = header.x + header.width;
    if (column_headers_border_style.read() != DataGridViewHeaderBorderStyle::none) {
        record.column_divider = visible_part({edge - 1.0, header.y, 1.0, header.height}, clip);
    }
    if (column_resizable(column)) {
        const double half = grip_half_width * scale;
        record.column_grip = visible_part({edge - half, header.y, half * 2.0, header.height}, clip);
    }
    // Without header visual styles the header style's colours stand on the
    // header, its label and its sort glyph (owner decision T4-1).
    if (!enable_headers_visual_styles.read()) {
        const bool selected = column_states(column).contains(AnatomyState::selected);
        const AuthoredColours colours = authored_colours(style, selected);
        record.column_header.authored_background = colours.background;
        record.column_label.authored_foreground = colours.foreground;
        record.sort_glyph.authored_foreground = colours.foreground;
    }
    return record;
}

DataGridView::AuthoredColours DataGridView::authored_colours(const DataGridViewCellStyle& style, bool selected) {
    // A selected cell or header shows the selection colours; an unset one
    // leaves the theme's selection look, not the unselected colour.
    AuthoredColours result;
    if (selected) {
        result.background = style.selection_back_color;
        result.foreground = style.selection_fore_color;
    } else {
        result.background = style.back_color;
        result.foreground = style.fore_color;
    }
    return result;
}

// -- states --------------------------------------------------------------------

namespace {

// Application-defined column states, as ObjectView declares them.
constexpr CustomStateIndex sorted_ascending_state{0U};
constexpr CustomStateIndex sorted_descending_state{1U};
// The cell system's own state: a link cell whose link is visited.
constexpr CustomStateIndex visited_state{0U};

} // namespace

AnatomyStateSet DataGridView::row_states(DataGridViewRowPosition row) const {
    AnatomyStateSet states;
    if (is_row_selected(row)) states.insert(AnatomyState::selected);
    if (is_current_row(row)) states.insert(AnatomyState::current);
    if (read_only.read() || row_read_only(row)) states.insert(AnatomyState::read_only);
    if (!row_error_text(row).empty()) states.insert(AnatomyState::invalid);
    if (resizing_row_ == row.value) states.insert(AnatomyState::captured);
    return states;
}

AnatomyStateSet DataGridView::cell_states(DataGridViewRowPosition row, std::size_t column) const {
    AnatomyStateSet states;
    const bool current = is_current_cell(row, column);
    if (is_cell_selected(row, column)) states.insert(AnatomyState::selected);
    if (current) states.insert(AnatomyState::current);
    if (cell_read_only(row, column)) states.insert(AnatomyState::read_only);
    const bool edit_invalid = current && editing_ && edit_invalid_;
    if (edit_invalid || !cell_error_text(row, column).empty()) states.insert(AnatomyState::invalid);
    const DataGridViewColumnKind kind = columns_[column].kind;
    if (kind == DataGridViewColumnKind::check_box &&
        binding_value_to_bool(stored_value(row, column)).value_or(false)) {
        states.insert(AnatomyState::checked);
    }
    const bool pressed = pressed_content_ && (*pressed_content_).column == column &&
                         (*pressed_content_).row_id == row_id_text(row);
    if (pressed) states.insert(AnatomyState::pressed);
    if (kind == DataGridViewColumnKind::link && is_link_visited(row_id_text(row), column)) {
        states.insert(visited_state);
    }
    if (current && edit_kind_ == EditKind::choice) states.insert(AnatomyState::open);
    const Window* const host = attached_window();
    const bool cue = host != nullptr && (*host).focus_cue_visible();
    if (current && focused_ && cue && !editing_) states.insert(AnatomyState::focus_visible);
    return states;
}

AnatomyStateSet DataGridView::column_states(std::size_t column) const {
    AnatomyStateSet states;
    if (hot_column_ == column) states.insert(AnatomyState::hot);
    if (pressed_column_ == column) states.insert(AnatomyState::pressed);
    if (resizing_column_ == column) states.insert(AnatomyState::captured);
    if (columns_[column].sort_glyph == DataGridViewSortDirection::ascending) {
        states.insert(sorted_ascending_state);
    } else if (columns_[column].sort_glyph == DataGridViewSortDirection::descending) {
        states.insert(sorted_descending_state);
    }
    return states;
}

AnatomyStateSet DataGridView::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (read_only.read()) states.insert(AnatomyState::read_only);
    return states;
}

// -- hit testing ---------------------------------------------------------------

DataGridView::HitResult DataGridView::hit_test(Point local) const {
    const GridLayout layout = grid_layout();
    HitResult result;
    if (!layout.top_left.empty() && layout.top_left.contains(local)) {
        result.zone = HitZone::top_left;
        return result;
    }
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t display_end = visible_display_end(layout);
    if (!layout.column_header_band.empty() && layout.column_header_band.contains(local)) {
        for (std::size_t display = first_display; display < display_end; ++display) {
            const std::size_t column = display_order_[display];
            const ColumnParts placed = column_parts(column, layout);
            if (placed.column_grip.present && placed.column_grip.bounds.contains(local)) {
                result.zone = HitZone::column_divider;
                result.column = column;
                return result;
            }
        }
        for (std::size_t display = first_display; display < display_end; ++display) {
            const std::size_t column = display_order_[display];
            const ColumnParts placed = column_parts(column, layout);
            if (placed.column_header.present && placed.column_header.bounds.contains(local)) {
                result.zone = HitZone::column_header;
                result.column = column;
                return result;
            }
        }
        return result;
    }
    // The row under the point, by offset from the first displayed row's top.
    const std::size_t row_end = visible_row_end(layout);
    const DataGridViewRowHeights& heights = row_heights();
    const std::size_t first_row = std::min(first_row_, heights.count());
    const double offset = heights.top(first_row) + std::max(0.0, local.y - layout.data.y) / layout.scale;
    const DataGridViewRowPosition row{std::max(first_row, heights.position_at(offset))};
    if (!layout.row_header_band.empty() && layout.row_header_band.contains(local)) {
        // A row's grip reaches half into the row below, so the row above
        // the one under the point is tried as well.
        const std::size_t above = row.value > first_row ? row.value - 1U : row.value;
        for (std::size_t position = above; position <= row.value && position < row_end; ++position) {
            const RowParts placed = row_parts(DataGridViewRowPosition{position}, layout);
            if (placed.row_grip.present && placed.row_grip.bounds.contains(local)) {
                result.zone = HitZone::row_divider;
                result.row = DataGridViewRowPosition{position};
                return result;
            }
        }
        if (row.value < row_end) {
            result.zone = HitZone::row_header;
            result.row = row;
        }
        return result;
    }
    if (!layout.data.contains(local)) return result;
    if (row.value >= row_end) return result;
    for (std::size_t display = first_display; display < display_end; ++display) {
        const std::size_t column = display_order_[display];
        if (cell_bounds(row, column, layout).contains(local)) {
            result.zone = HitZone::cell;
            result.row = row;
            result.column = column;
            return result;
        }
    }
    return result;
}

// -- paint ---------------------------------------------------------------------

void DataGridView::paint_row(Painter& painter, DataGridViewRowPosition row, const GridLayout& layout) const {
    const RowParts parts = row_parts(row, layout);
    const AnatomyStateSet states = row_states(row);
    const DataGridViewCellStyle style = inherited_row_header_style();
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.states = states;
    part.font = (style.font ? effective_font(*style.font) : text_font(font.read()));
    part.role = PartRole::face;
    part.bounds = parts.row_header.bounds;
    part.authored_background = parts.row_header.authored_background;
    paint_part(painter, *this, part);
    part.authored_background.reset();
    if (parts.row_glyph.present) {
        // Owner decision B1-3: the arrow, the pencil and the star are glyph
        // marks, their look the theme's (T4).
        PartMark mark = PartMark::asterisk;
        if (is_current_row(row)) {
            mark = show_editing_icon.read() && is_current_row_dirty() ? PartMark::pencil : PartMark::arrow_right;
        }
        part.role = PartRole::glyph;
        part.bounds = parts.row_glyph.bounds;
        part.mark = mark;
        part.authored_foreground = parts.row_glyph.authored_foreground;
        paint_part(painter, *this, part);
        part.authored_foreground.reset();
        part.mark = PartMark::none;
    }
    if (parts.row_error.present) {
        part.role = PartRole::glyph;
        part.bounds = parts.row_error.bounds;
        part.mark = PartMark::error;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (parts.row_header_label.present) {
        part.role = PartRole::label;
        part.bounds = parts.row_header_label.bounds;
        part.text = rows.read()[row.value].header_text;
        part.alignment = text_alignment(style.alignment);
        part.authored_foreground = parts.row_header_label.authored_foreground;
        paint_part(painter, *this, part);
        part.authored_foreground.reset();
        part.text = {};
        part.alignment = PartTextAlignment::start;
    }
    if (parts.row_rule.present) {
        part.role = PartRole::divider;
        part.bounds = parts.row_rule.bounds;
        paint_part(painter, *this, part);
    }
}

namespace {

// A part, a rectangle, moved by `offset` (the cell region's origin).
[[nodiscard]] PartGeometry moved_part(PartGeometry part, Point offset) noexcept {
    part.bounds.x += offset.x;
    part.bounds.y += offset.y;
    return part;
}

[[nodiscard]] Rect moved_rect(Rect rect, Point offset) noexcept {
    const Rect result{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
    return result;
}

} // namespace

bool DataGridView::resolve_cell(DataGridViewRowPosition row, std::size_t column, const GridLayout& layout,
                                Point offset, CellTextSource source, DataGridViewCellsSnapshot& into,
                                DataGridViewCellsCell& resolved) {
    const std::uint64_t revision = structure_revision_;
    const CellLayout placed = cell_layout(row, column, layout, source.provided);
    if (structure_revision_ != revision) return false;
    const CellParts& parts = placed.parts;
    resolved = DataGridViewCellsCell{};
    DataGridViewCellsParts& out = resolved.parts;
    out.cell = moved_part(parts.cell, offset);
    if (!parts.cell.present) return true;
    out.selection = moved_part(parts.cell_selection, offset);
    out.content = moved_part(parts.cell_content, offset);
    out.link = moved_part(parts.cell_link, offset);
    out.link_underline = moved_part(parts.cell_link_underline, offset);
    out.check_well = moved_part(parts.cell_check_well, offset);
    out.check_indicator = moved_part(parts.cell_check_indicator, offset);
    out.button_face = moved_part(parts.cell_button_face, offset);
    out.button_label = moved_part(parts.cell_button_label, offset);
    out.drop_down = moved_part(parts.cell_drop_down, offset);
    out.drop_down_glyph = moved_part(parts.cell_drop_down_glyph, offset);
    out.vertical_rule = moved_part(parts.cell_vertical_rule, offset);
    out.horizontal_rule = moved_part(parts.cell_horizontal_rule, offset);
    out.focus = moved_part(parts.cell_focus, offset);
    out.error = moved_part(parts.cell_error, offset);
    const DataGridViewCellStyle& style = placed.content.style;
    resolved.states = cell_states(row, column);
    resolved.visited = resolved.states.contains(visited_state);
    resolved.editing_text = editing_ && edit_kind_ == EditKind::text && is_current_cell(row, column);
    resolved.font = (style.font ? effective_font(*style.font) : text_font(font.read()));
    resolved.alignment = text_alignment(style.alignment);
    const std::size_t text_begin = into.text.size();
    into.text.append(placed.content.text);
    resolved.text = DataGridViewCellsTextRange{text_begin, into.text.size()};
    // Wrapped lines are byte ranges of the cell's text; in the snapshot they
    // are ranges of its shared buffer.
    resolved.lines.first = into.lines.size();
    resolved.lines.count = placed.lines.size();
    for (const WrappedTextLine& line : placed.lines) {
        into.lines.push_back(DataGridViewCellsTextRange{text_begin + line.begin_byte, text_begin + line.end_byte});
    }
    // A provided cell's empty range is filled on the region's thread.
    if (placed.text_provided) {
        resolved.provided = DataGridViewCellsProvided{.provided = true, .row = source.row, .column = column};
    }
    resolved.lines_box = moved_rect(placed.lines_box, offset);
    resolved.line_height = placed.line_height;
    resolved.content_box = moved_rect(placed.content_box, offset);
    if (parts.cell_image.present) {
        // Drawn whole at its own bounds and cut at the cell's inside.
        const Rect inside = cell_inside(cell_bounds(row, column, layout));
        const std::optional<CellImage> image = cell_image(row, column, inside);
        if (image) {
            out.image = PartGeometry::from_bounds(moved_rect((*image).bounds, offset));
            resolved.image = (*image).image;
            resolved.image_clip = moved_rect(inside, offset);
        }
    }
    return true;
}

bool DataGridView::paint_cell(Painter& painter, DataGridViewRowPosition row, std::size_t column,
                              const GridLayout& layout) {
    const std::uint64_t revision = structure_revision_;
    paint_scratch_.text.clear();
    paint_scratch_.lines.clear();
    DataGridViewCellsCell resolved{};
    if (!resolve_cell(row, column, layout, Point{}, CellTextSource{}, paint_scratch_, resolved)) return false;
    if (!resolved.parts.cell.present) return true;
    paint_data_grid_view_cell_selection(painter, *this, resolved);
    // Owner drawing (owner decision W-Q6) replaces the content parts unless
    // a handler asks for them.
    bool default_content = true;
    if (draw_mode.read() == ItemDrawMode::owner_draw) {
        paint_stable_id_ = cell_stable_id(row, column);
        ItemContentPaint request{.painter = painter,
                                 .index = row.value,
                                 .column = column,
                                 .stable_id = paint_stable_id_,
                                 .content = Rect::intersection(resolved.content_box, layout.data),
                                 .states = resolved.states,
                                 .font = resolved.font,
                                 .text = item_content_text(*this, ControlVisualRole::editor, resolved.states)};
        default_content = raise_item_content_paint(draw_item_content_, request);
        if (!is_alive() || structure_revision_ != revision) return false;
    }
    paint_data_grid_view_cell_parts(painter, *this, paint_scratch_, resolved, layout.data, default_content);
    return true;
}

void DataGridView::paint_column_header(Painter& painter, std::size_t column, const GridLayout& layout) const {
    const ColumnParts parts = column_parts(column, layout);
    const DataGridViewCellStyle style = inherited_column_header_style(column);
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.states = column_states(column);
    part.font = (style.font ? effective_font(*style.font) : text_font(font.read()));
    part.role = PartRole::face;
    part.bounds = parts.column_header.bounds;
    part.authored_background = parts.column_header.authored_background;
    paint_part(painter, *this, part);
    part.authored_background.reset();
    if (parts.column_label.present) {
        const DataGridViewColumn& definition = columns_[column];
        part.role = PartRole::label;
        part.bounds = parts.column_label.bounds;
        part.text = definition.header_text;
        part.alignment = text_alignment(style.alignment);
        part.authored_foreground = parts.column_label.authored_foreground;
        paint_part(painter, *this, part);
        part.authored_foreground.reset();
        part.text = {};
        part.alignment = PartTextAlignment::start;
    }
    if (parts.sort_glyph.present) {
        part.role = PartRole::glyph;
        part.bounds = parts.sort_glyph.bounds;
        part.mark = columns_[column].sort_glyph == DataGridViewSortDirection::ascending ? PartMark::arrow_up
                                                                                        : PartMark::arrow_down;
        part.authored_foreground = parts.sort_glyph.authored_foreground;
        paint_part(painter, *this, part);
        part.authored_foreground.reset();
        part.mark = PartMark::none;
    }
    // The grip is the divider's resize hit region; the divider is its visible
    // form, so the grip is published but not drawn.
    if (parts.column_divider.present) {
        part.role = PartRole::divider;
        part.bounds = parts.column_divider.bounds;
        paint_part(painter, *this, part);
    }
}

void DataGridView::on_paint(Painter& painter, Rect) {
    const GridLayout layout = grid_layout();
    const Parts grid = grid_parts(layout);
    PartPaint surface;
    surface.family = ControlVisualRole::editor;
    surface.role = PartRole::well;
    surface.bounds = grid.well.bounds;
    surface.authored_background = grid.well.authored_background;
    paint_part(painter, *this, surface);
    surface.authored_background.reset();
    const std::size_t row_end = visible_row_end(layout);
    const std::size_t first_row = std::min(first_row_, row_end);
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t display_end = visible_display_end(layout);
    if (grid.well.authored_background && first_row < row_end && first_display < display_end) {
        // Under the cells the well keeps the theme's ground, as Windows
        // Forms' cells keep their own back colour over BackgroundColor: the
        // same well painted again, cut to the cells' extent, so that its
        // border stays where it was.
        const Rect last =
            cell_bounds(DataGridViewRowPosition{row_end - 1U}, display_order_[display_end - 1U], layout);
        const Rect extent{layout.data.x, layout.data.y, last.x + last.width - layout.data.x,
                          last.y + last.height - layout.data.y};
        painter.save();
        painter.clip_rect(Rect::intersection(extent, layout.data));
        paint_part(painter, *this, surface);
        painter.restore();
    }
    if (!layout.data.empty() && cells_on_region()) {
        // The cell region paints the cells, off this thread (stage S7).
        if (!send_cells_snapshot(layout, grid)) return;
    } else if (!layout.data.empty()) {
        painter.save();
        painter.clip_rect(layout.data);
        bool unchanged = true;
        for (std::size_t position = first_row; unchanged && position < row_end; ++position) {
            const DataGridViewRowPosition row{position};
            for (std::size_t display = first_display; unchanged && display < display_end; ++display) {
                unchanged = paint_cell(painter, row, display_order_[display], layout);
            }
        }
        painter.restore();
        // A handler changed the grid: the change invalidated it, and the
        // next paint draws it whole.
        if (!unchanged) return;
    }
    surface.family = ControlVisualRole::panel;
    surface.role = PartRole::face;
    if (grid.column_header_band.present) {
        surface.bounds = grid.column_header_band.bounds;
        paint_part(painter, *this, surface);
        painter.save();
        painter.clip_rect({layout.data.x, 0.0, layout.column_header_band.width, layout.column_header_band.height});
        for (std::size_t display = first_display; display < display_end; ++display) {
            paint_column_header(painter, display_order_[display], layout);
        }
        if (grid.column_drop_indicator.present) {
            surface.role = PartRole::drop;
            surface.bounds = grid.column_drop_indicator.bounds;
            paint_part(painter, *this, surface);
            surface.role = PartRole::face;
        }
        painter.restore();
    }
    if (grid.row_header_band.present) {
        surface.bounds = grid.row_header_band.bounds;
        paint_part(painter, *this, surface);
        painter.save();
        painter.clip_rect(layout.row_header_band);
        for (std::size_t position = first_row; position < row_end; ++position) {
            paint_row(painter, DataGridViewRowPosition{position}, layout);
        }
        painter.restore();
    }
    if (grid.top_left_header.present) {
        surface.bounds = grid.top_left_header.bounds;
        paint_part(painter, *this, surface);
    }
    if (grid.scroll_corner.present) {
        surface.bounds = grid.scroll_corner.bounds;
        paint_part(painter, *this, surface);
    }
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<PartField<DataGridView::Parts>, 12> grid_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &DataGridView::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &DataGridView::Parts::well},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &DataGridView::Parts::edge},
    {.declaration = {.name = "column_header_band", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::Parts::column_header_band},
    {.declaration = {.name = "row_header_band", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::Parts::row_header_band},
    {.declaration = {.name = "top_left_header", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::Parts::top_left_header},
    {.declaration = {.name = "horizontal_scroll_bar", .role = PartRole::track, .composed_from = "h_scroll_bar"},
     .member = &DataGridView::Parts::horizontal_scroll_bar},
    {.declaration = {.name = "vertical_scroll_bar", .role = PartRole::track, .composed_from = "v_scroll_bar"},
     .member = &DataGridView::Parts::vertical_scroll_bar},
    {.declaration = {.name = "scroll_corner", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::Parts::scroll_corner},
    {.declaration = {.name = "editor", .role = PartRole::well, .composed_from = "text_box"},
     .member = &DataGridView::Parts::editor},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DataGridView::Parts::focus},
    {.declaration = {.name = "column_drop_indicator", .role = PartRole::drop, .composed_from = {}},
     .member = &DataGridView::Parts::column_drop_indicator},
}};

constexpr std::array<StateField<DataGridView::States>, 6> grid_state_fields{{
    {.state = AnatomyState::normal, .member = &DataGridView::States::normal},
    {.state = AnatomyState::enabled, .member = &DataGridView::States::enabled},
    {.state = AnatomyState::disabled, .member = &DataGridView::States::disabled},
    {.state = AnatomyState::focused, .member = &DataGridView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &DataGridView::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &DataGridView::States::read_only},
}};

constexpr std::array<PartField<DataGridView::RowParts>, 6> row_part_fields{{
    {.declaration = {.name = "row_header", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::RowParts::row_header},
    {.declaration = {.name = "row_header_label", .role = PartRole::label, .composed_from = {}},
     .member = &DataGridView::RowParts::row_header_label},
    {.declaration = {.name = "row_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::RowParts::row_glyph},
    {.declaration = {.name = "row_error", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::RowParts::row_error},
    {.declaration = {.name = "row_rule", .role = PartRole::divider, .composed_from = {}},
     .member = &DataGridView::RowParts::row_rule},
    {.declaration = {.name = "row_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &DataGridView::RowParts::row_grip},
}};

constexpr std::array<StateField<DataGridView::RowStates>, 5> row_state_fields{{
    {.state = AnatomyState::selected, .member = &DataGridView::RowStates::selected},
    {.state = AnatomyState::current, .member = &DataGridView::RowStates::current},
    {.state = AnatomyState::read_only, .member = &DataGridView::RowStates::read_only},
    {.state = AnatomyState::invalid, .member = &DataGridView::RowStates::invalid},
    {.state = AnatomyState::captured, .member = &DataGridView::RowStates::captured},
}};

constexpr std::array<PartField<DataGridView::CellParts>, 16> cell_part_fields{{
    {.declaration = {.name = "cell", .role = PartRole::region, .composed_from = {}},
     .member = &DataGridView::CellParts::cell},
    {.declaration = {.name = "cell_selection", .role = PartRole::selection, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_selection},
    {.declaration = {.name = "cell_content", .role = PartRole::content, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_content},
    {.declaration = {.name = "cell_image", .role = PartRole::image, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_image},
    {.declaration = {.name = "cell_link", .role = PartRole::content, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_link},
    {.declaration = {.name = "cell_link_underline", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_link_underline},
    {.declaration = {.name = "cell_check_well", .role = PartRole::well, .composed_from = "check_box"},
     .member = &DataGridView::CellParts::cell_check_well},
    {.declaration = {.name = "cell_check_indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_check_indicator},
    {.declaration = {.name = "cell_button_face", .role = PartRole::face, .composed_from = "button"},
     .member = &DataGridView::CellParts::cell_button_face},
    {.declaration = {.name = "cell_button_label", .role = PartRole::label, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_button_label},
    {.declaration = {.name = "cell_drop_down", .role = PartRole::actuator, .composed_from = "button"},
     .member = &DataGridView::CellParts::cell_drop_down},
    {.declaration = {.name = "cell_drop_down_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_drop_down_glyph},
    {.declaration = {.name = "cell_vertical_rule", .role = PartRole::divider, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_vertical_rule},
    {.declaration = {.name = "cell_horizontal_rule", .role = PartRole::divider, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_horizontal_rule},
    {.declaration = {.name = "cell_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_focus},
    {.declaration = {.name = "cell_error", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::CellParts::cell_error},
}};

constexpr std::array<StateField<DataGridView::CellStates>, 8> cell_state_fields{{
    {.state = AnatomyState::selected, .member = &DataGridView::CellStates::selected},
    {.state = AnatomyState::current, .member = &DataGridView::CellStates::current},
    {.state = AnatomyState::read_only, .member = &DataGridView::CellStates::read_only},
    {.state = AnatomyState::invalid, .member = &DataGridView::CellStates::invalid},
    {.state = AnatomyState::checked, .member = &DataGridView::CellStates::checked},
    {.state = AnatomyState::pressed, .member = &DataGridView::CellStates::pressed},
    {.state = AnatomyState::open, .member = &DataGridView::CellStates::open},
    {.state = AnatomyState::focus_visible, .member = &DataGridView::CellStates::focus_visible},
}};

constexpr std::array<PartField<DataGridView::ColumnParts>, 5> column_part_fields{{
    {.declaration = {.name = "column_header", .role = PartRole::face, .composed_from = {}},
     .member = &DataGridView::ColumnParts::column_header},
    {.declaration = {.name = "column_label", .role = PartRole::label, .composed_from = {}},
     .member = &DataGridView::ColumnParts::column_label},
    {.declaration = {.name = "sort_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &DataGridView::ColumnParts::sort_glyph},
    {.declaration = {.name = "column_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &DataGridView::ColumnParts::column_divider},
    {.declaration = {.name = "column_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &DataGridView::ColumnParts::column_grip},
}};

constexpr std::array<StateField<DataGridView::ColumnStates>, 3> column_state_fields{{
    {.state = AnatomyState::hot, .member = &DataGridView::ColumnStates::hot},
    {.state = AnatomyState::pressed, .member = &DataGridView::ColumnStates::pressed},
    {.state = AnatomyState::captured, .member = &DataGridView::ColumnStates::captured},
}};

// "visited" has no counterpart in the design language's state set, so a
// link cell declares it as the grid's own, as LinkLabel does.
constexpr std::array<CustomStateDeclaration, 1> cell_custom_states{{
    {.name = "visited"},
}};

constexpr std::array<CustomStateDeclaration, 2> column_custom_states{{
    {.name = "sorted_ascending"},
    {.name = "sorted_descending"},
}};

constexpr std::array<PartDeclaration, 12> grid_parts_declared = part_declarations(grid_part_fields);
constexpr std::array<AnatomyState, 6> grid_states_declared = state_declarations(grid_state_fields);
constexpr std::array<PartDeclaration, 6> row_parts_declared = part_declarations(row_part_fields);
constexpr std::array<AnatomyState, 5> row_states_declared = state_declarations(row_state_fields);
constexpr std::array<PartDeclaration, 16> cell_parts_declared = part_declarations(cell_part_fields);
constexpr std::array<AnatomyState, 8> cell_states_declared = state_declarations(cell_state_fields);
constexpr std::array<PartDeclaration, 5> column_parts_declared = part_declarations(column_part_fields);
constexpr std::array<AnatomyState, 3> column_states_declared = state_declarations(column_state_fields);

constexpr ItemSystemIndex row_system{0U};
constexpr ItemSystemIndex cell_system{1U};
constexpr ItemSystemIndex column_system{2U};

constexpr std::array<ItemSystemDeclaration, 3> grid_item_systems{{
    {.name = "row", .parts = row_parts_declared, .states = row_states_declared, .custom_states = {}},
    {.name = "cell", .parts = cell_parts_declared, .states = cell_states_declared,
     .custom_states = cell_custom_states},
    {.name = "column", .parts = column_parts_declared, .states = column_states_declared,
     .custom_states = column_custom_states},
}};

constexpr ControlAnatomy data_grid_view_anatomy{
    .control_id = "data_grid_view",
    .parts = grid_parts_declared,
    .states = grid_states_declared,
    .custom_states = {},
    .item_systems = grid_item_systems,
};

} // namespace

const ControlAnatomy& DataGridView::anatomy() const noexcept {
    return data_grid_view_anatomy;
}

void DataGridView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = grid_parts(grid_layout());
    write_parts(anatomy(), grid_part_fields, record, geometry);
}

DataGridView::Parts DataGridView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), grid_part_fields, published.parts(), record);
    return record;
}

DataGridView::States DataGridView::states() const {
    States record;
    read_states(grid_state_fields, anatomy_states(), record);
    return record;
}

// Realized items, in paint order: the visible rows, then their visible cells
// row by row, then the visible column headers.
std::size_t DataGridView::realized_row_count(const GridLayout& layout) const noexcept {
    const std::size_t end = visible_row_end(layout);
    const std::size_t result = end > first_row_ ? end - first_row_ : 0U;
    return result;
}

std::size_t DataGridView::realized_column_count(const GridLayout& layout) const noexcept {
    const std::size_t first = first_visible_display(layout);
    const std::size_t end = visible_display_end(layout);
    const std::size_t result = end > first ? end - first : 0U;
    return result;
}

std::size_t DataGridView::realized_item_count() const {
    const GridLayout layout = grid_layout();
    const std::size_t rows = realized_row_count(layout);
    const std::size_t columns = realized_column_count(layout);
    const std::size_t headers = column_headers_visible.read() ? columns : 0U;
    const std::size_t result = rows + rows * columns + headers;
    return result;
}

DataGridView::RealizedSlot DataGridView::realized_slot(const GridLayout& layout, std::size_t item) const {
    const std::size_t rows = realized_row_count(layout);
    const std::size_t columns = realized_column_count(layout);
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t cells = rows * columns;
    RealizedSlot slot;
    if (item < rows) {
        slot.system = row_system;
        slot.row = DataGridViewRowPosition{first_row_ + item};
        return slot;
    }
    if (item < rows + cells) {
        // A nonzero cell count has columns: cells run row by row.
        const std::size_t position = item - rows;
        slot.system = cell_system;
        slot.row = DataGridViewRowPosition{first_row_ + position / columns};
        slot.column = display_order_[first_display + position % columns];
        return slot;
    }
    slot.system = column_system;
    slot.column = display_order_[first_display + (item - rows - cells)];
    return slot;
}

ItemSystemIndex DataGridView::publish_realized_item_system(std::size_t item) const {
    const RealizedSlot slot = realized_slot(grid_layout(), item);
    return slot.system;
}

std::string DataGridView::realized_item_stable_id(std::size_t item) const {
    const RealizedSlot slot = realized_slot(grid_layout(), item);
    if (slot.system == row_system) return row_id_text(slot.row);
    if (slot.system == cell_system) return cell_stable_id(slot.row, slot.column);
    return column_stable_id(slot.column);
}

AnatomyStateSet DataGridView::realized_item_anatomy_states(std::size_t item) const {
    const RealizedSlot slot = realized_slot(grid_layout(), item);
    if (slot.system == row_system) return row_states(slot.row);
    if (slot.system == cell_system) return cell_states(slot.row, slot.column);
    return column_states(slot.column);
}

void DataGridView::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const GridLayout layout = grid_layout();
    const RealizedSlot slot = realized_slot(layout, item);
    const ItemSystemDeclaration& system = anatomy().item_systems[slot.system.value];
    if (slot.system == row_system) {
        const RowParts record = row_parts(slot.row, layout);
        write_item_parts(system, row_part_fields, record, geometry);
    } else if (slot.system == cell_system) {
        const CellParts record = cell_parts(slot.row, slot.column, layout);
        write_item_parts(system, cell_part_fields, record, geometry);
    } else {
        const ColumnParts record = column_parts(slot.column, layout);
        write_item_parts(system, column_part_fields, record, geometry);
    }
}

std::vector<DataGridView::Row> DataGridView::realized_rows() const {
    const GridLayout layout = grid_layout();
    const std::size_t count = realized_row_count(layout);
    std::vector<Row> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Row record;
        record.position = DataGridViewRowPosition{first_row_ + item};
        record.stable_id = row_id_text(record.position);
        read_item_parts(anatomy().item_systems[row_system.value], row_part_fields, published.parts(), record.parts);
        read_states(row_state_fields, row_states(record.position), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<DataGridView::Cell> DataGridView::realized_cells() const {
    const GridLayout layout = grid_layout();
    const std::size_t rows = realized_row_count(layout);
    const std::size_t columns = realized_column_count(layout);
    const std::size_t first_display = first_visible_display(layout);
    std::vector<Cell> records;
    records.reserve(rows * columns);
    for (std::size_t position = 0; position < rows * columns; ++position) {
        const PartGeometryBuffer published = published_realized_item_geometry(rows + position);
        Cell record;
        record.address = {DataGridViewRowPosition{first_row_ + position / columns},
                          display_order_[first_display + position % columns]};
        record.stable_id = cell_stable_id(record.address.row, record.address.column);
        read_item_parts(anatomy().item_systems[cell_system.value], cell_part_fields, published.parts(), record.parts);
        const AnatomyStateSet states = cell_states(record.address.row, record.address.column);
        read_states(cell_state_fields, states, record.states);
        record.states.visited = states.contains(visited_state);
        records.push_back(record);
    }
    return records;
}

std::vector<DataGridView::Column> DataGridView::realized_columns() const {
    const GridLayout layout = grid_layout();
    if (!column_headers_visible.read()) return {};
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t count = realized_column_count(layout);
    std::vector<Column> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        Column record;
        record.index = display_order_[first_display + position];
        record.stable_id = column_stable_id(record.index);
        record.parts = column_parts(record.index, layout);
        const AnatomyStateSet states = column_states(record.index);
        read_states(column_state_fields, states, record.states);
        record.states.sorted_ascending = states.contains(sorted_ascending_state);
        record.states.sorted_descending = states.contains(sorted_descending_state);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events DataGridView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 35> data_grid_view_interactions{{
    {.name = "draw_item_content", .payload = "ItemContentPaint", .mutable_payload = true},
    {.name = "selection_changed", .payload = "", .mutable_payload = false},
    {.name = "current_cell_changed", .payload = "std::optional<DataGridViewCellAddress>", .mutable_payload = false},
    {.name = "cell_begin_edit", .payload = "DataGridViewCellCancelEvent", .mutable_payload = true},
    {.name = "cell_end_edit", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "cell_validating", .payload = "DataGridViewCellValidatingEvent", .mutable_payload = true},
    {.name = "cell_value_changed", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "cell_content_click", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "row_order_mode_changed", .payload = "DataGridViewRowOrderModeChange", .mutable_payload = false},
    {.name = "data_error", .payload = "DataGridViewDataErrorEvent", .mutable_payload = true},
    {.name = "cell_validated", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "current_cell_dirty_state_changed", .payload = "bool", .mutable_payload = false},
    {.name = "row_validating", .payload = "DataGridViewRowCancelEvent", .mutable_payload = true},
    {.name = "row_validated", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "cell_enter", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "cell_leave", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "row_enter", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "row_leave", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "default_values_needed", .payload = "DataGridViewRowValuesEvent", .mutable_payload = true},
    {.name = "user_added_row", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "user_deleting_row", .payload = "DataGridViewRowCancelEvent", .mutable_payload = true},
    {.name = "user_deleted_row", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "row_edit_cancelled", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "cell_error_text_changed", .payload = "DataGridViewCellEvent", .mutable_payload = false},
    {.name = "row_error_text_changed", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "cell_error_text_needed", .payload = "DataGridViewCellErrorTextNeededEvent", .mutable_payload = true},
    {.name = "row_error_text_needed", .payload = "DataGridViewRowErrorTextNeededEvent", .mutable_payload = true},
    {.name = "column_header_click", .payload = "std::size_t", .mutable_payload = false},
    {.name = "sort_compare", .payload = "DataGridViewSortCompareEvent", .mutable_payload = true},
    {.name = "sorted", .payload = "", .mutable_payload = false},
    {.name = "column_width_changed", .payload = "std::size_t", .mutable_payload = false},
    {.name = "column_display_index_changed", .payload = "std::size_t", .mutable_payload = false},
    {.name = "row_height_changed", .payload = "DataGridViewRowEvent", .mutable_payload = false},
    {.name = "cell_formatting", .payload = "DataGridViewCellFormattingEvent", .mutable_payload = true},
    {.name = "cell_parsing", .payload = "DataGridViewCellParsingEvent", .mutable_payload = true},
}};

} // namespace

void DataGridView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), data_grid_view_interactions.begin(),
                        data_grid_view_interactions.end());
}

} // namespace gui_forms
