// Stage L, phase L4: GridPanel over child controls. Track lists,
// areas and item records and their validation and reconciliation, the
// solver's results published as child slots, nested intrinsic sizes, the
// cell limit's refusal, spans and the two views of a placement (study 10.9),
// and the counts of PLAN section 5 at 10,000
// children: changing one item's text costs one item measure callback,
// resizing costs none, and arranging allocates nothing.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
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

LengthPercentage px(double value) {
    const LengthPercentage length{LogicalLength{value}, 0.0};
    return length;
}

GridTrackSize fixed(double value) {
    GridTrackSize size{};
    size.minimum = TrackBreadth{TrackBreadthKind::length_percentage, px(value), FractionFactor{}};
    size.maximum = size.minimum;
    return size;
}

GridTrackSize fraction(double value) {
    GridTrackSize size{};
    size.minimum = TrackBreadth{};
    size.maximum = TrackBreadth{TrackBreadthKind::fraction, {}, FractionFactor{value}};
    return size;
}

GridTrackSize automatic() {
    const GridTrackSize size{};
    return size;
}

GridTrackList tracks(std::vector<GridTrackSize> sizes) {
    GridTrackList list{};
    list.tracks = std::move(sizes);
    return list;
}

GridLine line(std::int32_t number) {
    GridLine made{};
    made.kind = GridLineKind::line;
    made.number = number;
    return made;
}

GridLine span(std::int32_t count) {
    GridLine made{};
    made.kind = GridLineKind::span;
    made.number = count;
    return made;
}

GridLine area_line(const std::string& name) {
    GridLine made{};
    made.kind = GridLineKind::area;
    made.area = name;
    return made;
}

GridItemLayout in_area(const std::string& name) {
    GridItemLayout layout{};
    layout.placement.column_start = area_line(name);
    layout.placement.column_end = area_line(name);
    layout.placement.row_start = area_line(name);
    layout.placement.row_end = area_line(name);
    return layout;
}

bool refuses_item(GridPanel& panel, const Control& child, const GridItemLayout& layout) {
    try {
        panel.set_item_layout(child, layout);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool refuses_columns(GridPanel& panel, const GridTrackList& list) {
    try {
        panel.set_template_columns(list);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
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

void test_tracks_gaps_and_stretch() {
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.tracks"));
    (*panel).padding = {10.0, 10.0, 10.0, 10.0};
    (*panel).set_template_columns(tracks({fixed(100.0), fraction(1.0)}));
    (*panel).set_column_gap(px(10.0));
    (*panel).set_row_gap(px(5.0));
    std::vector<std::shared_ptr<Panel>> boxes;
    for (int index = 0; index < 3; ++index) {
        boxes.push_back(box("grid.tracks." + std::to_string(index), 50.0, 20.0));
        (*panel).add_child(boxes.back());
    }
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{320.0, 100.0});
    Window& window = *held;
    window.perform_layout();
    // 300 by 80 of content: columns 100 and 190; two auto rows of 20
    // stretched by half of the 35 px left each.
    require_bounds(*boxes[0], 10.0, 10.0, 100.0, 37.5, "the first item fills its cell");
    require_bounds(*boxes[1], 120.0, 10.0, 190.0, 37.5, "the second takes the fraction column");
    require_bounds(*boxes[2], 10.0, 52.5, 100.0, 37.5, "the third wraps to the next row");
    const GridLayoutWorkspace& snapshot = (*panel).layout_snapshot();
    require(snapshot.columns().size() == 2U && snapshot.rows().size() == 2U, "the snapshot has the tracks");
    require(snapshot.rows()[1].implicit, "the rows are implicit");
    require((*panel).laid_out_children()[2] == (*boxes[2]).runtime_id(), "the snapshot names its children");
    require((*panel).layout_outcome() == GridSolveOutcome::solved, "the layout solved");

    // Start alignment sizes the items from their content instead.
    (*panel).set_justify_items(LayoutAlignment::start);
    (*panel).set_align_items(LayoutAlignment::end);
    window.perform_layout();
    require_bounds(*boxes[1], 120.0, 27.5, 50.0, 20.0, "a start-justified, end-aligned item");
}

void test_records_validate_and_reconcile() {
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.records"));
    std::shared_ptr<Panel> child = box("grid.records.child", 10.0, 10.0);
    std::shared_ptr<Panel> stranger = box("grid.records.stranger", 10.0, 10.0);
    (*panel).add_child(child);
    GridItemLayout spanning{};
    spanning.placement.column_start = span(2);
    require(refuses_item(*panel, *stranger, spanning), "a record needs a direct child");
    GridItemLayout zero_line{};
    zero_line.placement.row_start = line(0);
    require(refuses_item(*panel, *child, zero_line), "line 0 is refused");
    GridItemLayout zero_span{};
    zero_span.placement.row_end = span(0);
    require(refuses_item(*panel, *child, zero_span), "span 0 is refused");
    GridItemLayout content_width{};
    content_width.width = SizeValue{SizeKind::min_content, {}};
    require(refuses_item(*panel, *child, content_width), "a content keyword width is refused");

    GridTrackSize fraction_minimum = fraction(1.0);
    fraction_minimum.minimum = fraction_minimum.maximum;
    require(refuses_columns(*panel, tracks({fraction_minimum})), "a fraction minimum is refused");
    GridTrackList intrinsic_repeat{};
    intrinsic_repeat.auto_repeat = AutoRepeatKind::auto_fill;
    intrinsic_repeat.auto_repeat_tracks = {automatic()};
    require(refuses_columns(*panel, intrinsic_repeat), "an automatic repeat of an auto track is refused");
    GridTrackList too_many{};
    too_many.tracks.assign(maximum_grid_tracks + 1U, fixed(1.0));
    require(refuses_columns(*panel, too_many), "more than 10000 tracks are refused");
    GridTemplateAreas overlapping{};
    overlapping.rows = 1U;
    overlapping.columns = 2U;
    overlapping.areas = {GridNamedArea{"a", 0U, 1U, 0U, 1U}, GridNamedArea{"a", 0U, 1U, 1U, 2U}};
    bool refused = false;
    try {
        (*panel).set_template_areas(overlapping);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a name used twice is refused");
    std::vector<GridTrackSize> none{};
    refused = false;
    try {
        (*panel).set_auto_rows(none);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "grid-auto-rows needs a size");

    (*panel).set_item_layout(*child, spanning);
    require((*panel).item_layout(*child) == spanning, "the record is kept");
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{100.0, 40.0});
    Window& window = *held;
    window.perform_layout();
    static_cast<void>((*panel).remove_child((*child).runtime_id()));
    window.perform_layout();
    (*panel).add_child(child);
    require((*panel).item_layout(*child) == GridItemLayout{}, "a departed child's record is dropped");
}

void test_named_areas() {
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.areas"));
    (*panel).set_template_columns(tracks({fixed(80.0), fraction(1.0)}));
    (*panel).set_template_rows(tracks({fixed(30.0), fraction(1.0)}));
    GridTemplateAreas areas{};
    areas.rows = 2U;
    areas.columns = 2U;
    areas.areas = {GridNamedArea{"head", 0U, 1U, 0U, 2U}, GridNamedArea{"side", 1U, 2U, 0U, 1U},
                   GridNamedArea{"main", 1U, 2U, 1U, 2U}};
    (*panel).set_template_areas(areas);
    std::shared_ptr<Panel> main = box("grid.areas.main", 10.0, 10.0);
    std::shared_ptr<Panel> head = box("grid.areas.head", 10.0, 10.0);
    std::shared_ptr<Panel> side = box("grid.areas.side", 10.0, 10.0);
    (*panel).add_child(main);
    (*panel).add_child(head);
    (*panel).add_child(side);
    (*panel).set_item_layout(*main, in_area("main"));
    (*panel).set_item_layout(*head, in_area("head"));
    (*panel).set_item_layout(*side, in_area("side"));
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{300.0, 200.0});
    (*held).perform_layout();
    require_bounds(*head, 0.0, 0.0, 300.0, 30.0, "the head spans both columns");
    require_bounds(*side, 0.0, 30.0, 80.0, 170.0, "the side");
    require_bounds(*main, 80.0, 30.0, 220.0, 170.0, "the main area");
}

void test_nested_intrinsic_columns() {
    std::shared_ptr<GridPanel> outer = make_control<GridPanel>(StableId("grid.outer"));
    (*outer).set_template_columns(tracks({automatic(), fraction(1.0)}));
    std::shared_ptr<GridPanel> inner = make_control<GridPanel>(StableId("grid.outer.inner"));
    (*inner).set_template_columns(tracks({fixed(30.0), fixed(40.0)}));
    (*inner).set_column_gap(px(5.0));
    (*inner).margin = {0.0, 0.0, 0.0, 0.0};
    std::shared_ptr<Panel> first = box("grid.outer.inner.a", 10.0, 20.0);
    std::shared_ptr<Panel> second = box("grid.outer.inner.b", 10.0, 12.0);
    (*inner).add_child(first);
    (*inner).add_child(second);
    std::shared_ptr<Panel> after = box("grid.outer.after", 10.0, 10.0);
    (*outer).add_child(inner);
    (*outer).add_child(after);
    const std::unique_ptr<Window> held = std::make_unique<Window>(outer, Size{300.0, 100.0});
    (*held).perform_layout();
    require_bounds(*inner, 0.0, 0.0, 75.0, 100.0, "the inner grid's column is its max-content width");
    require_bounds(*after, 75.0, 0.0, 225.0, 100.0, "the fraction takes the rest");
    require_bounds(*second, 35.0, 0.0, 40.0, 100.0, "the inner items stretch over its row");
}

void test_cell_limit_refuses() {
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.limit"));
    std::shared_ptr<Panel> far = box("grid.limit.far", 10.0, 10.0);
    std::shared_ptr<Panel> placed = box("grid.limit.placed", 10.0, 10.0);
    (*panel).add_child(far);
    (*panel).add_child(placed);
    GridItemLayout distant{};
    distant.placement.column_start = line(2'000);
    distant.placement.row_start = line(2'000);
    (*panel).set_item_layout(*far, distant);
    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{100.0, 100.0});
    (*held).perform_layout();
    require((*panel).layout_outcome() == GridSolveOutcome::refused_too_many_cells,
            "a grid of four million cells is refused");
    require((*panel).laid_out_children().empty(), "a refused layout publishes no children");
    (*panel).set_item_layout(*far, GridItemLayout{});
    (*held).perform_layout();
    require((*panel).layout_outcome() == GridSolveOutcome::solved, "the layout solves again");
    require((*panel).laid_out_children().size() == 2U, "and publishes both children");
}

std::shared_ptr<Button> sized_button(const std::string& id) {
    std::shared_ptr<Button> made = make_control<Button>(StableId(id), "Button");
    (*made).set_requested_bounds({0.0, 0.0, 10.0, 20.0});
    (*made).margin = Insets{};
    return made;
}

// The docstring's two examples, with the results their comments state.
void test_documented_examples() {
    std::shared_ptr<GridPanel> form = make_control<GridPanel>(StableId("doc.form"));
    (*form).set_template_columns(tracks({fixed(80.0), fraction(1.0)}));
    (*form).set_column_gap(px(10.0));
    (*form).set_row_gap(px(5.0));
    std::vector<std::shared_ptr<Button>> buttons;
    for (int index = 0; index < 4; ++index) {
        buttons.push_back(sized_button("doc.form.button" + std::to_string(index)));
        (*form).add_child(buttons.back());
    }
    const std::unique_ptr<Window> form_window = std::make_unique<Window>(form, Size{200.0, 45.0});
    (*form_window).perform_layout();
    require_bounds(*buttons[0], 0.0, 0.0, 80.0, 20.0, "the first form button");
    require_bounds(*buttons[1], 90.0, 0.0, 110.0, 20.0, "the second form button");
    require_bounds(*buttons[2], 0.0, 25.0, 80.0, 20.0, "the third form button");
    require_bounds(*buttons[3], 90.0, 25.0, 110.0, 20.0, "the fourth form button");

    std::shared_ptr<GridPanel> cards = make_control<GridPanel>(StableId("doc.cards"));
    GridTrackSize card = fraction(1.0);
    card.minimum = TrackBreadth{TrackBreadthKind::length_percentage, px(100.0), FractionFactor{}};
    GridTrackList columns{};
    columns.auto_repeat = AutoRepeatKind::auto_fill;
    columns.auto_repeat_tracks = {card};
    (*cards).set_template_columns(columns);
    (*cards).set_column_gap(px(15.0));
    (*cards).set_row_gap(px(10.0));
    std::vector<std::shared_ptr<Button>> card_buttons;
    for (int index = 0; index < 5; ++index) {
        card_buttons.push_back(sized_button("doc.cards.button" + std::to_string(index)));
        (*cards).add_child(card_buttons.back());
    }
    const std::unique_ptr<Window> cards_window = std::make_unique<Window>(cards, Size{330.0, 50.0});
    (*cards_window).perform_layout();
    require((*cards).layout_snapshot().columns().size() == 3U, "three card columns fit");
    require_bounds(*card_buttons[0], 0.0, 0.0, 100.0, 20.0, "the first card");
    require_bounds(*card_buttons[1], 115.0, 0.0, 100.0, 20.0, "the second card");
    require_bounds(*card_buttons[2], 230.0, 0.0, 100.0, 20.0, "the third card");
    require_bounds(*card_buttons[3], 0.0, 30.0, 100.0, 20.0, "the fourth card");
    require_bounds(*card_buttons[4], 115.0, 30.0, 100.0, 20.0, "the fifth card");
}

void test_ten_thousand_children_counts() {
    constexpr std::size_t count = 10'000U;
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.many"));
    GridTrackList columns{};
    columns.auto_repeat = AutoRepeatKind::auto_fill;
    columns.auto_repeat_tracks = {fixed(120.0)};
    (*panel).set_template_columns(columns);
    (*panel).set_align_items(LayoutAlignment::start);
    std::vector<std::shared_ptr<CountingLabel>> labels;
    labels.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        std::shared_ptr<CountingLabel> label =
            make_control<CountingLabel>(StableId("grid.many." + std::to_string(index)));
        (*label).text = "Item " + std::to_string(index);
        (*panel).add_child(label);
        labels.push_back(std::move(label));
    }
    // The panel sits in a plain root, so resizing the container (not the
    // window) is a bounds change on the panel alone.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("grid.many.root"));
    (*panel).set_bounds({0.0, 0.0, 1200.0, 800.0});
    (*root).add_child(panel);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{1200.0, 800.0});
    Window& window = *held;
    window.perform_layout();
    require((*panel).layout_snapshot().columns().size() == 10U, "ten 120 px columns fit 1200 px");
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    window.reset_activity_metrics();

    // One item's text: the items stretch over fixed columns, so each is
    // asked only its height at its column's width. The changed item's
    // query misses, and so do the panel's own measure and the plain root's
    // above it; every other item answers from its cache.
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

    // Resizing the panel changes no item's constraint: the columns stay
    // 120 px wide, though there are fewer of them.
    for (const std::shared_ptr<CountingLabel>& label : labels) (*label).callbacks = 0U;
    (*panel).set_bounds({0.0, 0.0, 900.0, 700.0});
    window.perform_layout();
    callbacks = 0U;
    for (const std::shared_ptr<CountingLabel>& label : labels) callbacks += (*label).callbacks;
    require(callbacks == 0U, "resizing costs no item measure callback, got " + std::to_string(callbacks));
    require((*panel).layout_snapshot().columns().size() == 7U, "seven columns fit 900 px");

    // Arranging again at the same size allocates nothing.
    const Rect bounds = (*panel).arranged_bounds();
    (*panel).arrange(bounds);
    counted_allocations = 0U;
    counting_allocations = true;
    (*panel).arrange(bounds);
    counting_allocations = false;
    require(counted_allocations == 0U, "arranging 10000 children allocated " + std::to_string(counted_allocations) +
                                           " times");
    std::cout << "grid-panel-tests: 10000 children: 1 callback on a text change, 0 on a resize, "
                 "0 allocations per arrange\n";
}

bool refuses_area(GridPanel& panel, const Control& child, const std::string& name) {
    try {
        panel.area[child] = name;
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool refuses_cell(GridPanel& panel, const Control& child, GridCell cell) {
    try {
        panel.cell[child] = cell;
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool refuses_cell_value(GridPanel& panel, const Control& child, Point value) {
    try {
        panel.set_child_property_value(child, "cell", BindingValue{value});
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Study 10.9: spans owned by the panel, refused edits, and the two views of
// one placement (area and cell), name-keyed too.
void test_spans_and_placement() {
    std::shared_ptr<GridPanel> panel = make_control<GridPanel>(StableId("grid.spans"));
    (*panel).set_template_columns(tracks({fixed(50.0), fixed(50.0), fixed(50.0)}));
    (*panel).set_template_rows(tracks({fixed(20.0), fixed(20.0), fixed(20.0)}));
    const GridSpan footer{"footer", GridCell{2U, 0U}, 1U, 3U};
    const GridSpan side_region{"", GridCell{0U, 2U}, 2U, 1U};
    require((*panel).define_span(footer) == GridSpanOutcome::applied, "a named span is defined");
    require((*panel).define_span(side_region) == GridSpanOutcome::applied, "an anonymous span is defined");
    require((*panel).define_span(GridSpan{"", GridCell{1U, 2U}, 1U, 1U}) == GridSpanOutcome::overlaps,
            "a span over another's cell is refused");
    require((*panel).define_span(GridSpan{"footer", GridCell{0U, 0U}, 1U, 1U}) == GridSpanOutcome::name_repeated,
            "a repeated name is refused");
    require((*panel).define_span(GridSpan{"", GridCell{0U, 0U}, 0U, 1U}) == GridSpanOutcome::extent_invalid,
            "a span of no rows is refused");
    require((*panel).define_span(GridSpan{"", GridCell{9999U, 0U}, 2U, 1U}) == GridSpanOutcome::extent_invalid,
            "a span past the track limit is refused");
    require((*panel).spans().size() == 2U, "refusals change nothing");
    const GridTemplateAreas& areas = (*panel).template_areas();
    require(areas.rows == 3U && areas.columns == 3U && areas.areas.size() == 1U && areas.areas[0].name == "footer",
            "the template areas are the named spans");

    std::shared_ptr<Panel> title = box("grid.spans.title", 10.0, 10.0);
    std::shared_ptr<Panel> side = box("grid.spans.side", 10.0, 10.0);
    std::shared_ptr<Panel> ok = box("grid.spans.ok", 10.0, 10.0);
    std::shared_ptr<Panel> note = box("grid.spans.note", 10.0, 10.0);
    std::shared_ptr<Panel> stranger = box("grid.spans.stranger", 10.0, 10.0);
    (*panel).add_child(title);
    (*panel).add_child(side);
    (*panel).add_child(ok);
    (*panel).add_child(note);
    (*panel).cell[*title] = GridCell{0U, 0U};
    (*panel).cell[*side] = GridCell{0U, 2U};
    (*panel).area[*ok] = "footer";
    require(refuses_area(*panel, *note, "nowhere"), "an area names a span");
    require(refuses_cell(*panel, *note, GridCell{1U, 2U}), "a cell inside a span is refused");
    require(refuses_cell(*panel, *note, GridCell{10000U, 0U}), "a cell past the track limit is refused");
    require(refuses_cell(*panel, *stranger, GridCell{1U, 1U}), "a placement needs a direct child");
    require((*panel).placement(*note).form == GridPlacement::Form::automatic, "refusals place nothing");
    require((*panel).area[*ok].read() == "footer", "the area reads back");
    require((*panel).cell[*ok].read() == GridCell{2U, 0U}, "an area's cell is its span's anchor");
    require((*panel).area[*side].read().empty(), "a cell placement has no area");

    const std::unique_ptr<Window> held = std::make_unique<Window>(panel, Size{150.0, 60.0});
    Window& window = *held;
    window.perform_layout();
    require_bounds(*title, 0.0, 0.0, 50.0, 20.0, "a single cell");
    require_bounds(*side, 100.0, 0.0, 50.0, 40.0, "the anchor of an anonymous span takes its region");
    require_bounds(*ok, 0.0, 40.0, 150.0, 20.0, "the named span's region");

    require((*panel).remove_span("footer") == GridSpanOutcome::in_use, "a span in use is not removed");
    require((*panel).remove_span("absent") == GridSpanOutcome::not_found, "an absent span is not found");
    (*panel).cell[*note] = GridCell{1U, 1U};
    require((*panel).define_span(GridSpan{"", GridCell{1U, 0U}, 1U, 2U}) == GridSpanOutcome::covers_placed_child,
            "a span over a placed child's cell is refused");
    require((*panel).define_span(GridSpan{"", GridCell{0U, 0U}, 1U, 2U}) == GridSpanOutcome::applied,
            "a span anchored at a placed child's cell is defined");
    window.perform_layout();
    require_bounds(*title, 0.0, 0.0, 100.0, 20.0, "the child at the new span's anchor takes its region");

    (*panel).cell[*ok] = GridCell{1U, 0U};
    require((*panel).placement(*ok).form == GridPlacement::Form::cell && (*panel).area[*ok].read().empty(),
            "writing the cell replaces the area");
    require((*panel).remove_span("footer") == GridSpanOutcome::applied, "a span no longer in use is removed");
    require((*panel).template_areas().areas.empty(), "the template areas follow the spans");
    require((*panel).remove_span(GridCell{0U, 0U}) == GridSpanOutcome::applied, "a span is removed by its anchor");

    GridItemLayout lines{};
    lines.placement.column_start = line(3);
    (*panel).set_item_layout(*side, lines);
    require((*panel).placement(*side).form == GridPlacement::Form::automatic,
            "item layout lines replace a cell placement");
    (*panel).cell[*side] = GridCell{0U, 2U};
    require((*panel).item_layout(*side).placement == GridItemStyle{}, "a cell placement replaces the lines");

    (*panel).set_child_property_value(*side, "cell", BindingValue{Point{1.0, 1.0}});
    require((*panel).cell[*side].read() == GridCell{1U, 1U}, "the name-keyed write places by cell");
    require(refuses_cell_value(*panel, *side, Point{0.5, 1.0}), "a fractional cell is refused");
    require(refuses_cell_value(*panel, *side, Point{-1.0, 1.0}), "a negative cell is refused");
    const std::optional<BindingValue> read = (*panel).child_property_value(*side, "cell");
    require(read && *read == BindingValue{Point{1.0, 1.0}}, "the name-keyed read answers row then column");
    require((*panel).should_serialize_child_property(*side, "cell"), "a cell placement is serialized");
    require(!(*panel).should_serialize_child_property(*side, "area"), "an absent area is not");
    require((*panel).reset_child_property(*side, "area"), "the area resets");
    require((*panel).placement(*side).form == GridPlacement::Form::cell, "resetting the area keeps a cell");
    require((*panel).reset_child_property(*side, "cell"), "the cell resets");
    require((*panel).placement(*side).form == GridPlacement::Form::automatic, "resetting the cell clears it");

    static_cast<void>((*panel).remove_child((*note).runtime_id()));
    (*panel).add_child(note);
    require((*panel).placement(*note).form == GridPlacement::Form::automatic,
            "a child added again starts automatic");
    require((*panel).define_span(GridSpan{"", GridCell{1U, 0U}, 1U, 2U}) == GridSpanOutcome::applied,
            "a departed child's cell no longer refuses a span");

    // grid-template-areas is the same list: setting it replaces every span.
    GridTemplateAreas spelled{};
    spelled.rows = 3U;
    spelled.columns = 3U;
    spelled.areas = {GridNamedArea{"head", 0U, 1U, 0U, 3U}};
    (*panel).set_template_areas(spelled);
    require((*panel).spans().size() == 1U && (*panel).spans()[0] == GridSpan{"head", GridCell{0U, 0U}, 1U, 3U},
            "the areas become the spans");
    require((*panel).template_areas() == spelled, "and read back unchanged");
    (*panel).area[*ok] = "head";
    GridTemplateAreas without{};
    bool refused = false;
    try {
        (*panel).set_template_areas(without);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*panel).spans().size() == 1U, "areas that drop a span in use are refused");
}

} // namespace


// The `style` property and the `item_style` child property (study 10,
// section 6): CSS inline styles read by the layout style reader, read back
// in the reader's spelling, and refused whole for any declaration the grid
// does not carry (spans are span blocks, placement is area and cell, and
// margin lengths are the child's own margin).
void test_style_properties() {
    std::shared_ptr<GridPanel> grid = make_control<GridPanel>(StableId("grid.style"));
    std::shared_ptr<Button> item = make_control<Button>(StableId("grid.style.item"), "Item");
    (*grid).add_child(item);
    (*grid).set_property_value("style", BindingValue{std::string{
        "grid-template-columns: 90px 1fr; column-gap: 6px; justify-items: center"}});
    require((*grid).template_columns().tracks.size() == 2U &&
                (*grid).column_gap() == LengthPercentage{LogicalLength{6.0}, 0.0} &&
                (*grid).justify_items() == LayoutAlignment::center,
            "grid style: tracks, gap and alignment applied");
    const std::optional<BindingValue> text = (*grid).property_value("style");
    require(text.has_value() && !std::get<std::string>(*text).empty(), "grid style: reads back as text");
    std::shared_ptr<GridPanel> copy = make_control<GridPanel>(StableId("grid.style.copy"));
    (*copy).set_property_value("style", *text);
    require((*copy).template_columns() == (*grid).template_columns() && (*copy).column_gap() == (*grid).column_gap() &&
                (*copy).justify_items() == (*grid).justify_items(),
            "grid style: the text read back gives the same settings");
    bool areas_refused = false;
    try {
        (*grid).set_property_value("style", BindingValue{std::string{"grid-template-areas: \"a b\""}});
    } catch (const std::invalid_argument&) {
        areas_refused = true;
    }
    bool warning_refused = false;
    try {
        (*grid).set_property_value("style", BindingValue{std::string{"float: left"}});
    } catch (const std::invalid_argument&) {
        warning_refused = true;
    }
    bool item_field_refused = false;
    try {
        (*grid).set_property_value("style", BindingValue{std::string{"width: 10px"}});
    } catch (const std::invalid_argument&) {
        item_field_refused = true;
    }
    require(areas_refused && warning_refused && item_field_refused && (*grid).template_columns().tracks.size() == 2U &&
                (*grid).spans().empty(),
            "grid style: areas, a dropped declaration and an item field are refused and change nothing");
    (*grid).set_child_property_value(*item, "item_style", BindingValue{std::string{
        "justify-self: end; width: 40px; margin-left: auto"}});
    const GridItemLayout layout = (*grid).item_layout(*item);
    require(layout.justify_self == LayoutAlignment::end && layout.width.kind == SizeKind::length_percentage &&
                layout.auto_margin.left,
            "grid item style: alignment, width and an auto margin applied");
    const std::optional<BindingValue> item_text = (*grid).child_property_value(*item, "item_style");
    require(item_text.has_value() && std::get<std::string>(*item_text).find("justify-self: end") != std::string::npos,
            "grid item style: reads back as text");
    bool placement_refused = false;
    try {
        (*grid).set_child_property_value(*item, "item_style", BindingValue{std::string{"grid-column: 2"}});
    } catch (const std::invalid_argument&) {
        placement_refused = true;
    }
    bool margin_refused = false;
    try {
        (*grid).set_child_property_value(*item, "item_style", BindingValue{std::string{"margin-left: 4px"}});
    } catch (const std::invalid_argument&) {
        margin_refused = true;
    }
    require(placement_refused && margin_refused && (*grid).item_layout(*item) == layout,
            "grid item style: placement lines and margin lengths are refused and change nothing");
}

int main() {
    try {
        test_tracks_gaps_and_stretch();
        test_style_properties();
        test_records_validate_and_reconcile();
        test_named_areas();
        test_spans_and_placement();
        test_nested_intrinsic_columns();
        test_cell_limit_refuses();
        test_documented_examples();
        test_ten_thousand_children_counts();
        std::cout << "grid-panel-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "grid-panel-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
