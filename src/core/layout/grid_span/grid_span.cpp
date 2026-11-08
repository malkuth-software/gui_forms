// Grid spans' checks (study 10.9), shared by GridPanel and the form model.

#include "gui_forms/layout/grid_span/grid_span.hpp"

namespace gui_forms {

bool grid_span_extent_valid(const GridSpan& span) noexcept {
    if (span.rows == 0U || span.columns == 0U) return false;
    // Summed in 64 bits, so the ends cannot wrap.
    const std::uint64_t row_end = std::uint64_t{span.anchor.row} + std::uint64_t{span.rows};
    const std::uint64_t column_end = std::uint64_t{span.anchor.column} + std::uint64_t{span.columns};
    return row_end <= maximum_grid_span_extent && column_end <= maximum_grid_span_extent;
}

bool grid_cell_admitted(GridCell cell) noexcept {
    return cell.row < maximum_grid_span_extent && cell.column < maximum_grid_span_extent;
}

bool grid_span_contains(const GridSpan& span, GridCell cell) noexcept {
    const bool in_rows = cell.row >= span.anchor.row && cell.row - span.anchor.row < span.rows;
    const bool in_columns = cell.column >= span.anchor.column && cell.column - span.anchor.column < span.columns;
    return in_rows && in_columns;
}

bool grid_span_interior(const GridSpan& span, GridCell cell) noexcept {
    return grid_span_contains(span, cell) && !(span.anchor == cell);
}

bool grid_spans_overlap(const GridSpan& first, const GridSpan& second) noexcept {
    const std::uint64_t first_row_end = std::uint64_t{first.anchor.row} + first.rows;
    const std::uint64_t second_row_end = std::uint64_t{second.anchor.row} + second.rows;
    const std::uint64_t first_column_end = std::uint64_t{first.anchor.column} + first.columns;
    const std::uint64_t second_column_end = std::uint64_t{second.anchor.column} + second.columns;
    const bool rows_meet = first.anchor.row < second_row_end && second.anchor.row < first_row_end;
    const bool columns_meet = first.anchor.column < second_column_end && second.anchor.column < first_column_end;
    return rows_meet && columns_meet;
}

} // namespace gui_forms
