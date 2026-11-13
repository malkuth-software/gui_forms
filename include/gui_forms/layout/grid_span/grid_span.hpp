#pragma once

// Grid spans (study 10.9): regions of a grid that fuse cells, owned by a
// grid container (GridPanel) and written in `.form` as `span` blocks. The
// values and the checks that both the container and the form model apply
// live here, so the two cannot disagree.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

namespace gui_forms {

// One cell of a grid, zero-based, row first (as grid-area and `.form`'s
// `cell: row column` write it).
struct GUI_FORMS_API GridCell final {
    std::uint32_t row{};
    std::uint32_t column{};

    friend bool operator==(const GridCell& left, const GridCell& right) {
        return left.row == right.row && left.column == right.column;
    }
};

// A cell travels in bindings and `.form` as a point whose x is the row and
// whose y is the column (`cell: 0 2` is row 0, column 2).
template <>
struct PropertyValueTraits<GridCell> {
    static PropertyType type() { return PropertyType{BindingValueKind::point, false, nullptr}; }
    static BindingValue to_binding(GridCell value) {
        return BindingValue{Point{static_cast<double>(value.row), static_cast<double>(value.column)}};
    }
};

// A region of a grid that fuses `rows` x `columns` cells from its anchor,
// its top-left cell (top-right in a right-to-left layout, which the solver
// mirrors). A span is identified by its name when it has one, and always by
// its anchor. A named span is a named area (grid-template-areas is another
// spelling of the named spans); a child placed at an anonymous span's
// anchor occupies the whole region.
struct GUI_FORMS_API GridSpan final {
    // Empty: anonymous.
    std::string name{};
    GridCell anchor{};
    // Each at least 1; the region ends within maximum_grid_span_extent.
    std::uint32_t rows{1U};
    std::uint32_t columns{1U};

    friend bool operator==(const GridSpan& left, const GridSpan& right) {
        return left.name == right.name && left.anchor == right.anchor && left.rows == right.rows &&
               left.columns == right.columns;
    }
};

// The line past the last cell any span or cell may use on either axis:
// the grid's track limit (maximum_grid_tracks).
inline constexpr std::uint32_t maximum_grid_span_extent = 10'000U;

// How a span edit ended. Every refusal leaves the spans unchanged.
enum class GridSpanOutcome : std::uint8_t {
    applied,
    // A span has no rows or columns, or ends past maximum_grid_span_extent.
    extent_invalid,
    // Two spans share a cell.
    overlaps,
    // Two spans have the same name.
    name_repeated,
    // A span covers a cell a child is placed at, other than its anchor.
    covers_placed_child,
    // A removed named span has children placed into it by name.
    in_use,
    // No span has that name or anchor.
    not_found,
};

// Whether a span has at least one row and column and ends within
// maximum_grid_span_extent.
[[nodiscard]] GUI_FORMS_API bool grid_span_extent_valid(const GridSpan& span) noexcept;
// Whether a cell lies within maximum_grid_span_extent on both axes.
[[nodiscard]] GUI_FORMS_API bool grid_cell_admitted(GridCell cell) noexcept;
// Whether `cell` lies in `span`'s region, and in it other than at its
// anchor. Valid extents only.
[[nodiscard]] GUI_FORMS_API bool grid_span_contains(const GridSpan& span, GridCell cell) noexcept;
[[nodiscard]] GUI_FORMS_API bool grid_span_interior(const GridSpan& span, GridCell cell) noexcept;
// Whether two spans share a cell. Valid extents only.
[[nodiscard]] GUI_FORMS_API bool grid_spans_overlap(const GridSpan& first, const GridSpan& second) noexcept;

// A grid container type that holds spans opts in by specializing this to
// true beside its declaration (GridPanel does). Such a type provides the
// child properties `area` and `cell` and has
// `GridSpanOutcome define_span(const GridSpan&)`; the form schema and the
// loader reach it through this declaration only.
template <typename ControlType>
struct HoldsGridSpans : std::false_type {};

} // namespace gui_forms
