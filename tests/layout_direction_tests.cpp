// Right-to-left layout outside the CSS containers (stage L, phase L6): the
// owner's full mirroring (notes:planning/stages/L-layout/README.md, "Decisions in
// force"). Dock, Anchor and requested bounds in a right-to-left parent, the
// Windows Forms flow and table containers, scrolling, and the containers'
// reused workspaces under a measure that lays the panel out again.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap, as tests/layout_panel_tests.cpp
// does, to keep main's inlined frame small under the sanitizer.

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::shared_ptr<Button> sized_button(std::string id, double width, double height) {
    std::shared_ptr<Button> button = make_control<Button>(StableId(std::move(id)), "Item");
    (*button).set_requested_bounds({0.0, 0.0, width, height});
    (*button).margin = {0.0, 0.0, 0.0, 0.0};
    return button;
}

// Records the x of each vertical line drawn.
class VerticalLinePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point from, Point to, Color, double) override {
        if (from.x == to.x) xs.push_back(from.x);
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    std::vector<double> xs{};
};

// A child whose first measure measures its parent again, so the parent's
// layout pass runs inside its own pass. `parent` only observes.
class ReentrantMeasureControl final : public Control {
public:
    explicit ReentrantMeasureControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        const std::shared_ptr<Control> owner = parent_panel.lock();
        if (!reentered && owner) {
            reentered = true;
            static_cast<void>((*owner).measure({200.0, 200.0}));
        }
        return Control::measure(available);
    }

    std::weak_ptr<Control> parent_panel{};
    bool reentered{};
};

void test_dock_anchor_and_requested_bounds_mirror() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("direction.dock.root"));
    (*root).padding = {20.0, 10.0, 0.0, 10.0};
    std::shared_ptr<Button> left = sized_button("direction.dock.left", 30.0, 10.0);
    std::shared_ptr<Button> top = sized_button("direction.dock.top", 10.0, 20.0);
    std::shared_ptr<Button> placed = sized_button("direction.dock.placed", 50.0, 20.0);
    std::shared_ptr<Button> right = sized_button("direction.dock.right", 40.0, 20.0);
    (*left).dock = DockStyle::left;
    (*top).dock = DockStyle::top;
    (*placed).set_requested_bounds({10.0, 40.0, 50.0, 20.0});
    (*right).set_requested_bounds({200.0, 100.0, 40.0, 20.0});
    (*right).anchor = AnchorStyles::top | AnchorStyles::right;
    (*root).add_child(left);
    (*root).add_child(top);
    (*root).add_child(placed);
    (*root).add_child(right);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*left).arranged_bounds() == Rect{20.0, 10.0, 30.0, 180.0} &&
                (*top).arranged_bounds() == Rect{50.0, 10.0, 250.0, 20.0} &&
                (*placed).arranged_bounds() == Rect{10.0, 40.0, 50.0, 20.0},
            "left to right, dock and requested bounds are unchanged");

    (*root).layout_direction = LayoutDirection::right_to_left;
    window.perform_layout();
    require((*left).arranged_bounds() == Rect{250.0, 10.0, 30.0, 180.0} &&
                (*top).arranged_bounds() == Rect{0.0, 10.0, 250.0, 20.0},
            "right to left, dock left docks right and the left padding is at the right");
    require((*placed).arranged_bounds() == Rect{240.0, 40.0, 50.0, 20.0} &&
                (*right).arranged_bounds() == Rect{60.0, 100.0, 40.0, 20.0},
            "right to left, a requested x is measured from the right edge");
    require((*placed).requested_bounds() == Rect{10.0, 40.0, 50.0, 20.0},
            "mirroring must not rewrite the authored rectangle");

    window.resize({400.0, 200.0});
    window.perform_layout();
    require((*placed).arranged_bounds() == Rect{340.0, 40.0, 50.0, 20.0} &&
                (*right).arranged_bounds() == Rect{60.0, 100.0, 40.0, 20.0},
            "right to left, anchor left follows the right edge and anchor right keeps the left");
    require(window.hit_test({345.0, 45.0}) == placed,
            "hit testing must follow the mirrored geometry");

    (*root).layout_direction = LayoutDirection::left_to_right;
    window.perform_layout();
    require((*left).arranged_bounds() == Rect{20.0, 10.0, 30.0, 180.0} &&
                (*placed).arranged_bounds() == Rect{10.0, 40.0, 50.0, 20.0} &&
                (*right).arranged_bounds() == Rect{300.0, 100.0, 40.0, 20.0},
            "returning to left to right restores the unmirrored layout");
}

void test_flow_layout_starts_at_the_right() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("direction.flow"));
    std::shared_ptr<Button> first = sized_button("direction.flow.first", 40.0, 20.0);
    std::shared_ptr<Button> second = sized_button("direction.flow.second", 40.0, 20.0);
    std::shared_ptr<Button> third = sized_button("direction.flow.third", 40.0, 20.0);
    (*flow).add_child(first);
    (*flow).add_child(second);
    (*flow).add_child(third);
    (*flow).layout_direction = LayoutDirection::right_to_left;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{100.0, 50.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{60.0, 0.0, 40.0, 20.0} &&
                (*second).arranged_bounds() == Rect{20.0, 0.0, 40.0, 20.0} &&
                (*third).arranged_bounds() == Rect{60.0, 20.0, 40.0, 20.0},
            "right to left, a left-to-right flow starts at the right edge");

    (*flow).set_flow_direction(FlowDirection::right_to_left);
    window.perform_layout();
    require((*first).arranged_bounds().x == 0.0 && (*second).arranged_bounds().x == 40.0 &&
                (*third).arranged_bounds().x == 0.0,
            "right to left, a right-to-left flow mirrors back to the left edge");

    (*flow).set_flow_direction(FlowDirection::top_down);
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{60.0, 0.0, 40.0, 20.0} &&
                (*second).arranged_bounds() == Rect{60.0, 20.0, 40.0, 20.0} &&
                (*third).arranged_bounds() == Rect{20.0, 0.0, 40.0, 20.0},
            "right to left, a vertical flow's columns start at the right edge");

    (*flow).set_flow_direction(FlowDirection::left_to_right);
    (*flow).padding = {10.0, 0.0, 0.0, 0.0};
    (*second).margin = {5.0, 0.0, 0.0, 0.0};
    window.perform_layout();
    require((*first).arranged_bounds().x == 50.0 && (*second).arranged_bounds().x == 5.0 &&
                (*third).arranged_bounds() == Rect{50.0, 20.0, 40.0, 20.0},
            "right to left, padding and margins mirror with the layout");

    (*flow).layout_direction = LayoutDirection::left_to_right;
    window.perform_layout();
    require((*first).arranged_bounds().x == 10.0 && (*second).arranged_bounds().x == 55.0,
            "left to right, the flow is unchanged");
}

void test_flow_scrolls_toward_the_left() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("direction.scroll"));
    (*flow).auto_scroll = true;
    (*flow).set_wrap_contents(false);
    (*flow).set_item_spacing({4.0, 4.0});
    (*flow).layout_direction = LayoutDirection::right_to_left;
    std::vector<std::shared_ptr<Button>> items;
    for (int index = 0; index < 8; ++index) {
        std::shared_ptr<Button> item =
            make_control<Button>(StableId("direction.scroll." + std::to_string(index)), "Frame");
        (*item).set_requested_bounds({0.0, 0.0, 82.0, 76.0});
        (*flow).add_child(item);
        items.push_back(item);
    }
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{300.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    const Rect first = (*items.front()).absolute_bounds();
    require((*flow).hscroll() && first.x == 215.0 && window.hit_test({first.x + 10.0, 20.0}) == items.front(),
            "right to left, an unscrolled flow shows its first item at the right edge, inside its margin");

    (*flow).scroll_control_into_view(items.back());
    window.perform_layout();
    const Rect last = (*items.back()).absolute_bounds();
    require((*flow).scroll_position().x > 0.0 && last.x >= -0.001 && last.right() <= 300.001 &&
                window.hit_test({last.x + 10.0, last.y + 10.0}) == items.back(),
            "right to left, scrolling into view brings a later item in from the left");
    require((*items.front()).absolute_bounds().x > 300.0,
            "right to left, scrolling moves the first item past the right edge");
}

void test_table_layout_starts_at_the_right() {
    std::shared_ptr<TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("direction.table"));
    (*table).padding = {5.0, 5.0, 5.0, 5.0};
    (*table).set_column_count(2U);
    (*table).set_row_count(1U);
    (*table).set_column_style(0U, {TableSizeMode::absolute, 40.0});
    (*table).set_column_style(1U, {TableSizeMode::absolute, 60.0});
    (*table).set_row_style(0U, {TableSizeMode::absolute, 30.0});
    (*table).set_cell_border_style(TableCellBorderStyle::single);
    std::shared_ptr<Button> first = sized_button("direction.table.first", 30.0, 20.0);
    std::shared_ptr<Button> second = sized_button("direction.table.second", 50.0, 20.0);
    (*table).add_child(first);
    (*table).add_child(second);
    (*table).set_cell_position(*first, {0U, 0U});
    (*table).set_cell_position(*second, {1U, 0U});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{5.0, 5.0, 30.0, 20.0} &&
                (*second).arranged_bounds() == Rect{45.0, 5.0, 50.0, 20.0} &&
                (*table).parts().cell_grid.bounds == Rect{5.0, 5.0, 100.0, 30.0},
            "left to right, the table is unchanged");
    VerticalLinePainter left_to_right;
    (*table).on_paint(left_to_right, {0.0, 0.0, 200.0, 100.0});
    require(left_to_right.xs == std::vector<double>{5.0, 45.0, 105.0},
            "left to right, the column lines start at the left padding");

    (*table).layout_direction = LayoutDirection::right_to_left;
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{165.0, 5.0, 30.0, 20.0} &&
                (*second).arranged_bounds() == Rect{105.0, 5.0, 50.0, 20.0},
            "right to left, the first column is at the right and anchor left keeps a child to the cell's right");
    require((*table).parts().cell_grid.bounds == Rect{95.0, 5.0, 100.0, 30.0},
            "right to left, the cell grid ends at the right padding");
    VerticalLinePainter right_to_left;
    (*table).on_paint(right_to_left, {0.0, 0.0, 200.0, 100.0});
    require(right_to_left.xs == std::vector<double>{95.0, 155.0, 195.0},
            "right to left, the column lines are painted in mirrored order");
    require((*table).cell_position(*first) == std::optional(TableLayoutCellPosition{0U, 0U}) &&
                (*table).control_from_position(0U, 0U) == first &&
                (*table).control_from_position(1U, 0U) == second,
            "right to left, cell positions stay logical");
}

void test_reentrant_measure_keeps_the_outer_pass() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("direction.reentrant.flow"));
    (*flow).auto_size = true;
    std::shared_ptr<ReentrantMeasureControl> nested =
        make_control<ReentrantMeasureControl>(StableId("direction.reentrant.flow.nested"));
    (*nested).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
    (*nested).margin = {0.0, 0.0, 0.0, 0.0};
    (*nested).parent_panel = flow;
    std::shared_ptr<Button> after = sized_button("direction.reentrant.flow.after", 40.0, 20.0);
    (*flow).add_child(nested);
    (*flow).add_child(after);
    (*flow).flow_break[*after] = true;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("direction.reentrant.root"));
    (*root).add_child(flow);

    std::shared_ptr<TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("direction.reentrant.table"));
    (*table).auto_size = true;
    (*table).set_column_count(2U);
    std::shared_ptr<ReentrantMeasureControl> cell =
        make_control<ReentrantMeasureControl>(StableId("direction.reentrant.table.nested"));
    (*cell).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
    (*cell).margin = {0.0, 0.0, 0.0, 0.0};
    (*cell).parent_panel = table;
    std::shared_ptr<Button> neighbour = sized_button("direction.reentrant.table.after", 40.0, 20.0);
    (*table).add_child(cell);
    (*table).add_child(neighbour);
    (*table).set_requested_bounds({0.0, 100.0, 0.0, 0.0});
    (*root).add_child(table);

    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*nested).reentered && (*cell).reentered,
            "each panel must have been laid out inside its own pass");
    require((*flow).flow_break[*after].read() && (*after).arranged_bounds().x == (*nested).arranged_bounds().right(),
            "the flow's outer pass must keep its items after a nested pass");
    require((*table).cell_position(*cell) == std::optional(TableLayoutCellPosition{0U, 0U}) &&
                (*table).cell_position(*neighbour) == std::optional(TableLayoutCellPosition{1U, 0U}) &&
                (*neighbour).arranged_bounds().x == (*cell).arranged_bounds().right(),
            "the table's outer pass must keep its cells after a nested pass");
}

} // namespace

int main() {
    try {
        test_dock_anchor_and_requested_bounds_mirror();
        test_flow_layout_starts_at_the_right();
        test_flow_scrolls_toward_the_left();
        test_table_layout_starts_at_the_right();
        test_reentrant_measure_keeps_the_outer_pass();
        std::cout << "layout-direction-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "layout-direction-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
