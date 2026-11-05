// Stage L, phase L5: BlockPanel over child controls. Block stacking
// with margins collapsing between siblings and through a nested panel,
// widths and auto margins, line boxes with text-align and the strut,
// shrink-to-fit inline panels, records and their validation and
// reconciliation, and the counts of PLAN section 5 at 10,000 children:
// changing one item's text costs one item measure callback, a height change
// costs none, and arranging allocates nothing.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Counts global allocations while armed, so the arrange loop can be shown
// to allocate nothing (PLAN section 5).
namespace {
bool counting_allocations = false;
std::uint64_t counted_allocations = 0U;
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void* operator new[](std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) <= 1.0e-9;
}

std::string describe(Rect rect) {
    return "{" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " + std::to_string(rect.width) + ", " +
           std::to_string(rect.height) + "}";
}

void require_bounds(const Control& control, double x, double y, double width, double height,
                    const std::string& what) {
    const Rect bounds = control.arranged_bounds();
    const bool same =
        near(bounds.x, x) && near(bounds.y, y) && near(bounds.width, width) && near(bounds.height, height);
    require(same, what + ": got " + describe(bounds));
}

std::shared_ptr<Panel> box(const std::string& id, double width, double height) {
    std::shared_ptr<Panel> made = make_control<Panel>(StableId(id));
    (*made).set_bounds({0.0, 0.0, width, height});
    (*made).margin = {0.0, 0.0, 0.0, 0.0};
    return made;
}

std::shared_ptr<BlockPanel> flow(const std::string& id) {
    std::shared_ptr<BlockPanel> made = make_control<BlockPanel>(StableId(id));
    (*made).margin = {0.0, 0.0, 0.0, 0.0};
    return made;
}

SizeValue px(double value) {
    const SizeValue size{SizeKind::length_percentage, LengthPercentage{LogicalLength{value}, 0.0}};
    return size;
}

SizeValue percent(double value) {
    const SizeValue size{SizeKind::length_percentage, LengthPercentage{LogicalLength{}, value}};
    return size;
}

// The margin setter's order is left, top, right, bottom (Insets).
void set_vertical_margins(Control& control, double top, double bottom) {
    control.margin = {0.0, top, 0.0, bottom};
}

BlockFlowItemLayout inline_item(double width, double height) {
    BlockFlowItemLayout layout{};
    layout.level = BlockFlowLevel::inline_level;
    layout.width = px(width);
    layout.height = px(height);
    return layout;
}

bool refuses_item(BlockPanel& panel, const Control& child, const BlockFlowItemLayout& layout) {
    try {
        panel.set_item_layout(child, layout);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Chromium's probe of block margins: siblings, a nested block collapsing
// with its child's margins on both sides, an empty block. (A control's
// margins are nonnegative; the solver's tests cover negative ones.)
void test_stacking_and_margin_collapsing() {
    std::shared_ptr<BlockPanel> root = flow("block.collapse");
    (*root).padding = {8.0, 8.0, 8.0, 8.0};
    std::shared_ptr<Panel> first = box("block.collapse.a", 0.0, 20.0);
    set_vertical_margins(*first, 0.0, 10.0);
    std::shared_ptr<Panel> second = box("block.collapse.b", 0.0, 20.0);
    set_vertical_margins(*second, 30.0, 0.0);
    std::shared_ptr<BlockPanel> nested = flow("block.collapse.p");
    set_vertical_margins(*nested, 10.0, 0.0);
    std::shared_ptr<Panel> inner = box("block.collapse.c", 0.0, 20.0);
    set_vertical_margins(*inner, 25.0, 15.0);
    (*nested).add_child(inner);
    std::shared_ptr<Panel> after = box("block.collapse.d", 0.0, 20.0);
    set_vertical_margins(*after, 5.0, 0.0);
    std::shared_ptr<Panel> empty = box("block.collapse.e", 0.0, 0.0);
    set_vertical_margins(*empty, 20.0, 5.0);
    std::shared_ptr<Panel> last = box("block.collapse.f", 0.0, 20.0);
    set_vertical_margins(*last, 8.0, 12.0);
    std::shared_ptr<Panel> pulled = box("block.collapse.g", 0.0, 20.0);
    set_vertical_margins(*pulled, 4.0, 0.0);
    (*root).add_child(first);
    (*root).add_child(second);
    (*root).add_child(nested);
    (*root).add_child(after);
    (*root).add_child(empty);
    (*root).add_child(last);
    (*root).add_child(pulled);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{800.0, 600.0});
    (*held).perform_layout();
    require_bounds(*first, 8.0, 8.0, 784.0, 20.0, "a block fills the width");
    require_bounds(*second, 8.0, 58.0, 784.0, 20.0, "10 and 30 collapse to 30");
    require_bounds(*nested, 8.0, 103.0, 784.0, 20.0, "the nested block takes its child's 25 top margin");
    require_bounds(*inner, 0.0, 0.0, 784.0, 20.0, "its child sits at its top");
    require_bounds(*after, 8.0, 138.0, 784.0, 20.0, "the child's 15 bottom margin passes through");
    require_bounds(*empty, 8.0, 178.0, 784.0, 0.0, "the empty block after its top margin");
    require_bounds(*last, 8.0, 178.0, 784.0, 20.0, "20, 5 and 8 collapse to 20");
    require_bounds(*pulled, 8.0, 210.0, 784.0, 20.0, "12 and 4 collapse to 12");
    const BlockFlowLayoutWorkspace& snapshot = (*root).layout_snapshot();
    require(snapshot.items().size() == 7U && snapshot.lines().empty(), "the snapshot has the blocks");
    require((*root).laid_out_children()[2] == (*nested).runtime_id(), "the snapshot names its children");
    require((*root).layout_outcome() == BlockFlowSolveOutcome::solved, "the layout solved");

    // Top padding separates the nested block's margin from its child's.
    (*nested).padding = {0.0, 2.0, 0.0, 0.0};
    (*held).perform_layout();
    require_bounds(*nested, 8.0, 88.0, 784.0, 47.0, "with padding the nested block keeps its own margin");
    require_bounds(*inner, 0.0, 27.0, 784.0, 20.0, "and its child's margin is inside it");
}

void test_widths_and_auto_margins() {
    std::shared_ptr<BlockPanel> root = flow("block.widths");
    (*root).padding = {8.0, 8.0, 8.0, 8.0};
    std::shared_ptr<Panel> centred = box("block.widths.a", 0.0, 10.0);
    std::shared_ptr<Panel> half = box("block.widths.b", 0.0, 10.0);
    std::shared_ptr<Panel> pushed = box("block.widths.c", 0.0, 10.0);
    (*pushed).margin = {0.0, 0.0, 40.0, 0.0};
    (*root).add_child(centred);
    (*root).add_child(half);
    (*root).add_child(pushed);
    BlockFlowItemLayout narrow{};
    narrow.max_width = px(100.0);
    narrow.auto_margin.left = true;
    narrow.auto_margin.right = true;
    (*root).set_item_layout(*centred, narrow);
    BlockFlowItemLayout fraction{};
    fraction.width = percent(50.0);
    fraction.auto_margin.left = true;
    fraction.auto_margin.right = true;
    (*root).set_item_layout(*half, fraction);
    BlockFlowItemLayout right{};
    right.width = px(100.0);
    right.auto_margin.left = true;
    (*root).set_item_layout(*pushed, right);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{800.0, 600.0});
    (*held).perform_layout();
    require_bounds(*centred, 350.0, 8.0, 100.0, 10.0, "a maximum width centred by auto margins");
    require_bounds(*half, 204.0, 18.0, 392.0, 10.0, "half the width, centred");
    require_bounds(*pushed, 652.0, 28.0, 100.0, 10.0, "an auto left margin pushes it right");

    (*root).layout_direction = LayoutDirection::right_to_left;
    BlockFlowItemLayout plain{};
    plain.width = px(100.0);
    (*root).set_item_layout(*pushed, plain);
    (*held).perform_layout();
    require_bounds(*pushed, 652.0, 28.0, 100.0, 10.0, "right to left keeps the right margin");
}

void test_lines_and_shrink_to_fit() {
    std::shared_ptr<BlockPanel> root = flow("block.lines");
    std::shared_ptr<BlockPanel> line = flow("block.lines.w");
    (*line).set_text_align(TextAlignment::center);
    // A zero strut, so a line is as tall as its items (unset, the strut
    // would come from the panel's font).
    (*line).set_line_strut(LineStrut{});
    (*root).add_child(line);
    BlockFlowItemLayout narrow{};
    narrow.width = px(100.0);
    (*root).set_item_layout(*line, narrow);
    std::vector<std::shared_ptr<Panel>> boxes;
    for (int index = 0; index < 3; ++index) {
        boxes.push_back(box("block.lines.w." + std::to_string(index), 0.0, 0.0));
        (*line).add_child(boxes.back());
        (*line).set_item_layout(*boxes.back(), inline_item(40.0, 10.0));
    }
    // An inline-block panel shrinks to its content.
    std::shared_ptr<BlockPanel> fitted = flow("block.lines.fit");
    std::shared_ptr<Panel> content = box("block.lines.fit.content", 0.0, 0.0);
    (*fitted).add_child(content);
    BlockFlowItemLayout content_layout{};
    content_layout.width = px(30.0);
    content_layout.height = px(10.0);
    (*fitted).set_item_layout(*content, content_layout);
    (*root).add_child(fitted);
    BlockFlowItemLayout atomic{};
    atomic.level = BlockFlowLevel::inline_level;
    (*root).set_item_layout(*fitted, atomic);
    (*root).set_line_strut(LineStrut{});

    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{800.0, 600.0});
    (*held).perform_layout();
    require_bounds(*line, 0.0, 0.0, 100.0, 20.0, "two lines of ten");
    require_bounds(*boxes[0], 10.0, 0.0, 40.0, 10.0, "the first line centred");
    require_bounds(*boxes[2], 30.0, 10.0, 40.0, 10.0, "the second line centred");
    require((*line).layout_snapshot().lines().size() == 2U, "the snapshot has the lines");
    require_bounds(*fitted, 0.0, 20.0, 30.0, 10.0, "an inline panel shrinks to fit its content");

    // A strut of 10 above and below: each line is 20 tall.
    (*line).set_line_strut(LineStrut{10.0, 10.0, 0.0});
    (*held).perform_layout();
    require_bounds(*line, 0.0, 0.0, 100.0, 40.0, "the strut sets each line's height");
    require_bounds(*boxes[2], 30.0, 20.0, 40.0, 10.0, "the item sits on the baseline");
    bool refused = false;
    try {
        (*line).set_line_strut(LineStrut{-1.0, 0.0, 0.0});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a negative strut is refused");
}

void test_records_validate_and_reconcile() {
    std::shared_ptr<BlockPanel> panel = flow("block.records");
    std::shared_ptr<Panel> child = box("block.records.child", 10.0, 10.0);
    std::shared_ptr<Panel> stranger = box("block.records.stranger", 10.0, 10.0);
    (*panel).add_child(child);
    const BlockFlowItemLayout atomic = inline_item(10.0, 10.0);
    require(refuses_item(*panel, *stranger, atomic), "a record needs a direct child");
    BlockFlowItemLayout keyword{};
    keyword.width = SizeValue{SizeKind::min_content, {}};
    require(refuses_item(*panel, *child, keyword), "a content keyword width is refused");
    BlockFlowItemLayout automatic_maximum{};
    automatic_maximum.max_height = SizeValue{};
    require(refuses_item(*panel, *child, automatic_maximum), "an auto maximum is refused");
    BlockFlowItemLayout negative{};
    negative.height = px(-1.0);
    require(refuses_item(*panel, *child, negative), "a negative height is refused");

    (*panel).set_item_layout(*child, atomic);
    require((*panel).item_layout(*child) == atomic, "the record is kept");
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{100.0, 40.0});
    Window& window = *held;
    window.perform_layout();
    static_cast<void>((*panel).remove_child((*child).runtime_id()));
    window.perform_layout();
    (*panel).add_child(child);
    require((*panel).item_layout(*child) == BlockFlowItemLayout{}, "a departed child's record is dropped");
}

// A nested panel shares margins only as a block item: made inline, it is a
// formatting-context root again, and its cached measures are refreshed.
void test_level_change_refreshes_nested_panel() {
    std::shared_ptr<BlockPanel> root = flow("block.level");
    std::shared_ptr<BlockPanel> nested = flow("block.level.p");
    std::shared_ptr<Panel> inner = box("block.level.c", 0.0, 20.0);
    set_vertical_margins(*inner, 25.0, 0.0);
    BlockFlowItemLayout sized{};
    sized.width = px(50.0);
    (*nested).add_child(inner);
    (*root).add_child(nested);
    (*root).set_item_layout(*nested, sized);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{200.0, 200.0});
    (*held).perform_layout();
    require_bounds(*nested, 0.0, 25.0, 50.0, 20.0, "as a block it collapses with its child");
    BlockFlowItemLayout atomic = sized;
    atomic.level = BlockFlowLevel::inline_level;
    (*root).set_item_layout(*nested, atomic);
    (*held).perform_layout();
    require_bounds(*nested, 0.0, 0.0, 50.0, 45.0, "inline, it holds its child's margin");
    require_bounds(*inner, 0.0, 25.0, 50.0, 20.0, "and its child sits below it");
}

// The docstring's two examples, with the results their comments state.
void test_documented_examples() {
    std::shared_ptr<BlockPanel> stack = make_control<BlockPanel>(StableId("doc.stack"));
    std::vector<std::shared_ptr<Button>> buttons;
    for (int index = 0; index < 3; ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId("doc.stack.button" + std::to_string(index)), "Button");
        (*button).set_requested_bounds({0.0, 0.0, 10.0, 20.0});
        (*button).margin = {0.0, 10.0, 0.0, 10.0};
        (*stack).add_child(button);
        buttons.push_back(button);
    }
    const std::unique_ptr<Window> stack_window = std::make_unique<Window>(stack, Size{200.0, 100.0});
    (*stack_window).perform_layout();
    require_bounds(*buttons[0], 0.0, 10.0, 200.0, 20.0, "the first stacked button");
    require_bounds(*buttons[1], 0.0, 40.0, 200.0, 20.0, "the second stacked button");
    require_bounds(*buttons[2], 0.0, 70.0, 200.0, 20.0, "the third stacked button");

    std::shared_ptr<BlockPanel> row = make_control<BlockPanel>(StableId("doc.row"));
    (*row).set_text_align(TextAlignment::center);
    (*row).set_line_strut(LineStrut{});
    const BlockFlowItemLayout atomic = inline_item(80.0, 20.0);
    std::vector<std::shared_ptr<Button>> row_buttons;
    for (int index = 0; index < 3; ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId("doc.row.button" + std::to_string(index)), "Button");
        (*button).margin = Insets{};
        (*row).add_child(button);
        (*row).set_item_layout(*button, atomic);
        row_buttons.push_back(button);
    }
    const std::unique_ptr<Window> row_window = std::make_unique<Window>(row, Size{200.0, 60.0});
    (*row_window).perform_layout();
    require_bounds(*row_buttons[0], 20.0, 0.0, 80.0, 20.0, "the first inline button");
    require_bounds(*row_buttons[1], 100.0, 0.0, 80.0, 20.0, "the second inline button");
    require_bounds(*row_buttons[2], 60.0, 20.0, 80.0, 20.0, "the wrapped inline button");
}

// A label that counts its measure callbacks.
class CountingLabel final : public Label {
public:
    explicit CountingLabel(StableId id) : Label(std::move(id)) {}

    std::uint64_t callbacks{};

protected:
    Size measure_intrinsic(const LayoutConstraint& constraint) override {
        ++callbacks;
        const Size measured = Label::measure_intrinsic(constraint);
        return measured;
    }
};

void test_ten_thousand_children_counts() {
    constexpr std::size_t count = 10'000U;
    std::shared_ptr<BlockPanel> panel = flow("block.many");
    std::vector<std::shared_ptr<CountingLabel>> labels;
    labels.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        std::shared_ptr<CountingLabel> label =
            make_control<CountingLabel>(StableId("block.many." + std::to_string(index)));
        (*label).set_text("Item " + std::to_string(index));
        (*panel).add_child(label);
        labels.push_back(std::move(label));
    }
    // The panel sits in a plain root, so resizing the container (not the
    // window) is a bounds change on the panel alone.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("block.many.root"));
    (*panel).set_bounds({0.0, 0.0, 1200.0, 800.0});
    (*root).add_child(panel);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1200.0, 800.0});
    Window& window = *held;
    window.perform_layout();
    require((*panel).layout_snapshot().items().size() == count, "every label is laid out");
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    window.reset_activity_metrics();

    // One item's text: each block item is asked only its height at the
    // panel's width. The changed item's query misses, and so do the panel's
    // own measure and the plain root's above it.
    (*labels[4'321]).set_text("A rather longer item text");
    window.perform_layout();
    std::uint64_t callbacks = 0U;
    for (const std::shared_ptr<CountingLabel>& label : labels) callbacks += (*label).callbacks;
    require(callbacks == 1U, "changing one item's text costs one item measure callback, got " +
                                 std::to_string(callbacks));
    require((*labels[4'321]).callbacks == 1U, "the callback is the changed item's");
    const MetricsSnapshot changed = window.metrics_snapshot();
    require(changed.measure_cache_misses == 3U, "the item, the panel and the root miss, got " +
                                                    std::to_string(changed.measure_cache_misses));

    // A height change leaves every item's constraint as it was.
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    (*panel).set_bounds({0.0, 0.0, 1200.0, 700.0});
    window.perform_layout();
    callbacks = 0U;
    for (const std::shared_ptr<CountingLabel>& label : labels) callbacks += (*label).callbacks;
    require(callbacks == 0U, "resizing costs no item measure callback, got " + std::to_string(callbacks));

    // Arranging again at the same size allocates nothing.
    const Rect bounds = (*panel).arranged_bounds();
    (*panel).arrange(bounds);
    counted_allocations = 0U;
    counting_allocations = true;
    (*panel).arrange(bounds);
    counting_allocations = false;
    require(counted_allocations == 0U, "arranging 10000 children allocated " + std::to_string(counted_allocations) +
                                           " times");
    std::cout << "block-panel-tests: 10000 children: 1 callback on a text change, 0 on a resize, "
                 "0 allocations per arrange\n";
}

} // namespace

int main() {
    try {
        test_stacking_and_margin_collapsing();
        test_widths_and_auto_margins();
        test_lines_and_shrink_to_fit();
        test_records_validate_and_reconcile();
        test_level_change_refreshes_nested_panel();
        test_documented_examples();
        test_ten_thousand_children_counts();
        std::cout << "block-panel-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "block-panel-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
