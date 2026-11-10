// Stage L, phase L3: the flex solver, headless (PLAN section 2.1). Each case
// is worked out by hand from CSS Flexible Box Layout Level 1, section 9.

#include "gui_forms/layout/flex_solver/flex_solver.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) <= 1.0e-9;
}

void require_rect(const Rect& rect, double x, double y, double width, double height, const std::string& what) {
    const bool same = near(rect.x, x) && near(rect.y, y) && near(rect.width, width) && near(rect.height, height);
    require(same, what + ": got {" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " +
                      std::to_string(rect.width) + ", " + std::to_string(rect.height) + "}");
}

FlexItemInput item(double base, double grow = 0.0, double shrink = 1.0) {
    FlexItemInput input{};
    input.flex_base_size = base;
    input.grow = FlexFactor{grow};
    input.shrink = FlexFactor{shrink};
    return input;
}

FlexContainerInput row(double width, double height) {
    FlexContainerInput container{};
    container.width = FlexAvailableExtent{width, true};
    container.height = FlexAvailableExtent{height, true};
    return container;
}

// Runs both calls with every item's hypothetical cross size `cross`.
void solve(const FlexContainerInput& container, const std::vector<FlexItemInput>& items, double cross,
           FlexLayoutWorkspace& workspace) {
    require(resolve_flex_main_sizes(container, items, workspace) == FlexSolveOutcome::solved, "main sizes");
    const std::vector<FlexItemCross> crosses(items.size(), FlexItemCross{cross});
    require(place_flex_items(container, items, crosses, workspace) == FlexSolveOutcome::solved, "placement");
}

void test_grow_shares_free_space_by_factor() {
    FlexLayoutWorkspace workspace;
    const std::vector<FlexItemInput> items{item(50.0, 1.0), item(50.0, 1.0), item(50.0, 2.0)};
    solve(row(300.0, 40.0), items, 20.0, workspace);
    require_rect(workspace.items()[0].bounds, 0.0, 0.0, 87.5, 40.0, "first grows by a quarter and stretches");
    require_rect(workspace.items()[1].bounds, 87.5, 0.0, 87.5, 40.0, "second");
    require_rect(workspace.items()[2].bounds, 175.0, 0.0, 125.0, 40.0, "third grows by half");
    require(workspace.items()[2].freeze_reason == FlexFreezeReason::flexed, "a grown item is flexed");
}

void test_shrink_is_scaled_by_base_size() {
    FlexLayoutWorkspace workspace;
    std::vector<FlexItemInput> items{item(100.0), item(50.0)};
    solve(row(100.0, 10.0), items, 10.0, workspace);
    require(near(workspace.items()[0].main_size, 100.0 - 50.0 * 2.0 / 3.0), "shrinks by two thirds of 50");
    require(near(workspace.items()[1].main_size, 50.0 - 50.0 / 3.0), "shrinks by a third of 50");
    // A minimum freezes the first at 80 and the second takes the rest.
    items[0].minimum_main = 80.0;
    solve(row(100.0, 10.0), items, 10.0, workspace);
    require(near(workspace.items()[0].main_size, 80.0), "the minimum holds");
    require(workspace.items()[0].freeze_reason == FlexFreezeReason::minimum_violation, "a minimum violation");
    require(near(workspace.items()[1].main_size, 20.0), "the other absorbs the overflow");
}

void test_maximum_violation_and_small_factor_sums() {
    FlexLayoutWorkspace workspace;
    std::vector<FlexItemInput> items{item(0.0, 1.0), item(0.0, 1.0)};
    items[0].maximum_main = 30.0;
    solve(row(200.0, 10.0), items, 10.0, workspace);
    require(near(workspace.items()[0].main_size, 30.0), "the maximum holds");
    require(workspace.items()[0].freeze_reason == FlexFreezeReason::maximum_violation, "a maximum violation");
    require(near(workspace.items()[1].main_size, 170.0), "the other takes the rest");
    // Factors summing below one take only that share of the free space.
    const std::vector<FlexItemInput> partial{item(0.0, 0.25), item(0.0, 0.25)};
    solve(row(200.0, 10.0), partial, 10.0, workspace);
    require(near(workspace.items()[0].main_size, 50.0) && near(workspace.items()[1].main_size, 50.0),
            "a quarter of the free space each");
}

void test_wrap_breaks_lines_and_places_them() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container = row(200.0, 100.0);
    container.wrap = FlexWrap::wrap;
    container.column_gap = 10.0;
    container.row_gap = 4.0;
    container.align_content = LayoutAlignment::flex_start;
    container.align_items = LayoutAlignment::flex_start;
    const std::vector<FlexItemInput> items{item(80.0), item(80.0), item(80.0), item(80.0)};
    solve(container, items, 20.0, workspace);
    require(workspace.lines().size() == 2U, "two lines");
    require_rect(workspace.items()[1].bounds, 90.0, 0.0, 80.0, 20.0, "second on the first line");
    require_rect(workspace.items()[2].bounds, 0.0, 24.0, 80.0, 20.0, "third starts the second line");
    // align-content: stretch shares the free cross space between the lines.
    container.align_content = LayoutAlignment::stretch;
    container.align_items = LayoutAlignment::stretch;
    solve(container, items, 20.0, workspace);
    require(near(workspace.lines()[0].cross_size, 48.0), "each line takes half of the 56 px left");
    require_rect(workspace.items()[2].bounds, 0.0, 52.0, 80.0, 48.0, "stretched items fill their line");
    // wrap-reverse stacks the lines from the bottom.
    container.wrap = FlexWrap::wrap_reverse;
    container.align_content = LayoutAlignment::flex_start;
    container.align_items = LayoutAlignment::flex_start;
    solve(container, items, 20.0, workspace);
    require_rect(workspace.items()[0].bounds, 0.0, 80.0, 80.0, 20.0, "the first line at the bottom");
    require_rect(workspace.items()[2].bounds, 0.0, 56.0, 80.0, 20.0, "the second line above it");
}

void test_justify_content_distributions() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container = row(100.0, 10.0);
    const std::vector<FlexItemInput> items{item(20.0, 0.0, 0.0), item(20.0, 0.0, 0.0)};
    container.justify_content = LayoutAlignment::center;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 30.0), "center");
    container.justify_content = LayoutAlignment::space_between;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[1].bounds.x, 80.0), "space-between");
    container.justify_content = LayoutAlignment::space_around;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 15.0) && near(workspace.items()[1].bounds.x, 65.0), "space-around");
    container.justify_content = LayoutAlignment::space_evenly;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 20.0) && near(workspace.items()[1].bounds.x, 60.0), "space-evenly");
    container.justify_content = LayoutAlignment::flex_end;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 60.0), "flex-end");
    // The fallbacks are safe: overflowing space-between, space-around and
    // space-evenly all pack at the start; a lone item is centred.
    const std::vector<FlexItemInput> wide{item(80.0, 0.0, 0.0), item(80.0, 0.0, 0.0)};
    container.justify_content = LayoutAlignment::space_between;
    solve(container, wide, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 0.0), "negative space-between is safe flex-start");
    container.justify_content = LayoutAlignment::space_around;
    solve(container, wide, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 0.0), "negative space-around is safe center");
    container.justify_content = LayoutAlignment::space_evenly;
    solve(container, wide, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 0.0), "negative space-evenly is safe center");
    const std::vector<FlexItemInput> lone{item(40.0, 0.0, 0.0)};
    solve(container, lone, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 30.0), "a lone space-evenly item is centred");
}

void test_auto_margins_absorb_free_space_first() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container = row(100.0, 50.0);
    container.justify_content = LayoutAlignment::center;
    std::vector<FlexItemInput> items{item(20.0, 0.0, 0.0), item(20.0, 0.0, 0.0)};
    items[1].margin.left = FlexMargin{0.0, true};
    items[1].margin.top = FlexMargin{0.0, true};
    items[1].margin.bottom = FlexMargin{0.0, true};
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 0.0), "justify-content does not apply with auto margins");
    require_rect(workspace.items()[1].bounds, 80.0, 20.0, 20.0, 10.0, "auto margins push right and centre");
}

void test_cross_alignment_and_baselines() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container = row(100.0, 60.0);
    container.align_items = LayoutAlignment::center;
    std::vector<FlexItemInput> items{item(20.0), item(20.0), item(20.0)};
    items[1].align_self = LayoutAlignment::flex_end;
    items[2].definite_cross = true;
    items[2].align_self = LayoutAlignment::stretch;
    solve(container, items, 20.0, workspace);
    require(near(workspace.items()[0].bounds.y, 20.0), "centred");
    require(near(workspace.items()[1].bounds.y, 40.0), "flex-end");
    require(near(workspace.items()[2].bounds.height, 20.0), "a definite cross size is not stretched");
    // Baselines: ascents 10 and 30 align at 30.
    container.align_items = LayoutAlignment::baseline;
    const std::vector<FlexItemInput> plain{item(20.0), item(20.0)};
    require(resolve_flex_main_sizes(container, plain, workspace) == FlexSolveOutcome::solved, "main");
    const std::vector<FlexItemCross> crosses{FlexItemCross{20.0, 10.0}, FlexItemCross{40.0, 30.0}};
    require(place_flex_items(container, plain, crosses, workspace) == FlexSolveOutcome::solved, "place");
    require(near(workspace.items()[0].bounds.y, 20.0) && near(workspace.items()[1].bounds.y, 0.0),
            "baselines align");
}

void test_reversal_and_right_to_left() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container = row(100.0, 10.0);
    const std::vector<FlexItemInput> items{item(20.0, 0.0, 0.0), item(30.0, 0.0, 0.0)};
    container.direction = FlexDirection::row_reverse;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 80.0) && near(workspace.items()[1].bounds.x, 50.0), "row-reverse");
    container.direction = FlexDirection::row;
    container.layout_direction = LayoutDirection::right_to_left;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 80.0), "right-to-left starts on the right");
    // `start` follows the writing mode: a right-to-left row-reverse runs
    // from the left, and `start` packs its items against the right edge,
    // in order.
    container.direction = FlexDirection::row_reverse;
    container.justify_content = LayoutAlignment::start;
    solve(container, items, 10.0, workspace);
    require(near(workspace.items()[0].bounds.x, 50.0) && near(workspace.items()[1].bounds.x, 70.0),
            "start is the inline start");
}

void test_column_with_indefinite_height_and_order() {
    FlexLayoutWorkspace workspace;
    FlexContainerInput container{};
    container.direction = FlexDirection::column;
    container.width = FlexAvailableExtent{50.0, true};
    container.row_gap = 5.0;
    std::vector<FlexItemInput> items{item(10.0, 1.0), item(20.0, 1.0), item(30.0, 1.0)};
    items[0].order = 1;
    solve(container, items, 7.0, workspace);
    require(near(workspace.content_main(), 70.0), "the column's height is its items' and gaps");
    require(workspace.order()[0] == 1U && workspace.order()[2] == 0U, "order 1 goes last");
    require_rect(workspace.items()[1].bounds, 0.0, 0.0, 50.0, 20.0, "the first in order, stretched");
    require_rect(workspace.items()[0].bounds, 0.0, 60.0, 50.0, 10.0, "order 1 after the others and the gaps");
}

void test_refusals() {
    FlexLayoutWorkspace workspace;
    const std::vector<FlexItemInput> many(maximum_flex_items + 1U, item(1.0));
    require(resolve_flex_main_sizes(row(10.0, 10.0), many, workspace) == FlexSolveOutcome::refused_too_many_items,
            "65537 items are refused");
    require(workspace.items().empty(), "a refusal leaves nothing laid out");
    const std::vector<FlexItemInput> two{item(1.0), item(1.0)};
    require(resolve_flex_main_sizes(row(10.0, 10.0), two, workspace) == FlexSolveOutcome::solved, "two items");
    const std::vector<FlexItemCross> one(1U);
    require(place_flex_items(row(10.0, 10.0), two, one, workspace) == FlexSolveOutcome::refused_mismatched_input,
            "a cross record per item");
}

// Audit N41: a non-finite base counts as zero, and a non-finite free space
// (from the container's extent) still ends the freeze loop.
void test_non_finite_inputs_terminate() {
    FlexLayoutWorkspace workspace;
    const double not_a_number = std::numeric_limits<double>::quiet_NaN();
    const std::vector<FlexItemInput> items{item(not_a_number, 1.0), item(50.0, 1.0)};
    solve(row(100.0, 10.0), items, 10.0, workspace);
    require(workspace.items()[0].flex_base_size == 0.0, "a NaN base counts as zero");
    require(std::isfinite(workspace.items()[0].main_size) && std::isfinite(workspace.items()[1].main_size),
            "both items get finite sizes");
    const std::vector<FlexItemInput> plain{item(50.0, 1.0), item(50.0, 1.0)};
    require(resolve_flex_main_sizes(row(not_a_number, 10.0), plain, workspace) == FlexSolveOutcome::solved,
            "a NaN container extent ends the freeze loop");
}

} // namespace

int main() {
    try {
        test_grow_shares_free_space_by_factor();
        test_shrink_is_scaled_by_base_size();
        test_maximum_violation_and_small_factor_sums();
        test_wrap_breaks_lines_and_places_them();
        test_justify_content_distributions();
        test_auto_margins_absorb_free_space_first();
        test_cross_alignment_and_baselines();
        test_reversal_and_right_to_left();
        test_column_with_indefinite_height_and_order();
        test_refusals();
        test_non_finite_inputs_terminate();
        std::cout << "flex-solver-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "flex-solver-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
