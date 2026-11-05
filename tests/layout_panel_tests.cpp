#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap. main inlines every test, and
// the sanitizer build gives each inlined local its own stack slot, so
// windows held on the stack add up to a frame of 73 KB in main.

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right, double tolerance = 0.001) {
    return std::abs(left - right) <= tolerance;
}

class MeasureMutationControl final : public Control {
public:
    explicit MeasureMutationControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        ++measure_count;
        if (callback) callback();
        return is_alive() ? Control::measure(available) : Size{};
    }

    std::function<void()> callback;
    std::uint64_t measure_count{};
};

class SetFlowWrapOffThread final {
public:
    SetFlowWrapOffThread(FlowLayoutPanelCompat& flow, bool& rejected)
        : flow_(flow), rejected_(rejected) {}

    void operator()() const {
        try {
            flow_.set_wrap_contents(true);
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    FlowLayoutPanelCompat& flow_;
    bool& rejected_;
};

class RemoveControlDuringMeasure final {
public:
    RemoveControlDuringMeasure(Control& parent,
                               MeasureMutationControl& removed,
                               bool& removed_once)
        : parent_(parent), removed_(removed), removed_once_(removed_once) {}

    void operator()() const {
        if (removed_once_) {
            return;
        }
        removed_once_ = true;
        static_cast<void>(parent_.remove_child(removed_.runtime_id()));
    }

private:
    Control& parent_;
    MeasureMutationControl& removed_;
    bool& removed_once_;
};

std::shared_ptr<Button> sized_button(std::string id, double width,
                                     double height) {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId(std::move(id)), "Item");
    (*button).set_requested_bounds({0.0, 0.0, width, height});
    (*button).margin = {0.0, 0.0, 0.0, 0.0};
    return button;
}

void test_margin_padding_validation_and_retained_slots() {
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("layout.flow.slots"));
    (*flow).accessible_name = "Wrapping flow specimen";
    (*flow).padding = {5.0, 5.0, 5.0, 5.0};
    std::shared_ptr<Button> first = sized_button("layout.flow.first", 40.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.flow.second", 50.0, 20.0);
    std::shared_ptr<Button> third = sized_button("layout.flow.third", 30.0, 20.0);
    (*first).margin = {2.0, 1.0, 3.0, 1.0};
    (*second).margin = {4.0, 1.0, 4.0, 1.0};
    (*third).margin = {3.0, 2.0, 3.0, 2.0};
    (*flow).add_child(first);
    (*flow).add_child(second);
    (*flow).add_child(third);
    const Rect first_preferred = (*first).requested_bounds();
    const Rect second_preferred = (*second).requested_bounds();
    const Rect third_preferred = (*third).requested_bounds();

    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{120.0, 100.0});

    Window& window = *held_window;
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{7.0, 6.0, 40.0, 20.0} &&
                (*second).arranged_bounds() == Rect{54.0, 6.0, 50.0, 20.0} &&
                (*third).arranged_bounds() == Rect{8.0, 29.0, 30.0, 20.0},
            "flow layout must honor physical margins, padding, and wrap thresholds");
    require((*first).requested_bounds() == first_preferred &&
                (*second).requested_bounds() == second_preferred &&
                (*third).requested_bounds() == third_preferred,
            "parent-assigned layout slots must never overwrite authored preferred bounds");
    require(window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "layout-slot publication must converge within the bounded layout pipeline");
    require((*flow).semantic_descriptor().role == SemanticRole::group &&
                (*flow).semantic_descriptor().exposed &&
                window.semantic_snapshot().to_json().find(
                    "Wrapping flow specimen") != std::string::npos,
            "named flow layouts must publish one retained semantic group");

    bool margin_rejected = false;
    bool padding_rejected = false;
    try {
        (*first).margin = {-1.0, 0.0, 0.0, 0.0};
    } catch (const std::invalid_argument&) {
        margin_rejected = true;
    }
    try {
        (*flow).padding = {0.0, 0.0, INFINITY, 0.0};
    } catch (const std::invalid_argument&) {
        padding_rejected = true;
    }
    require(margin_rejected && padding_rejected,
            "margin and padding must reject negative or nonfinite geometry");
}

// The flow break as a child property (study sections 10 and 15.4): the
// typed borrow and the name-keyed table read and write one value per child;
// both refuse a control that is not a live direct child; a child that
// leaves takes its value with it.
void test_flow_break_child_property() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("layout.flow.child"));
    std::shared_ptr<Button> first = sized_button("layout.flow.child.first", 40.0, 20.0);
    std::shared_ptr<Button> stranger = sized_button("layout.flow.child.stranger", 40.0, 20.0);
    (*flow).add_child(first);

    const std::vector<gui_forms::PropertyDescriptor> provided = (*flow).child_property_descriptors();
    require(provided.size() == 1U && provided[0].name == "flow_break" &&
                provided[0].kind == gui_forms::BindingValueKind::boolean,
            "flow child table: one boolean flow_break");
    require(!(*flow).flow_break[*first].read(), "flow break: the default before any write");
    (*flow).set_child_property_value(*first, "flow-break", gui_forms::BindingValue{true});
    require((*flow).flow_break[*first].read(), "flow break: a name-keyed write reaches the typed read");
    (*flow).flow_break[*first] = false;
    const std::optional<gui_forms::BindingValue> named = (*flow).child_property_value(*first, "flow_break");
    require(named && std::get<bool>(*named) == false && !(*flow).should_serialize_child_property(*first, "flow_break"),
            "flow break: a typed write reaches the name-keyed read; the default is not serialized");
    require(!(*flow).child_property_value(*first, "column_span"), "flow break: an unprovided name has no value");

    bool typed_refused = false;
    try {
        (*flow).flow_break[*stranger] = true;
    } catch (const std::invalid_argument&) {
        typed_refused = true;
    }
    bool named_refused = false;
    try {
        static_cast<void>((*flow).child_property_value(*stranger, "flow_break"));
    } catch (const std::invalid_argument&) {
        named_refused = true;
    }
    bool unknown_refused = false;
    try {
        (*flow).set_child_property_value(*first, "column_span", gui_forms::BindingValue{std::uint64_t{2U}});
    } catch (const std::invalid_argument&) {
        unknown_refused = true;
    }
    require(typed_refused && named_refused && unknown_refused,
            "flow break: a stranger and an unprovided name are refused");

    (*flow).flow_break[*first] = true;
    static_cast<void>((*flow).remove_child((*first).runtime_id()));
    (*flow).add_child(first);
    require(!(*flow).flow_break[*first].read(), "flow break: a child that left comes back with the default");
    (*flow).flow_break[*first] = true;
    require((*flow).reset_child_property(*first, "flow_break") && !(*flow).flow_break[*first].read(),
            "flow break: reset restores the default");
}

void test_flow_direction_break_visibility_and_resize() {
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("layout.flow.policy"));
    std::shared_ptr<Button> first = sized_button("layout.flow.policy.first", 40.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.flow.policy.second", 40.0, 20.0);
    std::shared_ptr<Button> third = sized_button("layout.flow.policy.third", 40.0, 20.0);
    (*flow).add_child(first);
    (*flow).add_child(second);
    (*flow).add_child(third);
    (*flow).flow_break[*first] = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{100.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*first).arranged_bounds().y == 0.0 &&
                (*second).arranged_bounds().y == 20.0 &&
                (*third).arranged_bounds().y == 20.0,
            "FlowBreak must terminate one row while retaining later child order");

    (*flow).flow_break[*first] = false;
    (*flow).set_flow_direction(FlowDirection::right_to_left);
    window.perform_layout();
    require((*first).arranged_bounds().x == 60.0 &&
                (*second).arranged_bounds().x == 20.0 &&
                (*third).arranged_bounds().x == 60.0 &&
                (*third).arranged_bounds().y == 20.0,
            "right-to-left flow must mirror each wrapped row without reversing ownership");

    (*second).visible = false;
    window.perform_layout();
    require((*first).arranged_bounds().x == 60.0 &&
                (*third).arranged_bounds().x == 20.0 &&
                (*third).arranged_bounds().y == 0.0,
            "hidden flow children must consume neither main-axis nor wrap space");

    (*second).visible = true;
    (*flow).set_flow_direction(FlowDirection::bottom_up);
    (*flow).set_wrap_contents(false);
    window.perform_layout();
    require((*first).arranged_bounds().y == 80.0 &&
                (*second).arranged_bounds().y == 60.0 &&
                (*third).arranged_bounds().y == 40.0,
            "bottom-up no-wrap flow must allocate one deterministic vertical column");

    bool wrong_thread_rejected = false;
    std::thread worker(SetFlowWrapOffThread(*flow, wrong_thread_rejected));
    worker.join();
    require(wrong_thread_rejected,
            "attached flow policy mutation must retain UI-thread enforcement");
}

void test_flow_item_spacing_is_explicit_bounded_layout_state() {
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("layout.flow.spacing"));
    std::shared_ptr<Button> first = sized_button("layout.flow.spacing.first", 30.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.flow.spacing.second", 30.0, 20.0);
    std::shared_ptr<Button> third = sized_button("layout.flow.spacing.third", 30.0, 20.0);
    (*flow).add_child(first);
    (*flow).add_child(second);
    (*flow).add_child(third);
    (*flow).set_item_spacing({7.0, 9.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{75.0, 80.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*first).arranged_bounds() == Rect{0.0, 0.0, 30.0, 20.0} &&
                (*second).arranged_bounds() == Rect{37.0, 0.0, 30.0, 20.0} &&
                (*third).arranged_bounds() == Rect{0.0, 29.0, 30.0, 20.0},
            "FlowLayoutPanelCompat item spacing must separate items and wrapped lines");

    bool rejected{};
    try {
        (*flow).set_item_spacing({257.0, 9.0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*flow).item_spacing() == Size{7.0, 9.0},
            "FlowLayoutPanelCompat item spacing must reject invalid geometry atomically");
}

void test_flow_auto_size_mode_preserves_authored_extent() {
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow =
        make_control<FlowLayoutPanelCompat>(StableId("layout.flow.auto-size-mode"));
    std::shared_ptr<Button> child =
        sized_button("layout.flow.auto-size-mode.child", 72.0, 43.0);
    (*flow).padding = {5.0, 2.0, 5.0, 0.0};
    (*flow).set_requested_bounds({0.0, 0.0, 176.0, 60.0});
    (*flow).minimum_size = {100.0, 50.0};
    (*flow).auto_size = true;
    (*flow).add_child(child);

    require((*flow).auto_size_mode.read() == AutoSizeMode::grow_only &&
                (*flow).measure({500.0, 500.0}) == Size{176.0, 60.0},
            "GrowOnly FlowLayoutPanelCompat AutoSize must retain authored extent");

    (*flow).auto_size_mode = AutoSizeMode::grow_and_shrink;
    require((*flow).measure({500.0, 500.0}) == Size{100.0, 50.0},
            "GrowAndShrink FlowLayoutPanelCompat AutoSize must derive content while honoring minimum size");
}

void test_flow_alignment_projects_relational_flex_geometry() {
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> centered =
        make_control<FlowLayoutPanelCompat>(StableId("layout.flow.centered"));
    std::shared_ptr<Button> centered_child =
        sized_button("layout.flow.centered.child", 40.0, 20.0);
    (*centered).add_child(centered_child);
    (*centered).set_wrap_contents(false);
    (*centered).set_main_alignment(FlowMainAlignment::center);
    (*centered).set_cross_alignment(FlowCrossAlignment::center);
    const std::unique_ptr<Window> held_centered_window = std::make_unique<Window>(centered, Size{200.0, 100.0});
    Window& centered_window = *held_centered_window;
    centered_window.perform_layout();
    require((*centered_child).arranged_bounds() == Rect{80.0, 40.0, 40.0, 20.0},
            "single-line flow must center a child on both live container axes");

    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> distributed =
        make_control<FlowLayoutPanelCompat>(StableId("layout.flow.distributed"));
    std::shared_ptr<Button> first =
        sized_button("layout.flow.distributed.first", 20.0, 10.0);
    std::shared_ptr<Button> second =
        sized_button("layout.flow.distributed.second", 20.0, 10.0);
    std::shared_ptr<Button> third =
        sized_button("layout.flow.distributed.third", 20.0, 10.0);
    (*distributed).add_child(first);
    (*distributed).add_child(second);
    (*distributed).add_child(third);
    (*distributed).set_wrap_contents(false);
    (*distributed).set_main_alignment(FlowMainAlignment::space_between);
    (*distributed).set_cross_alignment(FlowCrossAlignment::stretch);
    const std::unique_ptr<Window> held_distributed_window = std::make_unique<Window>(distributed, Size{100.0, 30.0});
    Window& distributed_window = *held_distributed_window;
    distributed_window.perform_layout();
    require((*first).arranged_bounds() == Rect{0.0, 0.0, 20.0, 30.0} &&
                (*second).arranged_bounds() == Rect{40.0, 0.0, 20.0, 30.0} &&
                (*third).arranged_bounds() == Rect{80.0, 0.0, 20.0, 30.0},
            "flow space-between and stretch must derive slots from live geometry");

    bool invalid_main = false;
    bool invalid_cross = false;
    try {
        (*distributed).set_main_alignment(static_cast<FlowMainAlignment>(0xffU));
    } catch (const std::invalid_argument&) {
        invalid_main = true;
    }
    try {
        (*distributed).set_cross_alignment(static_cast<FlowCrossAlignment>(0xffU));
    } catch (const std::invalid_argument&) {
        invalid_cross = true;
    }
    require(invalid_main && invalid_cross &&
                (*distributed).main_alignment() == FlowMainAlignment::space_between &&
                (*distributed).cross_alignment() == FlowCrossAlignment::stretch,
            "flow alignment must reject unknown values without mutation");

    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> growing =
        make_control<FlowLayoutPanelCompat>(StableId("layout.flow.growing"));
    std::shared_ptr<Button> fixed =
        sized_button("layout.flow.growing.fixed", 20.0, 10.0);
    std::shared_ptr<Button> first_grow =
        sized_button("layout.flow.growing.first", 10.0, 10.0);
    std::shared_ptr<Button> second_grow =
        sized_button("layout.flow.growing.second", 10.0, 10.0);
    (*growing).add_child(fixed);
    (*growing).add_child(first_grow);
    (*growing).add_child(second_grow);
    (*growing).set_wrap_contents(false);
    (*growing).set_flex_grow(*first_grow, 1.0);
    (*growing).set_flex_grow(*second_grow, 2.0);
    const std::unique_ptr<Window> held_growing_window = std::make_unique<Window>(growing, Size{100.0, 20.0});
    Window& growing_window = *held_growing_window;
    growing_window.perform_layout();
    require((*fixed).arranged_bounds().width == 20.0 &&
                near((*first_grow).arranged_bounds().width, 30.0) &&
                near((*second_grow).arranged_bounds().width, 50.0) &&
                near((*second_grow).arranged_bounds().x, 50.0),
            "retained flex grow must distribute remaining main-axis geometry by ratio");
    bool invalid_grow = false;
    try {
        (*growing).set_flex_grow(*first_grow, -1.0);
    } catch (const std::invalid_argument&) {
        invalid_grow = true;
    }
    require(invalid_grow && (*growing).flex_grow(*first_grow) == 1.0,
            "flex grow must reject invalid ratios without mutation");
}

void test_layout_panels_revalidate_snapshot_after_measure_callback() {
    {
        std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(
            StableId("layout.flow.mutation"));
        std::shared_ptr<MeasureMutationControl> mutator =
            make_control<MeasureMutationControl>(
                StableId("layout.flow.mutation.mutator"));
        std::shared_ptr<MeasureMutationControl> removed =
            make_control<MeasureMutationControl>(
                StableId("layout.flow.mutation.removed"));
        (*mutator).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
        (*removed).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
        (*flow).add_child(mutator);
        (*flow).add_child(removed);
        const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{160.0, 80.0});
        Window& window = *held_window;
        window.perform_layout();
        (*mutator).measure_count = 0U;
        (*removed).measure_count = 0U;
        bool removed_once{};
        (*mutator).callback =
            RemoveControlDuringMeasure(*flow, *removed, removed_once);
        (*flow).set_flow_direction(FlowDirection::right_to_left);
        window.perform_layout();
        require(removed_once && (*removed).measure_count == 0U &&
                    !(*removed).attached() && !(*removed).parent() &&
                    window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "flow layout must not measure or assign an identity removed by an earlier callback");
    }

    {
        std::shared_ptr<gui_forms::TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(
            StableId("layout.table.mutation"));
        (*table).set_column_count(2U);
        (*table).set_row_count(1U);
        std::shared_ptr<MeasureMutationControl> mutator =
            make_control<MeasureMutationControl>(
                StableId("layout.table.mutation.mutator"));
        std::shared_ptr<MeasureMutationControl> removed =
            make_control<MeasureMutationControl>(
                StableId("layout.table.mutation.removed"));
        (*mutator).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
        (*removed).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
        (*table).add_child(mutator);
        (*table).add_child(removed);
        const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{160.0, 80.0});
        Window& window = *held_window;
        window.perform_layout();
        (*mutator).measure_count = 0U;
        (*removed).measure_count = 0U;
        bool removed_once{};
        (*mutator).callback =
            RemoveControlDuringMeasure(*table, *removed, removed_once);
        (*table).set_column_style(0U, {TableSizeMode::absolute, 60.0});
        window.perform_layout();
        require(removed_once && (*removed).measure_count == 0U &&
                    !(*removed).attached() && !(*removed).parent() &&
                    window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "table layout must remove stale resolved items before invoking their measure callback");
    }
}

void test_table_mixed_tracks_spans_and_lookup() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("layout.table.mixed"));
    (*table).accessible_name = "Mixed table specimen";
    (*table).padding = {5.0, 5.0, 5.0, 5.0};
    (*table).set_column_count(4U);
    (*table).set_row_count(2U);
    (*table).set_column_style(0U, {TableSizeMode::absolute, 40.0});
    (*table).set_column_style(1U, {TableSizeMode::auto_size, 0.0});
    (*table).set_column_style(2U, {TableSizeMode::percent, 1.0});
    (*table).set_column_style(3U, {TableSizeMode::percent, 2.0});
    (*table).set_row_style(0U, {TableSizeMode::auto_size, 0.0});
    (*table).set_row_style(1U, {TableSizeMode::percent, 1.0});
    (*table).set_cell_border_style(TableCellBorderStyle::single);

    std::shared_ptr<Button> first = sized_button("layout.table.first", 30.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.table.second", 60.0, 30.0);
    std::shared_ptr<Button> spanning = sized_button("layout.table.spanning", 200.0, 30.0);
    std::shared_ptr<Button> automatic = sized_button("layout.table.automatic", 40.0, 20.0);
    (*table).add_child(first);
    (*table).add_child(second);
    (*table).add_child(spanning);
    (*table).add_child(automatic);
    (*table).set_cell_position(*first, {0U, 0U});
    (*table).set_cell_position(*second, {1U, 0U});
    (*table).set_cell_position(*spanning, {1U, 1U});
    (*table).set_column_span(*spanning, 3U);

    const Rect spanning_preferred = (*spanning).requested_bounds();
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{400.0, 160.0});
    Window& window = *held_window;
    window.perform_layout();
    const std::span<const double> widths = (*table).column_widths();
    const std::span<const double> heights = (*table).row_heights();
    require(widths.size() == 4U && heights.size() == 2U &&
                near(widths[0], 40.0) && near(widths[1], 85.0) &&
                near(widths[2], 88.333333) && near(widths[3], 176.666667) &&
                near(heights[0], 30.0) && near(heights[1], 120.0),
            "table tracks must resolve absolute, auto, and weighted percent sizes deterministically");
    require((*first).arranged_bounds() == Rect{5.0, 5.0, 30.0, 20.0} &&
                (*second).arranged_bounds() == Rect{45.0, 5.0, 60.0, 30.0} &&
                (*spanning).arranged_bounds() == Rect{45.0, 35.0, 200.0, 30.0},
            "table cells and a three-column span must place preferred child sizes at cell origins");
    require((*table).cell_position(*automatic) ==
                std::optional(TableLayoutCellPosition{2U, 0U}) &&
                (*table).control_from_position(3U, 1U) == spanning &&
                (*table).column_span(*spanning) == 3U &&
                (*spanning).requested_bounds() == spanning_preferred,
            "table lookup must expose automatic cells, span occupancy, and retained preferred bounds");
    require(!(*table).layout_overflowed() &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "mixed table layout must converge without overflow or bounded-pass failure");
    require((*table).semantic_descriptor().role == SemanticRole::group &&
                (*table).semantic_descriptor().exposed,
            "named table layouts must expose their child geometry as a semantic group");

    (*table).auto_size = true;
    const Size desired = (*table).measure({1000.0, 1000.0});
    require(near(desired.width, 330.0) && near(desired.height, 70.0),
            "auto-sized table measurement must preserve percent ratios while satisfying content minima");
}

void test_table_growth_hidden_children_and_fixed_overflow() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("layout.table.growth"));
    (*table).set_column_count(2U);
    (*table).set_row_count(1U);
    (*table).set_grow_style(TableLayoutGrowStyle::add_rows);
    std::shared_ptr<Button> first = sized_button("layout.table.growth.first", 30.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.table.growth.second", 30.0, 20.0);
    std::shared_ptr<Button> third = sized_button("layout.table.growth.third", 30.0, 20.0);
    (*table).add_child(first);
    (*table).add_child(second);
    (*table).add_child(third);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{120.0, 80.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*table).row_heights().size() == 2U &&
                (*table).cell_position(*third) ==
                    std::optional(TableLayoutCellPosition{0U, 1U}),
            "AddRows must create the minimum runtime track for automatic overflow");

    (*second).visible = false;
    window.perform_layout();
    require((*table).row_heights().size() == 1U &&
                (*table).cell_position(*third) ==
                    std::optional(TableLayoutCellPosition{1U, 0U}),
            "hidden table children must release occupancy and collapse runtime tracks");

    (*second).visible = true;
    (*table).set_grow_style(TableLayoutGrowStyle::fixed_size);
    window.perform_layout();
    require((*table).layout_overflowed() && (*third).arranged_bounds().empty() &&
                !(*table).cell_position(*third),
            "fixed tables must report overflow and assign no overlapping slot");

    bool invalid_style_rejected = false;
    bool invalid_span_rejected = false;
    try {
        (*table).set_column_style(0U, {TableSizeMode::percent, 0.0});
    } catch (const std::invalid_argument&) {
        invalid_style_rejected = true;
    }
    try {
        (*table).set_row_span(*first, 0U);
    } catch (const std::out_of_range&) {
        invalid_span_rejected = true;
    }
    require(invalid_style_rejected && invalid_span_rejected,
            "table styles and spans must reject degenerate configuration before mutation");
}

void test_table_cell_dock_fill_consumes_growth() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("layout.table.fill"));
    (*table).set_column_count(1U);
    (*table).set_row_count(1U);
    (*table).set_column_style(0U, {TableSizeMode::percent, 100.0});
    (*table).set_row_style(0U, {TableSizeMode::percent, 100.0});
    std::shared_ptr<Button> child = sized_button("layout.table.fill.child", 120.0, 60.0);
    (*child).margin = Insets{};
    (*child).dock = DockStyle::fill;
    (*table).add_child(child);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{240.0, 120.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*child).arranged_bounds() == Rect{0.0, 0.0, 240.0, 120.0},
            "a table-cell Dock=Fill child must consume its complete initial cell");
    window.resize({640.0, 360.0});
    window.perform_layout();
    require((*child).arranged_bounds() == Rect{0.0, 0.0, 640.0, 360.0} &&
                (*child).requested_bounds() == Rect{0.0, 0.0, 120.0, 60.0},
            "a table-cell Dock=Fill child must consume later growth without rewriting authored bounds");
}

void test_table_track_spacing_is_relational_and_bounded() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> table =
        make_control<TableLayoutPanelCompat>(StableId("layout.table.spacing"));
    (*table).set_column_count(2U);
    (*table).set_row_count(1U);
    (*table).set_column_style(0U, {TableSizeMode::percent, 1.0});
    (*table).set_column_style(1U, {TableSizeMode::percent, 1.0});
    (*table).set_row_style(0U, {TableSizeMode::percent, 1.0});
    (*table).set_track_spacing({18.0, 0.0});
    std::shared_ptr<Button> first =
        sized_button("layout.table.spacing.first", 20.0, 20.0);
    std::shared_ptr<Button> second =
        sized_button("layout.table.spacing.second", 20.0, 20.0);
    (*first).dock = DockStyle::fill;
    (*second).dock = DockStyle::fill;
    (*table).add_child(first);
    (*table).add_child(second);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{218.0, 80.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*table).column_widths().size() == 2U &&
                near((*table).column_widths()[0], 100.0) &&
                near((*table).column_widths()[1], 100.0) &&
                (*first).arranged_bounds() == Rect{0.0, 0.0, 100.0, 80.0} &&
                (*second).arranged_bounds() == Rect{118.0, 0.0, 100.0, 80.0},
            "table track spacing must be removed before fractional allocation and retained between live cells");

    bool rejected{};
    try {
        (*table).set_track_spacing({257.0, 0.0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*table).track_spacing() == Size{18.0, 0.0},
            "table track spacing must reject invalid geometry atomically");
}

class GridPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    std::size_t lines{};
};

void test_table_border_paint_is_public_geometry() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> table = make_control<TableLayoutPanelCompat>(StableId("layout.table.paint"));
    (*table).set_column_count(2U);
    (*table).set_row_count(2U);
    (*table).set_cell_border_style(TableCellBorderStyle::inset);
    // Tracks with extent, so every rule has length: a rule of no length is
    // not drawn.
    (*table).set_column_style(0U, {TableSizeMode::percent, 50.0});
    (*table).set_column_style(1U, {TableSizeMode::percent, 50.0});
    (*table).set_row_style(0U, {TableSizeMode::percent, 50.0});
    (*table).set_row_style(1U, {TableSizeMode::percent, 50.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(table, Size{100.0, 60.0});
    Window& window = *held_window;
    window.perform_layout();
    GridPainter painter;
    (*table).on_paint(painter, {0.0, 0.0, 100.0, 60.0});
    require(painter.lines == 12U,
            "inset table borders must render two retained passes over every grid edge");
}

void test_scaled_panel_and_group_are_public_layout_controls() {
    std::shared_ptr<gui_forms::ScaledPanel> root = make_control<ScaledPanel>(StableId("layout.scaled.root"),
                                          Size{100.0, 100.0});
    std::shared_ptr<Button> child = sized_button("layout.scaled.child", 30.0, 40.0);
    (*root).add_at(child, {10.0, 20.0, 30.0, 40.0});
    std::shared_ptr<gui_forms::ScaledGroupBox> group = make_control<ScaledGroupBox>(
        StableId("layout.scaled.group"), "Scaled group", Size{50.0, 50.0});
    std::shared_ptr<Button> grouped = sized_button("layout.scaled.grouped", 10.0, 10.0);
    const Rect child_preferred = (*child).requested_bounds();
    const Rect grouped_preferred = (*grouped).requested_bounds();
    (*group).add_at(grouped, {5.0, 10.0, 20.0, 15.0});
    (*root).add_at(group, {50.0, 0.0, 50.0, 100.0});

    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});

    Window& window = *held_window;
    window.perform_layout();
    require((*child).arranged_bounds() == Rect{20.0, 20.0, 60.0, 40.0} &&
                (*group).arranged_bounds() == Rect{100.0, 0.0, 100.0, 100.0} &&
                (*grouped).arranged_bounds() == Rect{10.0, 20.0, 40.0, 30.0},
            "scaled public layouts must project authored slots through nested design spaces");
    require((*child).requested_bounds() == child_preferred &&
                (*grouped).requested_bounds() == grouped_preferred,
            "scaled layout must publish retained slots without overwriting authored bounds");
    require((*root).design_bounds(*child) ==
                std::optional(Rect{10.0, 20.0, 30.0, 40.0}) &&
                (*group).design_bounds(*grouped) ==
                std::optional(Rect{5.0, 10.0, 20.0, 15.0}),
            "scaled layouts must retain queryable authored geometry");

    (*root).set_design_bounds(*child, {20.0, 10.0, 40.0, 20.0});
    window.perform_layout();
    require((*child).arranged_bounds() == Rect{40.0, 10.0, 80.0, 20.0},
            "runtime slot mutation must relayout without a custom subclass");
    static_cast<void>((*root).remove_child((*child).runtime_id()));
    window.perform_layout();
    require(!(*root).design_bounds(*child),
            "scaled layouts must revoke detached child metadata deterministically");

    bool invalid_size_rejected = false;
    bool foreign_child_rejected = false;
    try {
        (*root).set_design_size({0.0, 100.0});
    } catch (const std::invalid_argument&) {
        invalid_size_rejected = true;
    }
    try {
        (*root).set_design_bounds(*child, {0.0, 0.0, 1.0, 1.0});
    } catch (const std::logic_error&) {
        foreign_child_rejected = true;
    }
    require(invalid_size_rejected && foreign_child_rejected,
            "scaled layouts must reject degenerate spaces and foreign slot mutation");
}

void test_default_dock_layout_all_directions_and_z_order() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("layout.dock.root"));
    (*root).padding = {10.0, 10.0, 10.0, 10.0};
    std::shared_ptr<Button> fill = sized_button("layout.dock.fill", 10.0, 10.0);
    std::shared_ptr<Button> bottom = sized_button("layout.dock.bottom", 10.0, 25.0);
    std::shared_ptr<Button> right = sized_button("layout.dock.right", 40.0, 10.0);
    std::shared_ptr<Button> top = sized_button("layout.dock.top", 10.0, 20.0);
    std::shared_ptr<Button> left = sized_button("layout.dock.left", 30.0, 10.0);
    (*fill).dock = DockStyle::fill;
    (*bottom).dock = DockStyle::bottom;
    (*right).dock = DockStyle::right;
    (*top).dock = DockStyle::top;
    (*left).dock = DockStyle::left;
    (*root).add_child(left);
    (*root).add_child(top);
    (*root).add_child(right);
    (*root).add_child(bottom);
    (*root).add_child(fill);

    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 200.0});

    Window& window = *held_window;
    window.perform_layout();
    require((*left).arranged_bounds() == Rect{10.0, 10.0, 30.0, 180.0} &&
                (*top).arranged_bounds() == Rect{40.0, 10.0, 250.0, 20.0} &&
                (*right).arranged_bounds() == Rect{250.0, 30.0, 40.0, 160.0} &&
                (*bottom).arranged_bounds() == Rect{40.0, 165.0, 210.0, 25.0} &&
                (*fill).arranged_bounds() == Rect{40.0, 30.0, 210.0, 135.0},
            "Dock must consume the padded client rectangle in reverse public z order");
    require((*fill).requested_bounds() == Rect{0.0, 0.0, 10.0, 10.0} &&
                (*left).requested_bounds() == Rect{0.0, 0.0, 30.0, 10.0} &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "Dock slots must preserve preferred bounds and converge");

    (*left).visible = false;
    window.perform_layout();
    require((*top).arranged_bounds() == Rect{10.0, 10.0, 280.0, 20.0} &&
                (*fill).arranged_bounds() == Rect{10.0, 30.0, 240.0, 135.0},
            "hidden docked controls must consume no client extent");

    std::shared_ptr<gui_forms::Panel> order_root = make_control<Panel>(StableId("layout.dock.order.root"));
    std::shared_ptr<Button> first = sized_button("layout.dock.order.first", 10.0, 20.0);
    std::shared_ptr<Button> second = sized_button("layout.dock.order.second", 10.0, 30.0);
    (*first).dock = DockStyle::top;
    (*second).dock = DockStyle::top;
    (*order_root).add_child(first);
    (*order_root).add_child(second);
    const std::unique_ptr<Window> held_order_window = std::make_unique<Window>(order_root, Size{100.0, 100.0});
    Window& order_window = *held_order_window;
    order_window.perform_layout();
    require((*first).arranged_bounds().y == 0.0 &&
                (*second).arranged_bounds().y == 20.0,
            "backmost docked sibling must consume its edge first");
    require((*order_root).set_child_index((*first).runtime_id(), 0U),
            "dock z-order specimen must accept child-index mutation");
    order_window.perform_layout();
    require((*second).arranged_bounds().y == 0.0 &&
                (*first).arranged_bounds().y == 30.0,
            "reverse-z Dock order must update deterministically after SetChildIndex");
}

void test_compound_anchor_resize_runtime_rebase_and_validation() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("layout.anchor.root"));
    (*root).padding = {10.0, 10.0, 10.0, 10.0};
    std::shared_ptr<Button> fixed = sized_button("layout.anchor.fixed", 50.0, 20.0);
    (*fixed).set_requested_bounds({20.0, 20.0, 50.0, 20.0});
    std::shared_ptr<Button> stretch = sized_button("layout.anchor.stretch", 100.0, 20.0);
    (*stretch).set_requested_bounds({20.0, 50.0, 100.0, 20.0});
    (*stretch).anchor = AnchorStyles::left | AnchorStyles::right |
                        AnchorStyles::top;
    std::shared_ptr<Button> bottom_right = sized_button("layout.anchor.bottom-right", 50.0, 20.0);
    (*bottom_right).set_requested_bounds({130.0, 60.0, 50.0, 20.0});
    (*bottom_right).anchor = AnchorStyles::right | AnchorStyles::bottom;
    std::shared_ptr<Button> centered = sized_button("layout.anchor.centered", 50.0, 20.0);
    (*centered).set_requested_bounds({75.0, 40.0, 50.0, 20.0});
    (*centered).anchor = AnchorStyles::none;
    (*root).add_child(fixed);
    (*root).add_child(stretch);
    (*root).add_child(bottom_right);
    (*root).add_child(centered);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();

    window.resize({300.0, 150.0});
    window.perform_layout();
    require((*fixed).arranged_bounds() == Rect{20.0, 20.0, 50.0, 20.0} &&
                (*stretch).arranged_bounds() == Rect{20.0, 50.0, 200.0, 20.0} &&
                (*bottom_right).arranged_bounds() == Rect{230.0, 110.0, 50.0, 20.0} &&
                (*centered).arranged_bounds() == Rect{125.0, 65.0, 50.0, 20.0},
            "compound Anchor flags must retain edge distances, stretch, or center per axis");
    require((*stretch).requested_bounds() == Rect{20.0, 50.0, 100.0, 20.0},
            "Anchor resize must not rewrite the authored preferred rectangle");

    (*fixed).anchor = AnchorStyles::right | AnchorStyles::bottom;
    window.resize({350.0, 180.0});
    window.perform_layout();
    require((*fixed).arranged_bounds() == Rect{70.0, 50.0, 50.0, 20.0},
            "runtime Anchor mutation must rebase from current arranged geometry");

    (*bottom_right).set_requested_bounds({200.0, 90.0, 60.0, 25.0});
    window.perform_layout();
    require((*bottom_right).arranged_bounds() == Rect{200.0, 90.0, 60.0, 25.0},
            "authored bounds mutation must reset an established anchor baseline");
    window.resize({370.0, 190.0});
    window.perform_layout();
    require((*bottom_right).arranged_bounds() == Rect{220.0, 100.0, 60.0, 25.0},
            "rebased right/bottom anchors must follow later parent resize deltas");

    bool invalid_dock_rejected = false;
    bool invalid_anchor_rejected = false;
    try {
        (*fixed).dock = static_cast<DockStyle>(255U);
    } catch (const std::invalid_argument&) {
        invalid_dock_rejected = true;
    }
    try {
        (*fixed).anchor = static_cast<AnchorStyles>(0x80U);
    } catch (const std::invalid_argument&) {
        invalid_anchor_rejected = true;
    }
    require(invalid_dock_rejected && invalid_anchor_rejected &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "Dock/Anchor must reject unknown values and remain convergence-bounded");
}

void test_flow_auto_scroll_owns_viewport_and_input() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("flow.scroll"));
    (*flow).auto_scroll = true;
    (*flow).set_wrap_contents(false);
    (*flow).set_item_spacing({4.0, 4.0});
    std::vector<std::shared_ptr<Button>> items;
    for (int index = 0; index < 8; ++index) {
        std::shared_ptr<Button> item = make_control<Button>(StableId("flow.item." + std::to_string(index)), "Frame");
        (*item).set_requested_bounds({0.0, 0.0, 82.0, 76.0});
        (*flow).add_child(item);
        items.push_back(item);
    }
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{300.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*flow).hscroll() && !(*flow).vscroll() &&
                (*flow).scroll_snapshot().viewport_rectangle.width == 300.0,
            "unwrapped flow initializes a horizontal scroll viewport");
    require(window.hit_test({20.0, 20.0}) == items[0],
            "scrolled flow children remain visible to hit testing");
    (*flow).scroll_control_into_view(items.back());
    window.perform_layout();
    const Rect last = (*items.back()).absolute_bounds();
    require((*flow).scroll_position().x > 0.0 && last.right() <= 300.001 &&
                window.hit_test({last.x + 10.0, last.y + 10.0}) == items.back(),
            "scroll into view moves retained flow geometry and routed input together");
    (*flow).set_wrap_contents(true);
    window.resize({180.0, 100.0});
    window.perform_layout();
    require(!(*flow).hscroll() && (*flow).vscroll() &&
                (*items[1]).committed_arranged_bounds().y > (*items[0]).committed_arranged_bounds().y,
            "wrapped flow remeasures rows after vertical scrollbar consumes width");
    (*flow).scroll_control_into_view(items.back());
    window.perform_layout();
    require((*flow).scroll_position().y > 0.0,
            "wrapped flow scrolls to later rows");
    window.resize({900.0, 400.0});
    window.perform_layout();
    require(!(*flow).hscroll() && !(*flow).vscroll() && (*flow).scroll_position() == Point{} &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "resizing resets obsolete scroll offsets without a layout loop");
}

} // namespace

int main() {
    try {
        test_flow_auto_scroll_owns_viewport_and_input();
        test_margin_padding_validation_and_retained_slots();
        test_flow_break_child_property();
        test_flow_direction_break_visibility_and_resize();
        test_flow_item_spacing_is_explicit_bounded_layout_state();
        test_flow_auto_size_mode_preserves_authored_extent();
        test_flow_alignment_projects_relational_flex_geometry();
        test_layout_panels_revalidate_snapshot_after_measure_callback();
        test_table_mixed_tracks_spans_and_lookup();
        test_table_growth_hidden_children_and_fixed_overflow();
        test_table_cell_dock_fill_consumes_growth();
        test_table_track_spacing_is_relational_and_bounded();
        test_table_border_paint_is_public_geometry();
        test_scaled_panel_and_group_are_public_layout_controls();
        test_default_dock_layout_all_directions_and_z_order();
        test_compound_anchor_resize_runtime_rebase_and_validation();
        std::cout << "layout-panel-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "layout-panel-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
