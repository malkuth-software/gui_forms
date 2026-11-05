// Stage L, phase L1: the measure and arrange contract. Layout constraints and
// intrinsic queries, the per-control measure cache and its counters, layout
// boundaries, LayoutDirection, Label's min- and max-content sizes, and
// identical logical boxes at four display scales.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap so the sanitizer build's inlined
// test frames stay small (see layout_panel_tests.cpp).

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right, double tolerance = 1.0e-9) {
    return std::abs(left - right) <= tolerance;
}

// Counts measure(Size) callbacks and keeps the last `available` it saw.
class CountingControl : public Control {
public:
    explicit CountingControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        ++measure_calls;
        last_available = available;
        const Size measured = Control::measure(available);
        return measured;
    }

    void arrange(Rect final_bounds) override {
        ++arrange_calls;
        Control::arrange(final_bounds);
    }

    std::uint64_t measure_calls{};
    std::uint64_t arrange_calls{};
    Size last_available{};
};

// A container whose own size never reads its children: a layout boundary
// when its parent also places it without measuring it. Its arrange sizes
// each child through measure_for_layout at the container's width.
class BoundaryPanel final : public CountingControl {
public:
    explicit BoundaryPanel(StableId id) : CountingControl(std::move(id)) {}

    [[nodiscard]] bool size_depends_on_content() const noexcept override { return false; }

    void arrange(Rect final_bounds) override {
        ++arrange_calls;
        arrange_self(final_bounds);
        const std::vector<Ptr> retained = snapshot_layout_children();
        double top = 0.0;
        for (const Ptr& child : retained) {
            if (!is_current_layout_child(child)) continue;
            const LayoutConstraint constraint{
                definite_extent(LogicalLength{final_bounds.width}),
                at_most_extent(LogicalLength{final_bounds.height})};
            const Size measured = (*child).measure_for_layout(constraint);
            if (!is_current_layout_child(child)) continue;
            set_child_layout(child, {0.0, top, measured.width, measured.height});
            top += measured.height;
        }
    }
};

// A root that places its children from their requested bounds only.
class PlacingRoot final : public CountingControl {
public:
    PlacingRoot(StableId id, bool sizes_from_content)
        : CountingControl(std::move(id)), sizes_from_content_(sizes_from_content) {}

    [[nodiscard]] bool sizes_children_from_content() const noexcept override {
        return sizes_from_content_;
    }

private:
    bool sizes_from_content_{};
};

// Asks each child three different questions in one arrange, as a solver
// computing min-content, max-content and a definite-width height would.
class ProbingPanel final : public CountingControl {
public:
    explicit ProbingPanel(StableId id) : CountingControl(std::move(id)) {}

    void arrange(Rect final_bounds) override {
        ++arrange_calls;
        arrange_self(final_bounds);
        const std::vector<Ptr> retained = snapshot_layout_children();
        for (const Ptr& child : retained) {
            if (!is_current_layout_child(child)) continue;
            const Size minimum = (*child).measure_for_layout(
                LayoutConstraint{min_content_extent(), max_content_extent()});
            const Size maximum = (*child).measure_for_layout(
                LayoutConstraint{max_content_extent(), max_content_extent()});
            const Size fitted = (*child).measure_for_layout(
                LayoutConstraint{definite_extent(LogicalLength{final_bounds.width}),
                                 max_content_extent()});
            if (!is_current_layout_child(child)) continue;
            const double width = std::max(minimum.width, std::min(maximum.width, fitted.width));
            set_child_layout(child, {0.0, 0.0, width, fitted.height});
        }
    }
};

// Invalidates its own measure from inside its first measure callback.
class SelfInvalidatingControl final : public CountingControl {
public:
    explicit SelfInvalidatingControl(StableId id) : CountingControl(std::move(id)) {}

    Size measure(Size available) override {
        const Size measured = CountingControl::measure(available);
        if (measure_calls == 1U) invalidate(Dirty::measure);
        return measured;
    }
};

// Reports a measurement no layout can use.
class NonFiniteControl final : public Control {
public:
    explicit NonFiniteControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size) override {
        const Size measured{std::numeric_limits<double>::quiet_NaN(), -5.0};
        return measured;
    }
};

std::shared_ptr<CountingControl> counting_control(const char* id, double width, double height) {
    std::shared_ptr<CountingControl> control = std::make_shared<CountingControl>(StableId(std::string(id)));
    (*control).set_requested_bounds({0.0, 0.0, width, height});
    return control;
}

LayoutConstraint at_most(double width, double height) {
    const LayoutConstraint constraint = layout_constraint_at_most({width, height});
    return constraint;
}

void test_constraint_values_and_validation() {
    const LayoutConstraint content{min_content_extent(), max_content_extent()};
    require(valid_layout_constraint(content) && valid_layout_constraint(at_most(10.0, 0.0)),
            "content queries and nonnegative at-most extents must be valid");
    require(!valid_layout_constraint(at_most(-1.0, 10.0)) &&
                !valid_layout_constraint(LayoutConstraint{
                    definite_extent(LogicalLength{std::numeric_limits<double>::infinity()}),
                    max_content_extent()}) &&
                !valid_layout_constraint(LayoutConstraint{
                    AvailableExtent{AvailableExtentKind::min_content, LogicalLength{5.0}},
                    max_content_extent()}),
            "negative, infinite, or content extents carrying a length must be invalid");
    const Size available = measure_available_size(
        LayoutConstraint{definite_extent(LogicalLength{30.0}), min_content_extent()});
    require(available.width == 30.0 && available.height == practical_unbounded_extent,
            "definite extents pass through and content axes pass the practical unbounded extent");
    require(at_most(1.0, 2.0) == at_most(1.0, 2.0) && !(at_most(1.0, 2.0) == at_most(1.0, 3.0)) &&
                !(LayoutConstraint{definite_extent(LogicalLength{1.0}), max_content_extent()} ==
                  LayoutConstraint{at_most_extent(LogicalLength{1.0}), max_content_extent()}),
            "constraint equality must compare both kinds and extents");

    std::shared_ptr<CountingControl> control = counting_control("contract.validation", 10.0, 10.0);
    bool rejected = false;
    try {
        static_cast<void>((*control).measure_for_layout(at_most(-1.0, 10.0)));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*control).measure_calls == 0U,
            "an invalid constraint must be refused before any measure callback");
}

void test_base_mapping_reports_preferred_size_for_content_queries() {
    std::shared_ptr<CountingControl> control = counting_control("contract.base", 40.0, 20.0);
    const Size minimum = (*control).measure_for_layout(
        LayoutConstraint{min_content_extent(), min_content_extent()});
    require(minimum == Size{40.0, 20.0} &&
                (*control).last_available == Size{practical_unbounded_extent,
                                                  practical_unbounded_extent},
            "a control that cannot rewrap reports its preferred size as min-content");
    const Size maximum = (*control).measure_for_layout(
        LayoutConstraint{max_content_extent(), max_content_extent()});
    require(maximum == Size{40.0, 20.0}, "max-content must equal the preferred size");
    const Size definite = (*control).measure_for_layout(
        LayoutConstraint{definite_extent(LogicalLength{100.0}), max_content_extent()});
    require(definite == Size{100.0, 20.0}, "a definite axis must report exactly its extent");
    const Size fitted = (*control).measure_for_layout(at_most(10.0, 15.0));
    require(fitted == Size{10.0, 15.0}, "an at-most axis keeps today's measure(Size) clamp");
}

void test_cache_hits_misses_and_generation() {
    std::shared_ptr<CountingControl> root = counting_control("contract.cache.root", 200.0, 100.0);
    std::shared_ptr<CountingControl> child = counting_control("contract.cache.child", 50.0, 20.0);
    (*root).add_child(child);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    window.reset_activity_metrics();
    (*child).measure_calls = 0U;

    const LayoutConstraint question = at_most(30.0, 30.0);
    const Size first = (*child).measure_for_layout(question);
    const Size second = (*child).measure_for_layout(question);
    require(first == second && (*child).measure_calls == 1U,
            "the same question under the same generation must be answered from the cache");
    (*child).invalidate(Dirty::paint);
    static_cast<void>((*child).measure_for_layout(question));
    require((*child).measure_calls == 1U, "paint invalidation must keep the measure cache");
    const std::uint64_t generation = (*child).measure_generation();
    (*child).invalidate(Dirty::measure);
    require((*child).measure_generation() != generation,
            "measure invalidation must advance the measure generation");
    static_cast<void>((*child).measure_for_layout(question));
    require((*child).measure_calls == 2U, "measure invalidation must empty the cache");
    MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.measure_cache_hits == 2U && metrics.measure_cache_misses == 2U,
            "cache hits and misses must be counted");

    window.perform_layout();
    (*root).measure_calls = 0U;
    static_cast<void>((*root).measure_for_layout(question));
    static_cast<void>((*root).measure_for_layout(question));
    require((*root).measure_calls == 1U, "the parent's answer must be cached");
    (*child).invalidate(Dirty::measure);
    static_cast<void>((*root).measure_for_layout(question));
    require((*root).measure_calls == 2U,
            "a child's measure invalidation must make its ancestors' answers stale");
}

void test_cache_slots_keep_content_answers() {
    std::shared_ptr<CountingControl> control = counting_control("contract.slots", 40.0, 20.0);
    const LayoutConstraint minimum{min_content_extent(), max_content_extent()};
    const LayoutConstraint maximum{max_content_extent(), max_content_extent()};
    static_cast<void>((*control).measure_for_layout(minimum));
    static_cast<void>((*control).measure_for_layout(maximum));
    static_cast<void>((*control).measure_for_layout(at_most(10.0, 10.0)));
    static_cast<void>((*control).measure_for_layout(at_most(20.0, 10.0)));
    static_cast<void>((*control).measure_for_layout(at_most(30.0, 10.0)));
    require((*control).measure_calls == 5U, "five distinct questions must be five callbacks");
    static_cast<void>((*control).measure_for_layout(minimum));
    static_cast<void>((*control).measure_for_layout(maximum));
    static_cast<void>((*control).measure_for_layout(at_most(20.0, 10.0)));
    static_cast<void>((*control).measure_for_layout(at_most(30.0, 10.0)));
    require((*control).measure_calls == 5U,
            "content answers and the two most recent other answers must stay cached");
    static_cast<void>((*control).measure_for_layout(at_most(10.0, 10.0)));
    require((*control).measure_calls == 6U, "the oldest other answer must have been replaced");
}

void test_window_measure_is_retained_and_counted_per_pass() {
    std::shared_ptr<CountingControl> root = counting_control("contract.window.root", 120.0, 80.0);
    std::shared_ptr<CountingControl> child = counting_control("contract.window.child", 30.0, 10.0);
    (*root).add_child(child);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    window.perform_layout();
    const std::uint64_t calls = (*child).measure_calls;
    require(calls == 1U, "the window's measure pass must measure the child once");
    static_cast<void>((*child).measure_for_layout(at_most(120.0, 80.0)));
    require((*child).measure_calls == calls,
            "the window's own measure answer must be kept for its parent's arrange");

    std::shared_ptr<ProbingPanel> panel = std::make_shared<ProbingPanel>(StableId("contract.probe"));
    (*panel).set_requested_bounds({0.0, 0.0, 90.0, 60.0});
    std::shared_ptr<CountingControl> item = counting_control("contract.probe.item", 30.0, 10.0);
    (*panel).add_child(item);
    const std::unique_ptr<Window> held_probe_window = std::make_unique<Window>(panel, Size{90.0, 60.0});
    Window& probe_window = *held_probe_window;
    probe_window.perform_layout();
    require((*item).measure_calls == 4U &&
                probe_window.metrics_snapshot().maximum_measures_per_control_pass == 4U,
            "one window answer and three solver questions must be four callbacks in one pass");
    probe_window.reset_activity_metrics();
    (*item).invalidate(Dirty::measure);
    probe_window.perform_layout();
    const MetricsSnapshot metrics = probe_window.metrics_snapshot();
    require((*item).measure_calls == 8U && metrics.maximum_measures_per_control_pass == 4U &&
                metrics.measure_cache_misses == 5U,
            "the per-control count must restart with each layout pass");
}

void test_layout_boundary_stops_measure_propagation() {
    std::shared_ptr<PlacingRoot> root = std::make_shared<PlacingRoot>(StableId("contract.boundary.root"), false);
    (*root).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<BoundaryPanel> boundary = std::make_shared<BoundaryPanel>(StableId("contract.boundary"));
    (*boundary).set_requested_bounds({10.0, 10.0, 200.0, 100.0});
    std::shared_ptr<CountingControl> leaf = counting_control("contract.boundary.leaf", 50.0, 20.0);
    (*root).add_child(boundary);
    (*boundary).add_child(leaf);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*leaf).arranged_bounds() == Rect{0.0, 0.0, 200.0, 20.0},
            "the boundary panel must size its child through measure_for_layout");

    window.reset_activity_metrics();
    (*root).measure_calls = 0U;
    (*root).arrange_calls = 0U;
    (*boundary).measure_calls = 0U;
    (*boundary).arrange_calls = 0U;
    (*leaf).measure_calls = 0U;
    (*leaf).invalidate(Dirty::measure);
    require(has_dirty((*boundary).dirty(), Dirty::arrange) &&
                !has_dirty((*boundary).dirty(), Dirty::measure) &&
                !has_dirty((*root).dirty(), Dirty::layout),
            "measure dirtiness must stop at the boundary, which is marked for arrange only");
    window.perform_layout();
    require((*root).measure_calls == 0U && (*root).arrange_calls == 0U &&
                (*boundary).measure_calls == 0U && (*boundary).arrange_calls == 1U &&
                (*leaf).measure_calls == 2U,
            "only the leaf is re-measured and only the boundary re-arranged");
    require(window.metrics_snapshot().layout_boundary_stops == 1U,
            "the boundary stop must be counted");

    // A parent that sizes its children from their measure makes the same
    // container's size content-dependent, so propagation continues.
    std::shared_ptr<PlacingRoot> sizing_root =
        std::make_shared<PlacingRoot>(StableId("contract.sizing.root"), true);
    (*sizing_root).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    std::shared_ptr<BoundaryPanel> inner = std::make_shared<BoundaryPanel>(StableId("contract.sizing"));
    (*inner).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<CountingControl> inner_leaf = counting_control("contract.sizing.leaf", 50.0, 20.0);
    (*sizing_root).add_child(inner);
    (*inner).add_child(inner_leaf);
    const std::unique_ptr<Window> held_sizing_window =
        std::make_unique<Window>(sizing_root, Size{300.0, 200.0});
    Window& sizing_window = *held_sizing_window;
    sizing_window.perform_layout();
    (*sizing_root).measure_calls = 0U;
    (*inner_leaf).invalidate(Dirty::measure);
    require(has_dirty((*inner).dirty(), Dirty::measure) &&
                has_dirty((*sizing_root).dirty(), Dirty::measure),
            "without a boundary every ancestor must be marked for measure");
    sizing_window.perform_layout();
    require((*sizing_root).measure_calls == 1U, "the content-sizing root must be re-measured");
}

void test_detached_invalidation_advances_ancestors() {
    std::shared_ptr<CountingControl> parent = counting_control("contract.detached.parent", 80.0, 40.0);
    std::shared_ptr<CountingControl> child = counting_control("contract.detached.child", 20.0, 10.0);
    (*parent).add_child(child);
    static_cast<void>((*parent).measure_for_layout(at_most(80.0, 40.0)));
    const std::uint64_t calls = (*parent).measure_calls;
    (*child).invalidate(Dirty::measure);
    static_cast<void>((*parent).measure_for_layout(at_most(80.0, 40.0)));
    require((*parent).measure_calls == calls + 1U,
            "a detached child's measure invalidation must make its parent's answer stale");
    (*child).invalidate_subtree(Dirty::measure);
    static_cast<void>((*parent).measure_for_layout(at_most(80.0, 40.0)));
    require((*parent).measure_calls == calls + 2U,
            "a detached subtree invalidation must make its ancestors' answers stale");
}

void test_self_invalidation_and_clamps() {
    std::shared_ptr<SelfInvalidatingControl> control =
        std::make_shared<SelfInvalidatingControl>(StableId("contract.self"));
    (*control).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
    static_cast<void>((*control).measure_for_layout(at_most(10.0, 10.0)));
    static_cast<void>((*control).measure_for_layout(at_most(10.0, 10.0)));
    static_cast<void>((*control).measure_for_layout(at_most(10.0, 10.0)));
    require((*control).measure_calls == 2U,
            "an answer invalidated by its own callback must not be cached");

    std::shared_ptr<CountingControl> root = counting_control("contract.clamp.root", 50.0, 50.0);
    std::shared_ptr<NonFiniteControl> broken = std::make_shared<NonFiniteControl>(StableId("contract.clamp"));
    (*root).add_child(broken);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{50.0, 50.0});
    Window& window = *held_window;
    window.perform_layout();
    window.reset_activity_metrics();
    const Size measured = (*broken).measure_for_layout(at_most(40.0, 40.0));
    require(measured == Size{0.0, 0.0} && window.metrics_snapshot().layout_measure_clamps == 1U,
            "negative or non-finite measurements must be clamped to zero and counted");
}

void test_label_intrinsic_sizes() {
    std::shared_ptr<CountingControl> root = counting_control("contract.label.root", 400.0, 300.0);
    std::shared_ptr<Label> label = std::make_shared<Label>(StableId("contract.label"), "alpha beta gamma");
    (*label).set_text_wrapping(TextWrapping::word);
    std::shared_ptr<Label> single = std::make_shared<Label>(StableId("contract.label.single"),
                                                            "alpha beta\ngamma");
    std::shared_ptr<Label> fixed = std::make_shared<Label>(StableId("contract.label.fixed"), "alpha beta gamma");
    (*fixed).set_text_wrapping(TextWrapping::word);
    (*fixed).set_requested_bounds({0.0, 0.0, 50.0, 0.0});
    (*root).add_child(label);
    (*root).add_child(single);
    (*root).add_child(fixed);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{400.0, 300.0});
    Window& window = *held_window;
    window.perform_layout();

    const FontSpec font = (*label).effective_font((*label).font());
    double widest_word = 0.0;
    for (const char* word : {"alpha", "beta", "gamma"}) {
        widest_word = std::max(widest_word,
                               (*label).resolve_text_layout_utf8(word, font).logical_size.width);
    }
    const double whole_line = (*label).resolve_text_layout_utf8("alpha beta gamma", font).logical_size.width;
    const LayoutConstraint minimum{min_content_extent(), max_content_extent()};
    const LayoutConstraint maximum{max_content_extent(), max_content_extent()};
    const Size minimum_size = (*label).measure_for_layout(minimum);
    const Size maximum_size = (*label).measure_for_layout(maximum);
    require(near(minimum_size.width, widest_word + 4.0),
            "a wrapping label's min-content width is its widest word plus the inset");
    require(near(maximum_size.width, whole_line + 4.0),
            "a wrapping label's max-content width is its unwrapped line plus the inset");
    require(near(minimum_size.height - 4.0, 3.0 * (maximum_size.height - 4.0)),
            "at min-content width each word must take its own line, unbroken");
    const Size at_minimum = (*label).measure_for_layout(
        LayoutConstraint{definite_extent(LogicalLength{minimum_size.width}), max_content_extent()});
    require(near(at_minimum.height, minimum_size.height),
            "laying the label out at its min-content width must give the min-content height");

    const Size single_minimum = (*single).measure_for_layout(minimum);
    const Size single_maximum = (*single).measure_for_layout(maximum);
    const FontSpec single_font = (*single).effective_font((*single).font());
    const double paragraph = (*single).resolve_text_layout_utf8("alpha beta", single_font).logical_size.width;
    require(single_minimum == single_maximum && near(single_maximum.width, paragraph + 4.0),
            "a label that does not wrap answers both content queries with its widest paragraph");

    const Size fixed_minimum = (*fixed).measure_for_layout(minimum);
    const Size fixed_maximum = (*fixed).measure_for_layout(maximum);
    require(fixed_minimum == fixed_maximum && fixed_minimum.width == 50.0,
            "a label with a requested width answers both content queries at that width");
}

void test_layout_direction_inheritance_and_invalidation() {
    std::shared_ptr<CountingControl> root = counting_control("contract.direction.root", 200.0, 100.0);
    std::shared_ptr<CountingControl> child = counting_control("contract.direction.child", 100.0, 50.0);
    std::shared_ptr<CountingControl> grandchild = counting_control("contract.direction.grandchild", 20.0, 10.0);
    (*root).add_child(child);
    (*child).add_child(grandchild);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    require((*grandchild).layout_direction.type() == LayoutDirection::inherit &&
                (*grandchild).effective_layout_direction() == LayoutDirection::left_to_right,
            "an inheriting control under a default root lays out left to right");

    (*root).layout_direction = LayoutDirection::right_to_left;
    require((*child).effective_layout_direction() == LayoutDirection::right_to_left &&
                (*grandchild).effective_layout_direction() == LayoutDirection::right_to_left,
            "inheriting descendants must take the nearest explicit direction");
    require(has_dirty((*grandchild).dirty(), Dirty::arrange) &&
                !has_dirty((*grandchild).dirty(), Dirty::measure),
            "a direction change must re-arrange the subtree without re-measuring it");
    window.perform_layout();
    (*root).layout_direction = LayoutDirection::right_to_left;
    require(!has_dirty((*grandchild).dirty(), Dirty::layout),
            "setting the same direction must not invalidate");

    (*child).layout_direction = LayoutDirection::left_to_right;
    require((*grandchild).effective_layout_direction() == LayoutDirection::left_to_right &&
                (*root).effective_layout_direction() == LayoutDirection::right_to_left,
            "an explicit direction must override its ancestors for its own subtree only");

    std::shared_ptr<CountingControl> other = counting_control("contract.direction.other", 50.0, 50.0);
    (*other).layout_direction = LayoutDirection::right_to_left;
    const Control::Ptr moved = (*child).remove_child((*grandchild).runtime_id());
    require((*moved).effective_layout_direction() == LayoutDirection::left_to_right,
            "a detached root lays out left to right");
    (*other).add_child(moved);
    require((*moved).effective_layout_direction() == LayoutDirection::right_to_left,
            "a reparented control must follow its new parent");

    bool rejected = false;
    try {
        (*root).layout_direction = static_cast<LayoutDirection>(7);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*root).layout_direction.type() == LayoutDirection::right_to_left,
            "an out-of-range direction must be refused and leave the direction unchanged");
}

std::shared_ptr<Label> wrapping_label(const char* id, const char* text) {
    std::shared_ptr<Label> label = std::make_shared<Label>(StableId(std::string(id)), text);
    (*label).set_text_wrapping(TextWrapping::word);
    return label;
}

void collect_boxes(const Control::Ptr& control, std::vector<Rect>& boxes) {
    boxes.push_back((*control).absolute_bounds());
    for (const Control::Ptr& child : (*control).children()) {
        collect_boxes(child, boxes);
    }
}

void test_logical_boxes_identical_at_four_scales() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("contract.scale.root"));
    (*root).set_requested_bounds({0.0, 0.0, 420.0, 300.0});
    std::shared_ptr<FlowLayoutPanelCompat> flow = std::make_shared<FlowLayoutPanelCompat>(StableId("contract.scale.flow"));
    (*flow).set_requested_bounds({7.0, 5.0, 263.0, 170.0});
    (*flow).set_wrap_contents(true);
    (*flow).add_child(wrapping_label("contract.scale.flow.a", "Proportional advances decide wrapping"));
    std::shared_ptr<Button> button = std::make_shared<Button>(StableId("contract.scale.flow.b"), "Apply changes");
    (*button).auto_size = true;
    (*flow).add_child(button);
    (*flow).add_child(wrapping_label("contract.scale.flow.c", "short"));
    std::shared_ptr<Label> docked = wrapping_label("contract.scale.docked",
                                                   "A docked label wraps at the panel's width.");
    (*docked).dock = DockStyle::bottom;
    (*docked).set_requested_bounds({0.0, 0.0, 0.0, 41.0});
    std::shared_ptr<Label> anchored = wrapping_label("contract.scale.anchored", "Anchored text");
    (*anchored).set_requested_bounds({281.0, 9.0, 0.0, 0.0});
    (*root).add_child(flow);
    (*root).add_child(docked);
    (*root).add_child(anchored);

    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{420.0, 300.0});
    Window& window = *held_window;
    window.perform_layout();
    std::vector<Rect> reference;
    collect_boxes(root, reference);
    std::vector<Rect> scaled;
    scaled.reserve(reference.size());
    for (const double scale : {1.25, 1.5, 2.0, 1.0}) {
        window.set_scale(scale);
        window.perform_layout();
        scaled.clear();
        collect_boxes(root, scaled);
        require(scaled == reference,
                "logical boxes must be identical at display scales 1, 1.25, 1.5 and 2");
    }
}

} // namespace

int main() {
    try {
        test_constraint_values_and_validation();
        test_base_mapping_reports_preferred_size_for_content_queries();
        test_cache_hits_misses_and_generation();
        test_cache_slots_keep_content_answers();
        test_window_measure_is_retained_and_counted_per_pass();
        test_layout_boundary_stops_measure_propagation();
        test_detached_invalidation_advances_ancestors();
        test_self_invalidation_and_clamps();
        test_label_intrinsic_sizes();
        test_layout_direction_inheritance_and_invalidation();
        test_logical_boxes_identical_at_four_scales();
        std::cout << "layout-contract-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "layout-contract-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
