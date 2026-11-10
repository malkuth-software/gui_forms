// Stage L, phase L3: FlexPanel over child controls. Item records and
// their validation and reconciliation, the solver's results published as
// child slots, nested intrinsic sizes, and the counts of PLAN section 5 at
// 10,000 children: changing one item's text costs one item measure
// callback, resizing costs none, and arranging allocates nothing.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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

FlexItemLayout grown(double grow) {
    FlexItemLayout layout{};
    layout.flex.grow = FlexFactor{grow};
    return layout;
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

void test_row_grows_and_stretches() {
    std::shared_ptr<FlexPanel> panel = make_control<FlexPanel>(StableId("flex.row"));
    (*panel).padding = {10.0, 5.0, 10.0, 5.0};
    (*panel).column_gap = LengthPercentage{LogicalLength{6.0}, 0.0};
    std::vector<std::shared_ptr<Panel>> boxes;
    for (int index = 0; index < 3; ++index) {
        boxes.push_back(box("flex.row." + std::to_string(index), 50.0, 20.0));
        (*panel).add_child(boxes.back());
    }
    (*panel).set_item_layout(*boxes[0], grown(1.0));
    (*panel).set_item_layout(*boxes[1], grown(1.0));
    (*panel).set_item_layout(*boxes[2], grown(2.0));
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{320.0, 60.0});
    Window& window = *held;
    window.perform_layout();
    // 300 px of content, 12 px of gaps, 150 px of bases: 138 px to share.
    require_bounds(*boxes[0], 10.0, 5.0, 84.5, 50.0, "first grows a quarter and stretches");
    require_bounds(*boxes[1], 100.5, 5.0, 84.5, 50.0, "second");
    require_bounds(*boxes[2], 191.0, 5.0, 119.0, 50.0, "third grows a half");
    const FlexLayoutWorkspace& snapshot = (*panel).layout_snapshot();
    require(snapshot.items().size() == 3U && snapshot.lines().size() == 1U, "the snapshot has the items and a line");
    require((*panel).laid_out_children()[2] == (*boxes[2]).runtime_id(), "the snapshot names its children");
    require(near(snapshot.items()[2].flex_base_size, 50.0), "the base size is the box's content size");
}

void test_item_records_validate_and_reconcile() {
    std::shared_ptr<FlexPanel> panel = make_control<FlexPanel>(StableId("flex.records"));
    std::shared_ptr<Panel> child = box("flex.records.child", 10.0, 10.0);
    std::shared_ptr<Panel> stranger = box("flex.records.stranger", 10.0, 10.0);
    (*panel).add_child(child);
    bool refused = false;
    try {
        (*panel).set_item_layout(*stranger, grown(1.0));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a record needs a direct child");
    refused = false;
    try {
        (*panel).set_item_layout(*child, grown(-1.0));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a negative grow factor is refused");
    refused = false;
    try {
        FlexItemLayout layout{};
        layout.max_width = SizeValue{};
        (*panel).set_item_layout(*child, layout);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "max-width: auto is refused");
    (*panel).set_item_layout(*child, grown(3.0));
    require((*panel).item_layout(*child).flex.grow == FlexFactor{3.0}, "the record is kept");
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{100.0, 40.0});
    Window& window = *held;
    window.perform_layout();
    static_cast<void>((*panel).remove_child((*child).runtime_id()));
    window.perform_layout();
    (*panel).add_child(child);
    require((*panel).item_layout(*child) == FlexItemLayout{}, "a departed child's record is dropped");
}

void test_nested_columns_size_from_content() {
    std::shared_ptr<FlexPanel> column = make_control<FlexPanel>(StableId("flex.column"));
    (*column).flex_direction = FlexDirection::column;
    (*column).row_gap = LengthPercentage{LogicalLength{4.0}, 0.0};
    (*column).align_items = LayoutAlignment::flex_start;
    std::shared_ptr<FlexPanel> inner = make_control<FlexPanel>(StableId("flex.column.row"));
    std::shared_ptr<Panel> first = box("flex.column.row.a", 30.0, 12.0);
    std::shared_ptr<Panel> second = box("flex.column.row.b", 40.0, 18.0);
    (*inner).add_child(first);
    (*inner).add_child(second);
    (*inner).margin = {0.0, 0.0, 0.0, 0.0};
    std::shared_ptr<Panel> after = box("flex.column.after", 25.0, 10.0);
    (*column).add_child(inner);
    (*column).add_child(after);
    const std::unique_ptr<Window> held = std::make_unique<Window>(column, Size{200.0, 100.0});
    Window& window = *held;
    window.perform_layout();
    require_bounds(*inner, 0.0, 0.0, 70.0, 18.0, "the inner row is as wide and tall as its content");
    require_bounds(*first, 0.0, 0.0, 30.0, 18.0, "its items stretch to its height");
    require_bounds(*after, 0.0, 22.0, 25.0, 10.0, "the next item follows after the gap");
}

// The docstring's two examples, with the results their comments state.
void test_documented_examples() {
    std::shared_ptr<FlexPanel> row = make_control<FlexPanel>(StableId("doc.row"));
    std::shared_ptr<Button> fixed = make_control<Button>(StableId("doc.row.fixed"), "Fixed");
    std::shared_ptr<Button> narrow = make_control<Button>(StableId("doc.row.narrow"), "Narrow");
    std::shared_ptr<Button> wide = make_control<Button>(StableId("doc.row.wide"), "Wide");
    (*fixed).set_requested_bounds({0.0, 0.0, 20.0, 10.0});
    (*narrow).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
    (*wide).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
    (*fixed).margin = Insets{};
    (*narrow).margin = Insets{};
    (*wide).margin = Insets{};
    (*row).add_child(fixed);
    (*row).add_child(narrow);
    (*row).add_child(wide);
    (*row).set_item_layout(*narrow, grown(1.0));
    (*row).set_item_layout(*wide, grown(2.0));
    const std::unique_ptr<Window> row_window = std::make_unique<Window>(row, Size{100.0, 20.0});
    (*row_window).perform_layout();
    require_bounds(*fixed, 0.0, 0.0, 20.0, 20.0, "the fixed button");
    require_bounds(*narrow, 20.0, 0.0, 30.0, 20.0, "the one-share button");
    require_bounds(*wide, 50.0, 0.0, 50.0, 20.0, "the two-share button");

    std::shared_ptr<FlexPanel> bar = make_control<FlexPanel>(StableId("doc.bar"));
    (*bar).align_items = LayoutAlignment::center;
    std::shared_ptr<Button> save = make_control<Button>(StableId("doc.bar.save"), "Save");
    std::shared_ptr<Button> close = make_control<Button>(StableId("doc.bar.close"), "Close");
    (*save).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
    (*close).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
    (*save).margin = Insets{};
    (*close).margin = Insets{};
    (*bar).add_child(save);
    (*bar).add_child(close);
    FlexItemLayout pushed{};
    pushed.auto_margin.left = true;
    (*bar).set_item_layout(*close, pushed);
    const std::unique_ptr<Window> bar_window = std::make_unique<Window>(bar, Size{200.0, 30.0});
    (*bar_window).perform_layout();
    require_bounds(*save, 0.0, 5.0, 40.0, 20.0, "the save button");
    require_bounds(*close, 160.0, 5.0, 40.0, 20.0, "the pushed close button");
}

void test_ten_thousand_children_counts() {
    constexpr std::size_t count = 10'000U;
    std::shared_ptr<FlexPanel> panel = make_control<FlexPanel>(StableId("flex.many"));
    (*panel).flex_wrap = FlexWrap::wrap;
    (*panel).align_items = LayoutAlignment::flex_start;
    FlexItemLayout rigid{};
    rigid.flex.shrink = FlexFactor{0.0};
    std::vector<std::shared_ptr<CountingLabel>> labels;
    labels.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        std::shared_ptr<CountingLabel> label =
            make_control<CountingLabel>(StableId("flex.many." + std::to_string(index)));
        (*label).text = "Item " + std::to_string(index);
        (*panel).add_child(label);
        (*panel).set_item_layout(*label, rigid);
        labels.push_back(std::move(label));
    }
    // The panel sits in a plain root, so resizing the container (not the
    // window) is a bounds change on the panel alone.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("flex.many.root"));
    (*panel).set_bounds({0.0, 0.0, 1200.0, 800.0});
    (*root).add_child(panel);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1200.0, 800.0});
    Window& window = *held;
    window.perform_layout();
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    window.reset_activity_metrics();

    // One item's text: its one content query misses, and so do the
    // panel's own measure and the plain root's above it (Panel keeps the
    // default propagation); every other item answers from its cache.
    (*labels[4'321]).text = "A rather longer item text";
    window.perform_layout();
    std::uint64_t callbacks = 0U;
    for (const std::shared_ptr<CountingLabel>& label : labels) callbacks += (*label).callbacks;
    require(callbacks == 1U, "changing one item's text costs one item measure callback, got " +
                                 std::to_string(callbacks));
    require((*labels[4'321]).callbacks == 1U, "the callback is the changed item's");
    const MetricsSnapshot changed = window.metrics_snapshot();
    require(changed.measure_cache_misses == 3U, "the item, the panel and the root miss, got " +
                                                    std::to_string(changed.measure_cache_misses));

    // Resizing the panel changes no item's constraint: every content query
    // is answered from the cache, though the lines wrap differently.
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    (*panel).set_bounds({0.0, 0.0, 900.0, 700.0});
    window.perform_layout();
    callbacks = 0U;
    for (const std::shared_ptr<CountingLabel>& label : labels) callbacks += (*label).callbacks;
    require(callbacks == 0U, "resizing costs no item measure callback, got " + std::to_string(callbacks));
    require(near((*panel).arranged_bounds().width, 900.0), "the panel took its new width");

    // Arranging again at the same size allocates nothing.
    const Rect bounds = (*panel).arranged_bounds();
    (*panel).arrange(bounds);
    counted_allocations = 0U;
    counting_allocations = true;
    (*panel).arrange(bounds);
    counting_allocations = false;
    require(counted_allocations == 0U, "arranging 10000 children allocated " + std::to_string(counted_allocations) +
                                           " times");
    std::cout << "flex-panel-tests: 10000 children: 1 callback on a text change, 0 on a resize, "
                 "0 allocations per arrange\n";
}

} // namespace


// The `style` property and the `item_style` child property (study 10,
// section 6): CSS inline styles read by the layout style reader, read back
// in the reader's spelling, and refused whole for any declaration the panel
// does not carry.
void test_style_properties() {
    std::shared_ptr<FlexPanel> flex = make_control<FlexPanel>(StableId("flex.style"));
    std::shared_ptr<Button> item = make_control<Button>(StableId("flex.style.item"), "Item");
    (*flex).add_child(item);
    (*flex).set_property_value("style", BindingValue{std::string{
        "flex-direction: column; flex-wrap: wrap; align-items: center; row-gap: 6px"}});
    require((*flex).flex_direction.read() == FlexDirection::column && (*flex).flex_wrap.read() == FlexWrap::wrap &&
                (*flex).align_items.read() == LayoutAlignment::center &&
                (*flex).row_gap.read() == LengthPercentage{LogicalLength{6.0}, 0.0},
            "flex style: direction, wrap, alignment and gap applied");
    const std::optional<BindingValue> text = (*flex).property_value("style");
    std::shared_ptr<FlexPanel> copy = make_control<FlexPanel>(StableId("flex.style.copy"));
    require(text.has_value(), "flex style: reads back");
    (*copy).set_property_value("style", *text);
    require((*copy).flex_direction.read() == FlexDirection::column && (*copy).flex_wrap.read() == FlexWrap::wrap &&
                (*copy).align_items.read() == LayoutAlignment::center && (*copy).row_gap.read() == (*flex).row_gap.read(),
            "flex style: the text read back gives the same settings");
    bool item_field_refused = false;
    try {
        (*flex).set_property_value("style", BindingValue{std::string{"flex-grow: 1"}});
    } catch (const std::invalid_argument&) {
        item_field_refused = true;
    }
    bool error_refused = false;
    try {
        (*flex).set_property_value("style", BindingValue{std::string{"flex-direction: sideways"}});
    } catch (const std::invalid_argument&) {
        error_refused = true;
    }
    require(item_field_refused && error_refused && (*flex).flex_direction.read() == FlexDirection::column,
            "flex style: an item field and a bad value are refused and change nothing");
    (*flex).set_child_property_value(*item, "item_style", BindingValue{std::string{"flex-grow: 2; margin-left: auto"}});
    const FlexItemLayout layout = (*flex).item_layout(*item);
    require(layout.flex.grow == FlexFactor{2.0} && layout.auto_margin.left, "flex item style: grow and auto margin");
    const std::optional<BindingValue> item_text = (*flex).child_property_value(*item, "item_style");
    require(item_text.has_value() && std::get<std::string>(*item_text).find("flex-grow: 2") != std::string::npos,
            "flex item style: reads back as text");
    bool margin_refused = false;
    try {
        (*flex).set_child_property_value(*item, "item_style", BindingValue{std::string{"margin-top: 3px"}});
    } catch (const std::invalid_argument&) {
        margin_refused = true;
    }
    require(margin_refused && (*flex).item_layout(*item) == layout,
            "flex item style: a margin length is refused and changes nothing");
}

// The auto_scroll extent is every item's margin box: lines wrapped below
// (or beside) the viewport, a nowrap line running past it, and content a
// reversed axis pushes past the content box's start, which moves inside.
struct ScrollCase final {
    std::shared_ptr<FlexPanel> panel;
    std::vector<std::shared_ptr<Panel>> boxes;
    std::unique_ptr<Window> window;
};

ScrollCase scroll_case(const std::string& id, Size panel_size, Size box_size, int count, FlexDirection direction,
                       FlexWrap wrap) {
    ScrollCase made{};
    made.panel = make_control<FlexPanel>(StableId(id));
    (*made.panel).padding = {0.0, 0.0, 0.0, 0.0};
    (*made.panel).flex_direction = direction;
    (*made.panel).flex_wrap = wrap;
    (*made.panel).auto_scroll = true;
    FlexItemLayout rigid{};
    rigid.flex.shrink = FlexFactor{0.0};
    for (int index = 0; index < count; ++index) {
        made.boxes.push_back(box(id + "." + std::to_string(index), box_size.width, box_size.height));
        (*made.panel).add_child(made.boxes.back());
        (*made.panel).set_item_layout(*made.boxes.back(), rigid);
    }
    made.window = std::make_unique<Window>(made.panel, panel_size);
    (*made.window).perform_layout();
    return made;
}

void require_extent(const FlexPanel& panel, double width, double height, const std::string& what) {
    const Rect display = panel.scroll_snapshot().display_rectangle;
    require(near(display.width, width) && near(display.height, height),
            what + ": scroll extent " + describe(display));
}

void test_auto_scroll_extent_covers_all_content() {
    // Row, wrap: two per line (the vertical bar leaves 84), five lines.
    const ScrollCase rows =
        scroll_case("flex.scroll.rows", {100.0, 60.0}, {40.0, 20.0}, 10, FlexDirection::row, FlexWrap::wrap);
    require_extent(*rows.panel, 84.0, 100.0, "wrapped rows reach below the viewport");
    require((*rows.panel).scroll_snapshot().vertical.visible && !(*rows.panel).scroll_snapshot().horizontal.visible,
            "wrapped rows: only the vertical bar");
    require_bounds(*rows.boxes[9], 40.0, 80.0, 40.0, 20.0, "wrapped rows: the last item on the fifth line");

    // Column, wrap: two per column (the horizontal bar leaves 84), five
    // columns.
    const ScrollCase columns =
        scroll_case("flex.scroll.columns", {60.0, 100.0}, {20.0, 40.0}, 10, FlexDirection::column, FlexWrap::wrap);
    require_extent(*columns.panel, 100.0, 84.0, "wrapped columns reach right of the viewport");
    require_bounds(*columns.boxes[9], 80.0, 40.0, 20.0, 40.0, "wrapped columns: the last item in the fifth column");

    // Row-reverse, nowrap: the line runs left past the content box's start.
    const ScrollCase reversed =
        scroll_case("flex.scroll.row-reverse", {100.0, 60.0}, {40.0, 20.0}, 4, FlexDirection::row_reverse,
                    FlexWrap::nowrap);
    require_extent(*reversed.panel, 160.0, 44.0, "a reversed row's overflow is scrollable");
    // It opens on the main-axis start (CSS's scroll origin): scrolled by the
    // 60 units moved inside, so the first item stands at the right edge.
    require(near((*reversed.panel).scroll_position().x, 60.0), "a reversed row opens on its main-axis start");
    require(near((*reversed.boxes[0]).arranged_bounds().x, 60.0) && near((*reversed.boxes[3]).arranged_bounds().x, -60.0),
            "a reversed row: the first item at the viewport's right edge, the overflow left of it");
    // A scroll the user makes survives the next layout (the shift is
    // unchanged, so the position is not moved again).
    static_cast<void>((*reversed.panel).scroll_to(Point{0.0, 0.0}));
    (*reversed.window).perform_layout();
    require(near((*reversed.panel).scroll_position().x, 0.0) && near((*reversed.boxes[3]).arranged_bounds().x, 0.0),
            "a reversed row keeps the user's scroll across a layout");

    // Column-reverse, nowrap: the line runs up past the start.
    const ScrollCase upward =
        scroll_case("flex.scroll.column-reverse", {60.0, 100.0}, {20.0, 40.0}, 4, FlexDirection::column_reverse,
                    FlexWrap::nowrap);
    require_extent(*upward.panel, 44.0, 160.0, "a reversed column's overflow is scrollable");
    require(near((*upward.panel).scroll_position().y, 60.0) && near((*upward.boxes[0]).arranged_bounds().y, 60.0) &&
                near((*upward.boxes[3]).arranged_bounds().y, -60.0),
            "a reversed column opens on its main-axis start, the first item at the bottom edge");

    // Row, wrap-reverse: lines stack upward past the start.
    const ScrollCase stacked =
        scroll_case("flex.scroll.wrap-reverse", {100.0, 60.0}, {40.0, 20.0}, 10, FlexDirection::row,
                    FlexWrap::wrap_reverse);
    require_extent(*stacked.panel, 84.0, 100.0, "wrap-reverse lines are all scrollable");
    require(near((*stacked.panel).scroll_position().y, 40.0) && near((*stacked.boxes[0]).arranged_bounds().y, 40.0) &&
                near((*stacked.boxes[9]).arranged_bounds().y, -40.0),
            "wrap-reverse opens on the cross-start: the first line at the viewport's bottom, the last above it");

    // Without auto_scroll nothing moves and the extent is the client.
    std::shared_ptr<FlexPanel> plain = make_control<FlexPanel>(StableId("flex.scroll.off"));
    (*plain).padding = {0.0, 0.0, 0.0, 0.0};
    (*plain).flex_wrap = FlexWrap::wrap;
    std::vector<std::shared_ptr<Panel>> items;
    for (int index = 0; index < 10; ++index) {
        items.push_back(box("flex.scroll.off." + std::to_string(index), 40.0, 20.0));
        (*plain).add_child(items.back());
    }
    Window plain_window(plain, Size{100.0, 60.0});
    plain_window.perform_layout();
    require_extent(*plain, 100.0, 60.0, "without auto_scroll the extent is the client");
    require_bounds(*items[9], 40.0, 80.0, 40.0, 20.0, "without auto_scroll the items still wrap below");
}

int main() {
    try {
        test_row_grows_and_stretches();
        test_style_properties();
        test_item_records_validate_and_reconcile();
        test_nested_columns_size_from_content();
        test_documented_examples();
        test_ten_thousand_children_counts();
        test_auto_scroll_extent_covers_all_content();
        std::cout << "flex-panel-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "flex-panel-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
