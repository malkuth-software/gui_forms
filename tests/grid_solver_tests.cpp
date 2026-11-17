// Stage L, phase L4: the grid solver headless. Placement (lines, spans,
// areas, sparse and dense auto-placement in both flows, implicit tracks on
// both sides), track sizing (fixed, flexible, intrinsic, spanning items,
// fit-content, auto-repeat), alignment, right-to-left and the refusals.

#include "gui_forms/layout/grid_solver/grid_solver.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) <= 1.0e-6;
}

GridTrackSize px(double value) {
    GridTrackSize size{};
    size.minimum = TrackBreadth{TrackBreadthKind::length_percentage, LengthPercentage{LogicalLength{value}, 0.0}, FractionFactor{}};
    size.maximum = size.minimum;
    return size;
}

GridTrackSize fr(double value) {
    GridTrackSize size{};
    size.maximum = TrackBreadth{TrackBreadthKind::fraction, {}, FractionFactor{value}};
    return size;
}

GridTrackSize automatic() {
    return GridTrackSize{};
}

GridTrackSize fit(double limit) {
    GridTrackSize size{};
    size.maximum = TrackBreadth{TrackBreadthKind::max_content, {}, FractionFactor{}};
    size.fit_content = true;
    size.fit_content_limit = LengthPercentage{LogicalLength{limit}, 0.0};
    return size;
}

GridLine line(std::int32_t number) {
    return GridLine{GridLineKind::line, number, {}};
}

GridLine span(std::int32_t number) {
    return GridLine{GridLineKind::span, number, {}};
}

GridLine area(const std::string& name) {
    return GridLine{GridLineKind::area, 0, name};
}

GridItemStyle placed(GridLine column_start, GridLine column_end, GridLine row_start, GridLine row_end) {
    GridItemStyle style{};
    style.column_start = column_start;
    style.column_end = column_end;
    style.row_start = row_start;
    style.row_end = row_end;
    return style;
}

// One test grid: the style, the items' placements, and box sizes that are
// every contribution (min-content, max-content and minimum alike).
struct Box final {
    double width{};
    double height{};
};

struct Grid final {
    GridContainerStyle style{};
    GridContainerInput container{};
    std::vector<GridItemStyle> placements{};
    std::vector<Box> boxes{};
    std::vector<GridItemInput> items{};
    std::vector<GridItemContribution> columns{};
    std::vector<GridItemContribution> rows{};
    GridLayoutWorkspace workspace{};

    void add(const GridItemStyle& placement, Box size) {
        placements.push_back(placement);
        boxes.push_back(size);
    }

    GridSolveOutcome solve() {
        items.assign(placements.size(), GridItemInput{});
        columns.assign(placements.size(), GridItemContribution{});
        rows.assign(placements.size(), GridItemContribution{});
        for (std::size_t index = 0U; index < placements.size(); ++index) {
            items[index].placement = &placements[index];
            const double width = boxes[index].width;
            items[index].width = SizeValue{SizeKind::length_percentage, LengthPercentage{LogicalLength{width}, 0.0}};
            items[index].height =
                SizeValue{SizeKind::length_percentage, LengthPercentage{LogicalLength{boxes[index].height}, 0.0}};
            columns[index] = GridItemContribution{width, false, width, width};
        }
        return run();
    }

    // With the items' sizes already in `items`, `columns` and the boxes'
    // heights.
    GridSolveOutcome run() {
        const GridSolveOutcome placed = place_grid_items(style, container, items, workspace);
        if (placed != GridSolveOutcome::solved) return placed;
        const GridSolveOutcome sized = size_grid_tracks(GridAxis::columns, container, items, columns, workspace);
        if (sized != GridSolveOutcome::solved) return sized;
        rows.assign(items.size(), GridItemContribution{});
        for (std::size_t index = 0U; index < items.size(); ++index) {
            const double height = boxes[index].height;
            rows[index] = GridItemContribution{height, false, height, height};
        }
        const GridSolveOutcome rows_sized = size_grid_tracks(GridAxis::rows, container, items, rows, workspace);
        if (rows_sized != GridSolveOutcome::solved) return rows_sized;
        return arrange_grid_items(container, items, columns, rows, workspace);
    }

    [[nodiscard]] Rect bounds(std::size_t index) const { return workspace.items()[index].bounds; }
    [[nodiscard]] GridItemArea area_of(std::size_t index) const { return workspace.items()[index].area; }
};

GridContainerInput definite(double width, double height) {
    GridContainerInput container{};
    container.width = GridAvailableExtent{width, GridSizingKind::definite};
    container.height = GridAvailableExtent{height, GridSizingKind::definite};
    return container;
}

std::string describe(Rect rect) {
    return "{" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " + std::to_string(rect.width) + ", " +
           std::to_string(rect.height) + "}";
}

void require_rect(Rect rect, double x, double y, double width, double height, const std::string& what) {
    const bool same = near(rect.x, x) && near(rect.y, y) && near(rect.width, width) && near(rect.height, height);
    require(same, what + ": got " + describe(rect));
}

void test_fixed_and_flexible_columns() {
    Grid grid{};
    grid.container = definite(400.0, 100.0);
    grid.container.column_gap = 10.0;
    grid.style.template_columns.tracks = {px(100.0), fr(1.0), fr(2.0)};
    grid.style.template_rows.tracks = {px(40.0)};
    grid.add(placed({}, {}, {}, {}), {20.0, 20.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    const std::span<const GridTrack> columns = grid.workspace.columns();
    require(columns.size() == 3U, "three columns");
    require(near(columns[0].size, 100.0) && near(columns[1].size, 280.0 / 3.0) && near(columns[2].size, 560.0 / 3.0),
            "100px 1fr 2fr share 280 px");
    require(near(columns[2].position, 100.0 + 10.0 + 280.0 / 3.0 + 10.0), "the third column starts after two gaps");
    require_rect(grid.bounds(0), 0.0, 0.0, 20.0, 20.0, "a sized item sits at its area's start");
}

void test_auto_tracks_size_and_stretch() {
    Grid grid{};
    grid.container = definite(300.0, 100.0);
    grid.style.template_columns.tracks = {automatic(), automatic()};
    grid.add(placed({}, {}, {}, {}), {50.0, 10.0});
    grid.add(placed({}, {}, {}, {}), {80.0, 10.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(near(grid.workspace.columns()[0].size, 135.0) && near(grid.workspace.columns()[1].size, 165.0),
            "auto columns take content and share the rest (justify-content: normal)");
    // One implicit auto row stretched to the height.
    require(grid.workspace.rows().size() == 1U && near(grid.workspace.rows()[0].size, 100.0), "the row stretches");
    grid.container.justify_content = LayoutAlignment::start;
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(near(grid.workspace.columns()[0].size, 50.0) && near(grid.workspace.columns()[1].size, 80.0),
            "with justify-content: start the auto columns keep their content size");
}

void test_fit_content_and_minmax() {
    Grid grid{};
    grid.container = definite(500.0, 50.0);
    grid.container.justify_content = LayoutAlignment::start;
    GridTrackSize bounded{};
    bounded.minimum = px(30.0).minimum;
    bounded.maximum = px(70.0).maximum;
    grid.style.template_columns.tracks = {fit(60.0), fit(60.0), bounded};
    grid.add(placed(line(1), {}, {}, {}), {100.0, 10.0});
    grid.add(placed(line(2), {}, {}, {}), {40.0, 10.0});
    grid.add(placed(line(3), {}, {}, {}), {10.0, 10.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    const std::span<const GridTrack> columns = grid.workspace.columns();
    require(near(columns[0].size, 100.0), "fit-content's minimum is auto: the 100 px item's minimum holds it");
    require(near(columns[1].size, 40.0), "fit-content takes the content below its limit");
    require(near(columns[2].size, 70.0), "minmax(30px, 70px) is maximized to 70 px");
}

// An item's automatic minimum (CSS Grid 6.6) is clamped only by fixed
// maximums: a fit-content() argument limits the min- and max-content
// contributions, not the automatic minimum. A word wider than the argument
// keeps its column at the word's width, as Chromium lays it out.
void test_fit_content_keeps_an_automatic_minimum() {
    Grid grid{};
    grid.container = definite(500.0, 50.0);
    grid.container.justify_content = LayoutAlignment::start;
    grid.style.template_columns.tracks = {fit(40.0), fit(120.0)};
    grid.add(placed(line(1), {}, {}, {}), {70.0, 10.0});
    grid.add(placed(line(2), {}, {}, {}), {200.0, 10.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    // The first item is one 70 px word: every contribution is its width,
    // and its minimum is automatic. The second is a 200 px paragraph whose
    // widest word is 30 px.
    grid.columns[0] = GridItemContribution{70.0, true, 70.0, 70.0};
    grid.columns[1] = GridItemContribution{30.0, true, 30.0, 200.0};
    require(grid.run() == GridSolveOutcome::solved, "solved with automatic minimums");
    const std::span<const GridTrack> columns = grid.workspace.columns();
    require(near(columns[0].size, 70.0), "fit-content(40px) does not clamp a 70 px automatic minimum");
    require(near(columns[1].size, 120.0), "fit-content(120px) clamps a 200 px max-content contribution");
}

void test_spanning_item_distributes() {
    Grid grid{};
    grid.container.width = GridAvailableExtent{0.0, GridSizingKind::max_content};
    grid.container.height = GridAvailableExtent{0.0, GridSizingKind::max_content};
    grid.style.template_columns.tracks = {automatic(), automatic()};
    grid.add(placed(line(1), {}, line(1), {}), {50.0, 10.0});
    grid.add(placed(line(1), span(2), line(2), {}), {200.0, 10.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    const std::span<const GridTrack> columns = grid.workspace.columns();
    require(near(columns[0].size, 50.0) && near(columns[1].size, 150.0),
            "the spanning item's extra space goes to the track with room: got " + std::to_string(columns[0].size) +
                ", " + std::to_string(columns[1].size));
    require(near(grid.workspace.content_width(), 200.0), "the max-content width is the spanning item's");

    Grid even{};
    even.container = grid.container;
    even.style.template_columns.tracks = {automatic(), automatic()};
    even.add(placed(line(1), span(2), {}, {}), {120.0, 10.0});
    require(even.solve() == GridSolveOutcome::solved, "solved");
    require(near(even.workspace.columns()[0].size, 60.0) && near(even.workspace.columns()[1].size, 60.0),
            "with no other items the span is shared equally");
}

void test_placement_lines_and_auto_flow() {
    Grid grid{};
    grid.container = definite(300.0, 300.0);
    grid.style.template_columns.tracks = {px(10.0), px(10.0), px(10.0)};
    grid.style.template_rows.tracks = {px(10.0), px(10.0)};
    grid.add(placed(line(2), line(4), line(1), {}), {1.0, 1.0});   // columns 2-3, row 1
    grid.add(placed(line(-1), {}, {}, {}), {1.0, 1.0});            // after the last explicit line
    grid.add(placed({}, {}, {}, {}), {1.0, 1.0});                  // auto
    grid.add(placed({}, span(2), {}, {}), {1.0, 1.0});             // auto, two columns
    grid.add(placed(line(-5), {}, line(1), {}), {1.0, 1.0});       // one implicit column before
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(grid.workspace.leading_implicit_columns() == 1U, "line -5 of 3 columns adds one before");
    // Track indices include the leading column.
    require(grid.area_of(0) == GridItemArea{2U, 4U, 0U, 1U}, "explicit lines 2 / 4");
    require(grid.area_of(1).column_start == 4U && grid.area_of(1).column_end == 5U,
            "line -1 starts at the last explicit line: an implicit column after");
    require(grid.area_of(4) == GridItemArea{0U, 1U, 0U, 1U}, "the implicit column before the grid");
    // Sparse: the cursor followed the line -1 item to the last column, so
    // the first auto item starts row 2 (in the leading implicit column).
    require(grid.area_of(2).row_start == 1U && grid.area_of(2).column_start == 0U,
            "the first auto item goes on from the cursor to row 2");
    require(grid.area_of(3).row_start == 1U && grid.area_of(3).column_start == 1U &&
                grid.area_of(3).column_end == 3U,
            "the two-column item follows it");

    Grid sparse{};
    sparse.container = definite(300.0, 300.0);
    sparse.style.template_columns.tracks = {px(10.0), px(10.0), px(10.0)};
    sparse.add(placed({}, {}, {}, {}), {1.0, 1.0});
    sparse.add(placed({}, span(3), {}, {}), {1.0, 1.0});
    sparse.add(placed({}, {}, {}, {}), {1.0, 1.0});
    require(sparse.solve() == GridSolveOutcome::solved, "solved");
    require(sparse.area_of(2).row_start == 2U, "sparse placement never goes back to row 1's hole");
    sparse.style.auto_flow = GridAutoFlow::row_dense;
    require(sparse.solve() == GridSolveOutcome::solved, "solved");
    require(sparse.area_of(2).row_start == 0U && sparse.area_of(2).column_start == 1U,
            "dense placement fills the hole");

    Grid column_flow{};
    column_flow.container = definite(300.0, 300.0);
    column_flow.style.template_rows.tracks = {px(10.0), px(10.0)};
    column_flow.style.auto_flow = GridAutoFlow::column;
    for (int index = 0; index < 5; ++index) column_flow.add(placed({}, {}, {}, {}), {1.0, 1.0});
    require(column_flow.solve() == GridSolveOutcome::solved, "solved");
    require(column_flow.area_of(4) == GridItemArea{2U, 3U, 0U, 1U}, "column flow fills each column's rows first");
    require(column_flow.workspace.columns().size() == 3U, "three implicit columns");
}

void test_template_areas() {
    Grid grid{};
    grid.container = definite(300.0, 200.0);
    grid.style.template_columns.tracks = {px(100.0), fr(1.0)};
    grid.style.template_rows.tracks = {px(40.0), fr(1.0)};
    grid.style.template_areas.rows = 2U;
    grid.style.template_areas.columns = 2U;
    grid.style.template_areas.areas = {GridNamedArea{"head", 0U, 1U, 0U, 2U}, GridNamedArea{"side", 1U, 2U, 0U, 1U},
                                       GridNamedArea{"main", 1U, 2U, 1U, 2U}};
    grid.add(placed(area("main"), area("main"), area("main"), area("main")), {0.0, 0.0});
    grid.add(placed(area("head"), area("head"), area("head"), area("head")), {0.0, 0.0});
    grid.add(placed(area("none"), area("none"), area("none"), area("none")), {0.0, 0.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(grid.area_of(0) == GridItemArea{1U, 2U, 1U, 2U}, "main");
    require(grid.area_of(1) == GridItemArea{0U, 2U, 0U, 1U}, "head spans both columns");
    require(grid.area_of(2) == GridItemArea{3U, 4U, 3U, 4U},
            "an unknown area names the first implicit line after the explicit grid");
}

void test_auto_repeat() {
    Grid grid{};
    grid.container = definite(350.0, 100.0);
    grid.container.column_gap = 10.0;
    grid.container.justify_content = LayoutAlignment::start;
    grid.style.template_columns.auto_repeat = AutoRepeatKind::auto_fill;
    grid.style.template_columns.auto_repeat_tracks = {px(100.0)};
    grid.add(placed({}, {}, {}, {}), {1.0, 1.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(grid.workspace.columns().size() == 3U, "auto-fill: 3 x 100 px and two 10 px gaps fit in 350 px");
    require(near(grid.workspace.content_width(), 320.0), "the filled grid is 320 px");
    grid.style.template_columns.auto_repeat = AutoRepeatKind::auto_fit;
    grid.container.justify_content = LayoutAlignment::center;
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require(grid.workspace.columns()[1].collapsed && grid.workspace.columns()[2].collapsed,
            "auto-fit collapses the empty repetitions");
    require(near(grid.workspace.content_width(), 100.0) && near(grid.workspace.columns()[0].position, 125.0),
            "collapsed tracks and their gaps take no space");
}

// A-gf-core-08-F1: an automatic repeat of 200 tiny tracks counts 10,000
// repetitions, 2,000,000 columns, past the cell limit: the solve is refused
// before the expansion is made, as the cell check after placement refused
// it.
void test_auto_repeat_past_the_cell_limit() {
    Grid grid{};
    grid.container = definite(400.0, 100.0);
    grid.style.template_columns.auto_repeat = AutoRepeatKind::auto_fill;
    grid.style.template_columns.auto_repeat_tracks.assign(200U, px(1.0e-9));
    grid.style.template_rows.tracks = {px(40.0)};
    grid.add(placed({}, {}, {}, {}), {1.0, 1.0});
    require(grid.solve() == GridSolveOutcome::refused_too_many_cells,
            "an automatic repeat past the cell limit is refused");
    require(grid.workspace.items().empty() && grid.workspace.columns().empty(),
            "the refused solve lays nothing out");
    grid.style.template_columns.auto_repeat_tracks.assign(2U, px(100.0));
    require(grid.solve() == GridSolveOutcome::solved && grid.workspace.columns().size() == 4U,
            "the next solve within the limit lays out again");
}

void test_alignment_and_margins() {
    Grid grid{};
    grid.container = definite(200.0, 100.0);
    grid.style.template_columns.tracks = {px(100.0), px(100.0)};
    grid.style.template_rows.tracks = {px(100.0)};
    grid.add(placed({}, {}, {}, {}), {40.0, 20.0});
    grid.add(placed({}, {}, {}, {}), {40.0, 20.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    // Re-run with self alignment and auto margins on the same records.
    grid.items[0].justify_self = LayoutAlignment::center;
    grid.items[0].align_self = LayoutAlignment::end;
    grid.items[1].margin.left = GridMargin{0.0, true};
    grid.items[1].margin.top = GridMargin{0.0, true};
    grid.items[1].margin.bottom = GridMargin{0.0, true};
    require(grid.run() == GridSolveOutcome::solved, "solved");
    require_rect(grid.bounds(0), 30.0, 80.0, 40.0, 20.0, "centred across, at the end down");
    require_rect(grid.bounds(1), 160.0, 40.0, 40.0, 20.0, "auto margins push right and centre vertically");

    // Auto-sized items stretch (normal), baseline-aligned ones share a
    // synthesized baseline at their border boxes' bottoms.
    Grid stretched{};
    stretched.container = definite(200.0, 60.0);
    stretched.style.template_columns.tracks = {px(50.0), px(50.0)};
    stretched.add(placed({}, {}, {}, {}), {0.0, 0.0});
    stretched.add(placed({}, {}, {}, {}), {0.0, 0.0});
    stretched.items.assign(2U, GridItemInput{});
    stretched.columns.assign(2U, GridItemContribution{10.0, false, 10.0, 10.0});
    stretched.items[0].placement = &stretched.placements[0];
    stretched.items[1].placement = &stretched.placements[1];
    stretched.boxes[0].height = 20.0;
    stretched.boxes[1].height = 20.0;
    require(stretched.run() == GridSolveOutcome::solved, "solved");
    require_rect(stretched.bounds(0), 0.0, 0.0, 50.0, 60.0, "an auto-sized item stretches in both axes");

    Grid baseline{};
    baseline.container = definite(200.0, 100.0);
    baseline.container.align_items = LayoutAlignment::baseline;
    baseline.container.align_content = LayoutAlignment::start;
    baseline.style.template_columns.tracks = {px(50.0), px(50.0)};
    baseline.add(placed({}, {}, {}, {}), {50.0, 20.0});
    baseline.add(placed({}, {}, {}, {}), {50.0, 45.0});
    require(baseline.solve() == GridSolveOutcome::solved, "solved");
    require_rect(baseline.bounds(0), 0.0, 25.0, 50.0, 20.0, "the shorter item drops to the shared baseline");
    require(near(baseline.workspace.rows()[0].size, 45.0), "the row holds the shimmed item");
}

void test_right_to_left() {
    Grid grid{};
    grid.container = definite(300.0, 100.0);
    grid.container.layout_direction = LayoutDirection::right_to_left;
    grid.style.template_columns.tracks = {px(100.0), px(50.0)};
    grid.add(placed({}, {}, {}, {}), {100.0, 10.0});
    grid.add(placed({}, {}, {}, {}), {30.0, 10.0});
    require(grid.solve() == GridSolveOutcome::solved, "solved");
    require_rect(grid.bounds(0), 200.0, 0.0, 100.0, 10.0, "the first column is at the right");
    require_rect(grid.bounds(1), 170.0, 0.0, 30.0, 10.0, "the second is left of it, its item at its start (right)");
}

void test_refusals() {
    Grid cells{};
    cells.container = definite(100.0, 100.0);
    cells.style.template_columns.tracks = {px(10.0), px(10.0)};
    cells.add(placed({}, {}, line(600'000), {}), {1.0, 1.0});
    require(cells.solve() == GridSolveOutcome::refused_too_many_cells, "2 x 600,000 cells are refused");
    require(cells.workspace.items().empty() && cells.workspace.columns().empty(), "a refusal leaves nothing");

    Grid order{};
    order.container = definite(100.0, 100.0);
    order.add(placed({}, {}, {}, {}), {1.0, 1.0});
    order.items.assign(1U, GridItemInput{});
    order.items[0].placement = &order.placements[0];
    order.columns.assign(1U, GridItemContribution{});
    require(place_grid_items(order.style, order.container, order.items, order.workspace) == GridSolveOutcome::solved,
            "placed");
    require(size_grid_tracks(GridAxis::rows, order.container, order.items, order.columns, order.workspace) ==
                GridSolveOutcome::refused_mismatched_input,
            "rows before columns are refused");
    const std::vector<GridItemContribution> none{};
    require(size_grid_tracks(GridAxis::columns, order.container, order.items, none, order.workspace) ==
                GridSolveOutcome::refused_mismatched_input,
            "a missing contribution is refused");
    order.items[0].placement = nullptr;
    require(place_grid_items(order.style, order.container, order.items, order.workspace) ==
                GridSolveOutcome::refused_mismatched_input,
            "an item without its placement record is refused");
}

} // namespace

int main() {
    try {
        test_fixed_and_flexible_columns();
        test_auto_tracks_size_and_stretch();
        test_fit_content_and_minmax();
        test_fit_content_keeps_an_automatic_minimum();
        test_spanning_item_distributes();
        test_placement_lines_and_auto_flow();
        test_template_areas();
        test_auto_repeat();
        test_auto_repeat_past_the_cell_limit();
        test_alignment_and_margins();
        test_right_to_left();
        test_refusals();
        std::cout << "grid-solver-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "grid-solver-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
