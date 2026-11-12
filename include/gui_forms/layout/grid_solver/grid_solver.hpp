#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gui_forms {

// The grid solver (stage L, phase L4; PLAN sections 2.1 and 2.3): CSS Grid
// Layout's placement (its section 8.5) and track sizing (sections 12.3 to
// 12.8) over named records, with no control, painter or host dependency.
// GridPanel realizes it over child controls; item systems may call it
// on their own records.
//
// One layout is four calls, because an item's height can depend on the
// width its columns give it:
// 1. place_grid_items: the explicit grid (with `repeat(auto-fill |
//    auto-fit, ...)` counted against a definite width or height), every
//    item's area by line-based placement and auto-placement, and the
//    implicit tracks.
// 2. size_grid_tracks for the columns, from each item's width
//    contributions.
// 3. The caller measures each item's height at the width of its column
//    area (GridLayoutWorkspace::area_extent), then size_grid_tracks for the
//    rows.
// 4. arrange_grid_items: track alignment (justify-content, align-content),
//    each item's size and self alignment in its area, auto margins,
//    first-baseline alignment across rows, and right-to-left mirroring.
//
// Section 12.1 repeats the column sizing when a row size changes an item's
// width contributions; a control's widths never depend on its height (no
// orthogonal writing modes), so the repeat changes nothing and is not run.
//
// The caller resolves what needs style or content knowledge: the gaps, and
// each item's contributions (min-content, max-content and minimum outer
// sizes). All sizes are logical units of an item's border box (PLAN 2.5);
// contributions include margins. Rectangles are relative to the container's
// content box.

// The most items one container lays out (PLAN section 5).
inline constexpr std::size_t maximum_grid_items = 65'536U;
// The most cells auto-placement's occupancy may cover, rows times columns
// of the implicit grid (PLAN section 5); above it nothing is laid out.
inline constexpr std::size_t maximum_grid_cells = 1'048'576U;
// The most repetitions one `repeat(auto-fill | auto-fit, ...)` generates.
inline constexpr std::size_t maximum_grid_repetitions = 10'000U;

enum class GridAxis : std::uint8_t { columns, rows };

// How one axis of the container's content box is sized: against a definite
// extent, or from the items under a min-content or max-content constraint
// (CSS Grid 12.1's intrinsic sizing of the container).
enum class GridSizingKind : std::uint8_t { definite, min_content, max_content };

struct GUI_FORMS_API GridAvailableExtent final {
    double extent{};
    GridSizingKind kind{GridSizingKind::max_content};
};

// One margin as the solver reads it: a resolved length, or `auto`.
struct GUI_FORMS_API GridMargin final {
    double length{};
    bool automatic{};

    friend constexpr bool operator==(const GridMargin& left, const GridMargin& right) noexcept {
        return left.length == right.length && left.automatic == right.automatic;
    }
};

// One item's inputs to every call.
struct GUI_FORMS_API GridItemInput final {
    // The item's placement properties, borrowed for each call: the caller
    // keeps the record alive and unchanged from the first call to the last.
    const GridItemStyle* placement{};
    // `auto` takes the container's justify-items or align-items.
    LayoutAlignment justify_self{LayoutAlignment::automatic};
    LayoutAlignment align_self{LayoutAlignment::automatic};
    // Physical sides.
    BoxSides<GridMargin> margin{};
    // The item's sizes; a percentage is of its grid area, resolved when the
    // item is placed in it.
    SizeValue width{};
    SizeValue height{};
    SizeValue min_width{};
    SizeValue min_height{};
    SizeValue max_width{SizeKind::none, {}};
    SizeValue max_height{SizeKind::none, {}};
};

struct GUI_FORMS_API GridContainerInput final {
    LayoutAlignment justify_content{LayoutAlignment::normal};
    LayoutAlignment align_content{LayoutAlignment::normal};
    LayoutAlignment justify_items{LayoutAlignment::normal};
    LayoutAlignment align_items{LayoutAlignment::normal};
    // Resolved gaps: column_gap between columns, row_gap between rows.
    double column_gap{};
    double row_gap{};
    GridAvailableExtent width{};
    GridAvailableExtent height{};
    // left_to_right or right_to_left (an `inherit` is resolved by the
    // caller): mirrors the columns at output (PLAN section 4).
    LayoutDirection layout_direction{LayoutDirection::left_to_right};
};

// One item's contributions to the tracks of one axis, outer sizes (margins
// included). `minimum` is the item's minimum contribution (CSS Grid 12.3):
// its min-content contribution when its width or height is definite; the
// outer min-width or min-height when that is a length; otherwise
// `automatic_minimum` is set and `minimum` is the content-based minimum
// (CSS Grid 6.6), which the solver applies only where 6.6 does: when the
// item spans a track whose minimum is `auto` and, if it spans several, no
// flexible one.
struct GUI_FORMS_API GridItemContribution final {
    double minimum{};
    bool automatic_minimum{};
    double min_content{};
    double max_content{};
    // A row contribution's first baseline, from the top of the item's
    // border box at its column size; NaN for none, in which case the solver
    // synthesizes the border box's bottom (12.5 step 1). Column
    // contributions ignore it.
    double baseline{std::numeric_limits<double>::quiet_NaN()};
};

// One track of the implicit grid, in line order.
struct GUI_FORMS_API GridTrack final {
    // The sizing function it was given (from the template, `repeat()`, or
    // grid-auto-columns / -rows for an implicit track).
    GridTrackSize sizing{};
    // The track sizing algorithm's base size and growth limit (infinity
    // while unbounded); `size` is the final base size.
    double base{};
    double growth_limit{std::numeric_limits<double>::infinity()};
    double size{};
    // Its start edge in the content box, after track alignment (columns
    // flow-relative: before right-to-left mirroring).
    double position{};
    bool implicit{};
    // An empty `auto-fit` repetition: size zero, and its gaps collapse.
    bool collapsed{};
};

// One item's area in the implicit grid: zero-based, half-open track ranges.
struct GUI_FORMS_API GridItemArea final {
    std::uint32_t column_start{};
    std::uint32_t column_end{};
    std::uint32_t row_start{};
    std::uint32_t row_end{};

    friend constexpr bool operator==(const GridItemArea& left, const GridItemArea& right) noexcept {
        return left.column_start == right.column_start && left.column_end == right.column_end &&
               left.row_start == right.row_start && left.row_end == right.row_end;
    }
};

struct GUI_FORMS_API GridItemResult final {
    GridItemArea area{};
    // Whether placement found the area from the item's lines (not by
    // auto-placement), for inspection.
    bool column_definite{};
    bool row_definite{};
    // The item's border box in the container's content box.
    Rect bounds{};
};

enum class GridSolveOutcome : std::uint8_t {
    solved,
    // More than maximum_grid_items: nothing was laid out.
    refused_too_many_items,
    // The implicit grid would cover more than maximum_grid_cells: nothing
    // was laid out.
    refused_too_many_cells,
    // A call out of order, an item without its placement record, or a
    // contribution count other than the item count: the call did nothing.
    refused_mismatched_input,
};

// Caller-owned storage the four calls fill, reused from layout to layout
// (house style section 1): `items` has one record per input item, in input
// order; `columns` and `rows` the tracks of the implicit grid in line
// order. Its storage grows only when the item or track count does.
class GUI_FORMS_API GridLayoutWorkspace final {
public:
    [[nodiscard]] std::span<const GridItemResult> items() const& noexcept {
        return std::span<const GridItemResult>(items_.data(), items_.size());
    }
    std::span<const GridItemResult> items() const&& = delete;
    [[nodiscard]] std::span<const GridTrack> columns() const& noexcept {
        return std::span<const GridTrack>(columns_.data(), columns_.size());
    }
    std::span<const GridTrack> columns() const&& = delete;
    [[nodiscard]] std::span<const GridTrack> rows() const& noexcept {
        return std::span<const GridTrack>(rows_.data(), rows_.size());
    }
    std::span<const GridTrack> rows() const&& = delete;
    // How many implicit tracks precede the explicit grid on each axis
    // (from negative or out-of-range lines).
    [[nodiscard]] std::uint32_t leading_implicit_columns() const noexcept { return leading_columns_; }
    [[nodiscard]] std::uint32_t leading_implicit_rows() const noexcept { return leading_rows_; }
    // The extent of item `index`'s area on `axis`: its tracks' sizes and the
    // gaps between them, once that axis is sized; zero before.
    [[nodiscard]] double area_extent(std::size_t index, GridAxis axis) const noexcept;
    // The sized grid's extent on each axis: the tracks and their gaps.
    [[nodiscard]] double content_width() const noexcept { return content_width_; }
    [[nodiscard]] double content_height() const noexcept { return content_height_; }
    // Reserves storage for `count` items, so a later layout of at most that
    // many items (and as many tracks) allocates nothing.
    void reserve(std::size_t count);

    // A track's sizing function with percentages resolved, as the sizing
    // algorithm reads it (CSS Grid 12.4). Internal to the solver.
    enum class Breadth : std::uint8_t { fixed, min_content, max_content, automatic, fraction, fit_content };
    struct GUI_FORMS_API TrackFunction final {
        Breadth minimum{Breadth::automatic};
        double minimum_value{};
        Breadth maximum{Breadth::automatic};
        // The fixed maximum, the fraction, or the fit-content() limit.
        double maximum_value{};
    };
    // One item's placement on one axis during auto-placement: its start
    // line (zero for the explicit grid's first line, negative before it),
    // its span, and whether its lines fixed it. Internal to the solver.
    struct GUI_FORMS_API AxisPlacement final {
        std::int64_t start{};
        std::uint32_t span{1U};
        bool definite{};
    };

private:
    friend GUI_FORMS_API GridSolveOutcome place_grid_items(const GridContainerStyle& style, const GridContainerInput& container,
                                             std::span<const GridItemInput> items, GridLayoutWorkspace& workspace);
    friend GUI_FORMS_API GridSolveOutcome size_grid_tracks(GridAxis axis, const GridContainerInput& container,
                                             std::span<const GridItemInput> items,
                                             std::span<const GridItemContribution> contributions,
                                             GridLayoutWorkspace& workspace);
    friend GUI_FORMS_API double grid_item_column_size(const GridContainerInput& container, const GridItemInput& item,
                                        const GridItemContribution& column_contribution,
                                        const GridLayoutWorkspace& workspace, std::size_t index);
    friend GUI_FORMS_API GridSolveOutcome arrange_grid_items(const GridContainerInput& container,
                                               std::span<const GridItemInput> items,
                                               std::span<const GridItemContribution> column_contributions,
                                               std::span<const GridItemContribution> row_contributions,
                                               GridLayoutWorkspace& workspace);

    std::vector<GridItemResult> items_{};
    std::vector<GridTrack> columns_{};
    std::vector<GridTrack> rows_{};
    std::vector<TrackFunction> column_functions_{};
    std::vector<TrackFunction> row_functions_{};
    // The explicit tracks' sizing functions, `repeat()` expanded.
    std::vector<GridTrackSize> explicit_columns_{};
    std::vector<GridTrackSize> explicit_rows_{};
    // Auto-placement's occupancy, one byte per cell, rows of `across` cells
    // along the flow's growing axis; and step 2's per-row cursors.
    std::vector<std::uint8_t> occupancy_{};
    std::vector<std::uint32_t> lane_cursors_{};
    // Per item, its placement on each axis during auto-placement.
    std::vector<AxisPlacement> column_placements_{};
    std::vector<AxisPlacement> row_placements_{};
    // Track sizing scratch, per track of the axis being sized.
    std::vector<double> planned_{};
    std::vector<double> incurred_{};
    std::vector<std::uint8_t> frozen_{};
    std::vector<std::uint8_t> growable_{};
    // Items in span order for the spanning steps, and the row contributions
    // with baseline shims added.
    std::vector<std::uint32_t> span_order_{};
    std::vector<GridItemContribution> adjusted_{};
    std::vector<double> baseline_shims_{};
    std::vector<double> group_ascents_{};
    std::uint32_t explicit_column_count_{};
    std::uint32_t explicit_row_count_{};
    std::uint32_t leading_columns_{};
    std::uint32_t leading_rows_{};
    // The explicit tracks an `auto-fit` repetition generated, as track
    // indices of the implicit grid (empty when none).
    std::uint32_t fit_columns_first_{};
    std::uint32_t fit_columns_end_{};
    std::uint32_t fit_rows_first_{};
    std::uint32_t fit_rows_end_{};
    double column_gap_{};
    double row_gap_{};
    double content_width_{};
    double content_height_{};
    bool placed_{};
    bool columns_sized_{};
    bool rows_sized_{};
};

// The first call. Fills the tracks (sizing functions, implicit and
// collapsed flags) and each item's area; `style` and `items` are borrowed
// for the call. Refuses more than maximum_grid_items items, and an implicit
// grid of more than maximum_grid_cells cells.
GUI_FORMS_API GridSolveOutcome place_grid_items(const GridContainerStyle& style, const GridContainerInput& container,
                                  std::span<const GridItemInput> items, GridLayoutWorkspace& workspace);

// The second and third calls: sizes the tracks of `axis`, columns first,
// from one contribution per item on that axis. Fills each track's base,
// growth_limit and size, and the content extent.
GUI_FORMS_API GridSolveOutcome size_grid_tracks(GridAxis axis, const GridContainerInput& container,
                                  std::span<const GridItemInput> items,
                                  std::span<const GridItemContribution> contributions,
                                  GridLayoutWorkspace& workspace);

// The border-box width item `index` takes in its column area, once the
// columns are sized: what arrange_grid_items will give it. The caller
// measures the item's height at this width for its row contributions.
GUI_FORMS_API double grid_item_column_size(const GridContainerInput& container, const GridItemInput& item,
                             const GridItemContribution& column_contribution, const GridLayoutWorkspace& workspace,
                             std::size_t index);

// The fourth call, with the contributions the sizing calls were given.
// Fills the tracks' positions and each item's bounds.
GUI_FORMS_API GridSolveOutcome arrange_grid_items(const GridContainerInput& container, std::span<const GridItemInput> items,
                                    std::span<const GridItemContribution> column_contributions,
                                    std::span<const GridItemContribution> row_contributions,
                                    GridLayoutWorkspace& workspace);

} // namespace gui_forms
