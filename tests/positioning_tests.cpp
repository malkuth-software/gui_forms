// Stage L, phase L5: positioning in the CSS containers (PLAN sections 2.5
// and 3.3, decision L-Q6). The core arithmetic of absolutely positioned
// boxes and relative offsets; absolute children of FlexPanel,
// GridPanel and BlockPanel, out of the flow and placed in their
// containing block; the containing_block_not_parent warning, recorded once
// per change of resolution; and the containing-block dependency: resizing
// the ancestor moves a `bottom: 0` grandchild in one arrange pass, removing
// the ancestor's positioning re-resolves to the next one with a new
// warning, disposing the ancestor leaves no stale dependent, and above
// 4,096 dependents the ancestor arranges its whole subtree instead.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/layout/positioning/positioning.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
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

std::shared_ptr<Panel> box(const std::string& id) {
    std::shared_ptr<Panel> made = make_control<Panel>(StableId(id));
    (*made).margin = {0.0, 0.0, 0.0, 0.0};
    return made;
}

template <typename Container> std::shared_ptr<Container> container(const std::string& id) {
    std::shared_ptr<Container> made = make_control<Container>(StableId(id));
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

AutoLengthPercentage inset_px(double value) {
    const AutoLengthPercentage inset{false, LengthPercentage{LogicalLength{value}, 0.0}};
    return inset;
}

AutoLengthPercentage inset_percent(double value) {
    const AutoLengthPercentage inset{false, LengthPercentage{LogicalLength{}, value}};
    return inset;
}

const AutoLengthPercentage inset_auto{true, {}};

// `position: absolute` with the given insets (top, right, bottom, left).
ItemPositioning absolute(AutoLengthPercentage top, AutoLengthPercentage right, AutoLengthPercentage bottom,
                         AutoLengthPercentage left) {
    ItemPositioning positioning{};
    positioning.position = PositionKind::absolute;
    positioning.inset = BoxSides<AutoLengthPercentage>{top, right, bottom, left};
    return positioning;
}

ItemPositioning relative(AutoLengthPercentage top, AutoLengthPercentage left) {
    ItemPositioning positioning{};
    positioning.position = PositionKind::relative;
    positioning.inset.top = top;
    positioning.inset.left = left;
    return positioning;
}

// -- the core arithmetic ----------------------------------------------------------

AbsoluteAxisInput axis(double extent, AutoLengthPercentage start, AutoLengthPercentage end) {
    AbsoluteAxisInput input{};
    input.containing_extent = extent;
    input.start_inset = start;
    input.end_inset = end;
    return input;
}

void require_axis(const AbsoluteAxisInput& input, double position, double size, const std::string& what) {
    const AbsoluteAxisResult result = resolve_absolute_axis(input);
    require(near(result.position, position) && near(result.size, size),
            what + ": got " + std::to_string(result.position) + ", " + std::to_string(result.size));
}

void test_absolute_axis_arithmetic() {
    // Both insets stretch an `auto` size; margins come off it.
    AbsoluteAxisInput stretched = axis(200.0, inset_px(10.0), inset_px(20.0));
    stretched.margin_start = 5.0;
    require_axis(stretched, 15.0, 165.0, "both insets stretch");
    require(!absolute_axis_reads_content(stretched), "a stretched axis does not read its content");

    // One inset and a size; the end inset places from the far edge.
    AbsoluteAxisInput sized = axis(200.0, inset_auto, inset_px(20.0));
    sized.definite_size = true;
    sized.size = 50.0;
    require_axis(sized, 130.0, 50.0, "right and a width");

    // Shrink-to-fit: the max-content width when it fits, the available
    // width between min-content and max-content when it does not.
    AbsoluteAxisInput fitted = axis(200.0, inset_auto, inset_px(20.0));
    fitted.content_minimum = 30.0;
    fitted.content_maximum = 80.0;
    require(absolute_axis_reads_content(fitted), "a one-inset auto axis reads its content");
    require_axis(fitted, 100.0, 80.0, "shrink-to-fit at max-content");
    fitted.containing_extent = 70.0;
    require_axis(fitted, 0.0, 50.0, "shrink-to-fit at the available width");

    // Auto margins with both insets and a size centre the box; with none
    // the box is over-constrained and keeps its start inset, or its end
    // inset right to left.
    AbsoluteAxisInput centred = axis(200.0, inset_px(0.0), inset_px(0.0));
    centred.definite_size = true;
    centred.size = 50.0;
    centred.auto_margin_start = true;
    centred.auto_margin_end = true;
    require_axis(centred, 75.0, 50.0, "auto margins centre");
    AbsoluteAxisInput constrained = axis(200.0, inset_px(10.0), inset_px(10.0));
    constrained.definite_size = true;
    constrained.size = 50.0;
    require_axis(constrained, 10.0, 50.0, "over-constrained keeps the start inset");
    constrained.end_wins = true;
    require_axis(constrained, 140.0, 50.0, "over-constrained right to left keeps the end inset");

    // Percentages are of the containing block; a minimum wins over a
    // maximum; both `auto` places at the static position.
    AbsoluteAxisInput bounded = axis(400.0, inset_percent(25.0), inset_percent(25.0));
    bounded.maximum = 100.0;
    bounded.minimum = 150.0;
    require_axis(bounded, 100.0, 150.0, "percent insets, the minimum wins");
    AbsoluteAxisInput placed = axis(400.0, inset_auto, inset_auto);
    placed.definite_size = true;
    placed.size = 30.0;
    placed.static_position = 8.0;
    placed.margin_start = 2.0;
    require_axis(placed, 10.0, 30.0, "the static position");
    placed.static_from_end = true;
    placed.static_position = 392.0;
    placed.margin_end = 4.0;
    require_axis(placed, 358.0, 30.0, "the static position's end edge");

    // Relative offsets: the start side and top win; percentages of the
    // containing block.
    ItemPositioning shifted{};
    shifted.position = PositionKind::relative;
    shifted.inset.left = inset_px(10.0);
    shifted.inset.right = inset_px(5.0);
    shifted.inset.top = inset_percent(10.0);
    shifted.inset.bottom = inset_px(9.0);
    const Point left_to_right = relative_position_offset(shifted, Size{200.0, 100.0}, LayoutDirection::left_to_right);
    require(near(left_to_right.x, 10.0) && near(left_to_right.y, 10.0), "relative: left and top win");
    const Point right_to_left = relative_position_offset(shifted, Size{200.0, 100.0}, LayoutDirection::right_to_left);
    require(near(right_to_left.x, -5.0), "relative right to left: right wins");
    shifted.inset.top = inset_auto;
    const Point upward = relative_position_offset(shifted, Size{200.0, 100.0}, LayoutDirection::left_to_right);
    require(near(upward.y, -9.0), "relative: bottom without top");
    shifted.position = PositionKind::absolute;
    const Point none = relative_position_offset(shifted, Size{200.0, 100.0}, LayoutDirection::left_to_right);
    require(near(none.x, 0.0) && near(none.y, 0.0), "an absolute item has no relative offset");
    std::cout << "positioning-tests: the absolute axis and relative offsets\n";
}

// -- absolute and relative children in each container -------------------------------

void test_flex_children() {
    std::shared_ptr<FlexPanel> root = container<FlexPanel>("pos.flex");
    (*root).padding = {10.0, 10.0, 10.0, 10.0};
    std::shared_ptr<Panel> first = box("pos.flex.a");
    std::shared_ptr<Panel> pinned = box("pos.flex.pinned");
    std::shared_ptr<Panel> second = box("pos.flex.b");
    (*root).add_child(first);
    (*root).add_child(pinned);
    (*root).add_child(second);
    FlexItemLayout item{};
    item.width = px(50.0);
    item.height = px(20.0);
    (*root).set_item_layout(*first, item);
    item.positioning = relative(inset_px(3.0), inset_px(5.0));
    (*root).set_item_layout(*second, item);
    FlexItemLayout corner{};
    corner.width = px(40.0);
    corner.height = px(20.0);
    corner.positioning = absolute(inset_auto, inset_px(0.0), inset_px(0.0), inset_auto);
    (*root).set_item_layout(*pinned, corner);
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{400.0, 300.0});
    Window& window = *held;
    window.perform_layout();
    require_bounds(*first, 10.0, 10.0, 50.0, 20.0, "the first in-flow item");
    require_bounds(*second, 65.0, 13.0, 50.0, 20.0, "the relative item, shifted; the absolute item takes no space");
    require_bounds(*pinned, 360.0, 280.0, 40.0, 20.0, "the absolute item in the padding box's corner");
    require((*root).laid_out_children().size() == 2U, "the absolute child is outside the solve");
    require(window.layout_diagnostics().empty(), "the window root is its own children's containing block");
    require(window.containing_block_watch_count() == 0U, "a container that is its own block watches nothing");
    require((*root).item_positioning(*pinned).position == PositionKind::absolute, "item_positioning reads the record");

    // A non-finite inset is refused and leaves the record as it was.
    FlexItemLayout refused = corner;
    refused.positioning.inset.left = inset_px(std::numeric_limits<double>::infinity());
    bool threw = false;
    try {
        (*root).set_item_layout(*pinned, refused);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*root).item_layout(*pinned) == corner, "a non-finite inset is refused");
    std::cout << "positioning-tests: flex: absolute out of flow, relative offset\n";
}

void test_grid_and_block_children() {
    std::shared_ptr<GridPanel> grid = container<GridPanel>("pos.grid");
    std::shared_ptr<Panel> cell = box("pos.grid.cell");
    std::shared_ptr<Panel> band = box("pos.grid.band");
    (*grid).add_child(cell);
    (*grid).add_child(band);
    GridItemLayout band_layout{};
    band_layout.width = percent(50.0);
    band_layout.positioning = absolute(inset_px(10.0), inset_auto, inset_px(10.0), inset_percent(25.0));
    (*grid).set_item_layout(*band, band_layout);
    const std::unique_ptr<Window> grid_window = std::make_unique<Window>(grid, Size{400.0, 300.0});
    (*grid_window).perform_layout();
    require_bounds(*band, 100.0, 10.0, 200.0, 280.0, "a percentage inset and width, the height stretched");
    require_bounds(*cell, 0.0, 0.0, 400.0, 300.0, "the in-flow item has the grid to itself");

    // Both insets `auto`: the static position, the content box's start
    // corner (its end edge right to left).
    std::shared_ptr<BlockPanel> flow = container<BlockPanel>("pos.block");
    (*flow).padding = {8.0, 8.0, 8.0, 8.0};
    std::shared_ptr<Panel> stacked = box("pos.block.stacked");
    std::shared_ptr<Panel> loose = box("pos.block.loose");
    (*flow).add_child(stacked);
    (*flow).add_child(loose);
    BlockFlowItemLayout stacked_layout{};
    stacked_layout.height = px(20.0);
    (*flow).set_item_layout(*stacked, stacked_layout);
    BlockFlowItemLayout loose_layout{};
    loose_layout.width = px(30.0);
    loose_layout.height = px(10.0);
    loose_layout.positioning = absolute(inset_auto, inset_auto, inset_auto, inset_auto);
    (*flow).set_item_layout(*loose, loose_layout);
    const std::unique_ptr<Window> flow_window = std::make_unique<Window>(flow, Size{400.0, 300.0});
    (*flow_window).perform_layout();
    require_bounds(*stacked, 8.0, 8.0, 384.0, 20.0, "the block item fills the content width");
    require_bounds(*loose, 8.0, 8.0, 30.0, 10.0, "the static position");
    (*flow).layout_direction = LayoutDirection::right_to_left;
    (*flow_window).perform_layout();
    require_bounds(*loose, 362.0, 8.0, 30.0, 10.0, "the static position right to left");
    std::cout << "positioning-tests: grid and block flow absolute children\n";
}

// -- the containing block of a grandchild ---------------------------------------------

// root (block flow, the window root) > ancestor (flex, `position: relative`,
// 200 high) > middle (grid, 100 by 100 at the ancestor's start) > pinned
// (absolute, left 0 and bottom 0, 30 by 10).
struct Grandchild final {
    std::shared_ptr<BlockPanel> root{};
    std::shared_ptr<FlexPanel> ancestor{};
    std::shared_ptr<GridPanel> middle{};
    std::shared_ptr<Panel> pinned{};
    std::unique_ptr<Window> window{};
};

BlockFlowItemLayout ancestor_layout(double height, bool positioned) {
    BlockFlowItemLayout layout{};
    layout.height = px(height);
    if (positioned) layout.positioning.position = PositionKind::relative;
    return layout;
}

Grandchild build_grandchild() {
    Grandchild built{};
    built.root = container<BlockPanel>("pos.root");
    built.ancestor = container<FlexPanel>("pos.ancestor");
    built.middle = container<GridPanel>("pos.middle");
    built.pinned = box("pos.pinned");
    (*built.root).add_child(built.ancestor);
    (*built.ancestor).add_child(built.middle);
    (*built.middle).add_child(built.pinned);
    (*built.root).set_item_layout(*built.ancestor, ancestor_layout(200.0, true));
    FlexItemLayout middle_layout{};
    middle_layout.width = px(100.0);
    middle_layout.height = px(100.0);
    (*built.ancestor).set_item_layout(*built.middle, middle_layout);
    GridItemLayout pinned_layout{};
    pinned_layout.width = px(30.0);
    pinned_layout.height = px(10.0);
    pinned_layout.positioning = absolute(inset_auto, inset_auto, inset_px(0.0), inset_px(0.0));
    (*built.middle).set_item_layout(*built.pinned, pinned_layout);
    built.window = std::make_unique<Window>(built.root, Size{800.0, 600.0});
    (*built.window).perform_layout();
    return built;
}

void require_warning(const Window& window, std::size_t index, const std::string& ancestor) {
    const std::span<const LayoutDiagnostic> diagnostics = window.layout_diagnostics();
    require(index < diagnostics.size(), "warning " + std::to_string(index) + " is recorded");
    const LayoutDiagnostic& diagnostic = diagnostics[index];
    require(diagnostic.code == LayoutDiagnosticCode::containing_block_not_parent &&
                diagnostic.severity == LayoutDiagnosticSeverity::warning,
            "the warning is containing_block_not_parent");
    require(diagnostic.child == "pos.pinned" && diagnostic.ancestor == ancestor,
            "the warning names the child and " + ancestor + ", got " + diagnostic.ancestor);
}

void test_resizing_the_ancestor_moves_the_grandchild() {
    Grandchild built = build_grandchild();
    Window& window = *built.window;
    require_bounds(*built.middle, 0.0, 0.0, 100.0, 100.0, "the middle container");
    require_bounds(*built.pinned, 0.0, 190.0, 30.0, 10.0, "bottom 0 in the ancestor's box, in the parent's coordinates");
    require(window.layout_diagnostics().size() == 1U, "one warning");
    require_warning(window, 0U, "pos.ancestor");
    require(layout_diagnostic_code_name(LayoutDiagnosticCode::containing_block_not_parent) ==
                "containing_block_not_parent",
            "the code's name");
    require(window.containing_block_dependent_count((*built.ancestor).runtime_id()) == 1U,
            "the ancestor has the middle container as its dependent");

    // The middle container's own slot does not change; the dependency
    // arranges it in the same pass.
    window.reset_activity_metrics();
    (*built.root).set_item_layout(*built.ancestor, ancestor_layout(300.0, true));
    window.perform_layout();
    const MetricsSnapshot resized = window.metrics_snapshot();
    require_bounds(*built.middle, 0.0, 0.0, 100.0, 100.0, "the middle container stays");
    require_bounds(*built.pinned, 0.0, 290.0, 30.0, 10.0, "the grandchild follows the ancestor's bottom");
    require(resized.arrange_passes == 1U, "in one arrange pass, got " + std::to_string(resized.arrange_passes));
    require(window.layout_diagnostics().size() == 1U, "an unchanged resolution records nothing");

    // Disposing the ancestor leaves no dependent behind.
    const RuntimeId ancestor_id = (*built.ancestor).runtime_id();
    const Control::Ptr removed = (*built.root).remove_child(ancestor_id);
    require(removed != nullptr, "the ancestor is removed");
    (*removed).dispose();
    window.perform_layout();
    require(window.containing_block_dependent_count(ancestor_id) == 0U, "no stale dependent on the ancestor");
    require(window.containing_block_watch_count() == 0U, "no watch is left");
    std::cout << "positioning-tests: the ancestor's resize moves a bottom 0 grandchild in one arrange pass; "
                 "disposing it leaves no dependent\n";
}

void test_removing_positioning_re_resolves() {
    Grandchild built = build_grandchild();
    Window& window = *built.window;
    (*built.root).set_item_layout(*built.ancestor, ancestor_layout(200.0, false));
    window.perform_layout();
    // The next box up is the window's root, whose box is the client area.
    require_bounds(*built.pinned, 0.0, 590.0, 30.0, 10.0, "bottom 0 in the client area");
    require(window.layout_diagnostics().size() == 2U, "the new resolution records a new warning");
    require_warning(window, 1U, "pos.root");
    require(window.containing_block_dependent_count((*built.root).runtime_id()) == 1U &&
                window.containing_block_dependent_count((*built.ancestor).runtime_id()) == 1U,
            "the middle container depends on the root and on the ancestor between");

    // Positioned again: back to the ancestor, warned again.
    (*built.root).set_item_layout(*built.ancestor, ancestor_layout(200.0, true));
    window.perform_layout();
    require_bounds(*built.pinned, 0.0, 190.0, 30.0, 10.0, "bottom 0 in the ancestor's box again");
    require(window.layout_diagnostics().size() == 3U, "a third resolution, a third warning");
    require_warning(window, 2U, "pos.ancestor");
    require(window.containing_block_dependent_count((*built.root).runtime_id()) == 0U,
            "the root's watch is withdrawn");

    // The middle container positioned: its own box, and no warning.
    FlexItemLayout middle_layout{};
    middle_layout.width = px(100.0);
    middle_layout.height = px(100.0);
    middle_layout.positioning.position = PositionKind::relative;
    (*built.ancestor).set_item_layout(*built.middle, middle_layout);
    window.perform_layout();
    require_bounds(*built.pinned, 0.0, 90.0, 30.0, 10.0, "bottom 0 in its parent's box");
    require(window.layout_diagnostics().size() == 3U, "the parent as the block records nothing");
    require(window.containing_block_watch_count() == 0U, "the middle container withdraws its dependencies");
    window.clear_layout_diagnostics();
    require(window.layout_diagnostics().empty() && window.dropped_layout_diagnostics() == 0U, "clearing empties both");
    std::cout << "positioning-tests: removing positioning re-resolves to the next box with a new warning\n";
}

// 4,097 containers, each with an absolute child, under one positioned
// ancestor: the ancestor keeps 4,096 dependents and then arranges its
// whole subtree; the window keeps 1,000 warnings and counts the rest.
void test_dependent_and_diagnostic_limits() {
    constexpr std::size_t count = maximum_containing_block_dependents + 1U;
    std::shared_ptr<BlockPanel> root = container<BlockPanel>("pos.many");
    std::shared_ptr<FlexPanel> ancestor = container<FlexPanel>("pos.many.ancestor");
    (*ancestor).flex_wrap = FlexWrap::wrap;
    // Lines packed at the start, so the ancestor's resize moves no holder.
    (*ancestor).align_content = LayoutAlignment::flex_start;
    (*root).add_child(ancestor);
    (*root).set_item_layout(*ancestor, ancestor_layout(500.0, true));
    std::vector<std::shared_ptr<FlexPanel>> holders;
    std::vector<std::shared_ptr<Panel>> pins;
    holders.reserve(count);
    pins.reserve(count);
    FlexItemLayout holder_layout{};
    holder_layout.width = px(10.0);
    holder_layout.height = px(4.0);
    FlexItemLayout pin_layout{};
    pin_layout.width = px(1.0);
    pin_layout.height = px(1.0);
    pin_layout.positioning = absolute(inset_auto, inset_auto, inset_px(0.0), inset_px(0.0));
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string id = "pos.many." + std::to_string(index);
        std::shared_ptr<FlexPanel> holder = container<FlexPanel>(id);
        std::shared_ptr<Panel> pin = box(id + ".pin");
        (*holder).add_child(pin);
        (*holder).set_item_layout(*pin, pin_layout);
        (*ancestor).add_child(holder);
        (*ancestor).set_item_layout(*holder, holder_layout);
        holders.push_back(holder);
        pins.push_back(pin);
    }
    const std::unique_ptr<Window> held = std::make_unique<Window>(root, Size{800.0, 600.0});
    Window& window = *held;
    window.perform_layout();
    require(window.layout_diagnostics().size() == maximum_layout_diagnostics, "1000 warnings are kept");
    require(window.dropped_layout_diagnostics() == count - maximum_layout_diagnostics, "the rest are counted");
    require(window.containing_block_dependent_count((*ancestor).runtime_id()) == maximum_containing_block_dependents,
            "the ancestor keeps 4096 dependents");

    window.reset_activity_metrics();
    (*root).set_item_layout(*ancestor, ancestor_layout(560.0, true));
    window.perform_layout();
    require(window.containing_block_subtree_arranges() >= 1U, "the overflowed ancestor arranged its subtree");
    const std::uint64_t passes = window.metrics_snapshot().arrange_passes;
    require(passes == 1U, "in one arrange pass, got " + std::to_string(passes));
    const std::array<std::size_t, 3> sampled{0U, 2'048U, count - 1U};
    for (const std::size_t index : sampled) {
        const Rect holder = (*holders[index]).arranged_bounds();
        const Rect pin = (*pins[index]).arranged_bounds();
        require(near(holder.y + pin.y, 559.0), "pin " + std::to_string(index) + " sits on the ancestor's bottom, got " +
                                                   std::to_string(holder.y + pin.y));
    }
    std::cout << "positioning-tests: " << count << " dependents: 4096 kept, then the subtree; 1000 warnings kept, "
              << window.dropped_layout_diagnostics() << " counted\n";
}

} // namespace

int main() {
    try {
        test_absolute_axis_arithmetic();
        test_flex_children();
        test_grid_and_block_children();
        test_resizing_the_ancestor_moves_the_grandchild();
        test_removing_positioning_re_resolves();
        test_dependent_and_diagnostic_limits();
    } catch (const std::exception& error) {
        std::cerr << "positioning-tests: fail: " << error.what() << '\n';
        return 1;
    }
    std::cout << "positioning-tests: pass\n";
    return 0;
}
