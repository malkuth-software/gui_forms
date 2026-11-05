// Positioned children of the CSS containers (stage L, phase L5): which box
// is an absolutely positioned child's containing block, the controls its
// position depends on, and the child's rectangle in its parent's
// coordinates, one axis at a time through the core positioning arithmetic.

#include "out_of_flow.hpp"

#include "gui_forms/controls/scrollable_control/container_control/block_panel/block_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flex_panel/flex_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/grid_panel/grid_panel.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gui_forms::out_of_flow {

namespace {

constexpr double unbounded = std::numeric_limits<double>::infinity();

bool admitted_inset(const AutoLengthPercentage& inset) noexcept {
    return std::isfinite(inset.value.length.value()) && std::isfinite(inset.value.percent);
}

double resolve_length(const LengthPercentage& value, double basis) noexcept {
    return value.length.value() + basis * value.percent / 100.0;
}

double resolve_minimum(const SizeValue& value, double basis) noexcept {
    if (value.kind != SizeKind::length_percentage) return 0.0;
    return std::max(0.0, resolve_length(value.value, basis));
}

double resolve_maximum(const SizeValue& value, double basis) noexcept {
    if (value.kind != SizeKind::length_percentage) return unbounded;
    return std::max(0.0, resolve_length(value.value, basis));
}

// A control's maximum dimension of zero is unbounded.
double control_maximum(double maximum) noexcept {
    return maximum > 0.0 ? maximum : unbounded;
}

Point scroll_of(const Control& control) noexcept {
    const ScrollableControl* const scrollable = dynamic_cast<const ScrollableControl*>(&control);
    if (scrollable == nullptr) return {};
    return (*scrollable).scroll_position();
}

} // namespace

bool admitted_positioning(const ItemPositioning& positioning) noexcept {
    if (positioning.position > PositionKind::absolute) return false;
    const BoxSides<AutoLengthPercentage>& inset = positioning.inset;
    return admitted_inset(inset.top) && admitted_inset(inset.right) && admitted_inset(inset.bottom) &&
           admitted_inset(inset.left);
}

PositionKind position_in_parent(const Control& control) noexcept {
    const Control::Ptr parent = control.parent();
    const Control* const owner = parent.get();
    if (owner == nullptr) return PositionKind::static_position;
    if (const FlexPanel* const flex = dynamic_cast<const FlexPanel*>(owner)) {
        return (*flex).item_positioning(control).position;
    }
    if (const GridPanel* const grid = dynamic_cast<const GridPanel*>(owner)) {
        return (*grid).item_positioning(control).position;
    }
    if (const BlockPanel* const flow = dynamic_cast<const BlockPanel*>(owner)) {
        return (*flow).item_positioning(control).position;
    }
    return PositionKind::static_position;
}

ContainingBlock resolve_containing_block(const Control& container) {
    // `origin` is the container's origin in the frame of `current`'s
    // children.
    std::shared_ptr<const Control> current = container.shared_from_this();
    Point origin{};
    while (position_in_parent(*current) == PositionKind::static_position) {
        Control::Ptr parent = (*current).parent();
        if (!parent) break;
        const Rect placed = (*current).committed_arranged_bounds();
        origin.x += placed.x;
        origin.y += placed.y;
        current = std::move(parent);
    }
    const Rect own = (*current).committed_arranged_bounds();
    const Point scroll = scroll_of(*current);
    const bool right_to_left = (*current).effective_layout_direction() == LayoutDirection::right_to_left;
    ContainingBlock block{};
    block.box = Rect{-scroll.x - origin.x, -scroll.y - origin.y, own.width, own.height};
    block.direction = right_to_left ? LayoutDirection::right_to_left : LayoutDirection::left_to_right;
    block.ancestor = std::move(current);
    return block;
}

void fill_dependency_path(const Control& container, const ContainingBlock& block, std::vector<RuntimeId>& ids) {
    ids.clear();
    if (block.ancestor.get() == &container) return;
    for (Control::Ptr current = container.parent(); current; current = (*current).parent()) {
        ids.push_back((*current).runtime_id());
        if (current.get() == block.ancestor.get()) break;
    }
}

Rect place_absolute_child(Control& child, const AbsoluteItem& item, const ContainingBlock& block,
                          const AbsoluteContainer& container) {
    const Rect box = block.box;
    const BoxSides<AutoLengthPercentage>& inset = item.positioning.inset;
    const Insets margin = child.margin.type();
    const Size control_minimum = child.minimum_size.type();
    const Size control_maximums = child.maximum_size.type();
    const bool right_to_left = container.direction == LayoutDirection::right_to_left;
    const bool block_right_to_left = block.direction == LayoutDirection::right_to_left;

    // The static position, in the containing block: the container's content
    // box's start corner (its end corner's x for right to left).
    const double content_left = container.padding.left - container.scroll.x;
    const double content_right = container.size.width - container.padding.right - container.scroll.x;
    const double content_top = container.padding.top - container.scroll.y;

    AbsoluteAxisInput horizontal{};
    horizontal.containing_extent = box.width;
    horizontal.start_inset = inset.left;
    horizontal.end_inset = inset.right;
    horizontal.definite_size = item.width.kind == SizeKind::length_percentage;
    horizontal.size = horizontal.definite_size ? std::max(0.0, resolve_length(item.width.value, box.width)) : 0.0;
    horizontal.minimum = std::max(resolve_minimum(item.min_width, box.width), control_minimum.width);
    horizontal.maximum =
        std::min(resolve_maximum(item.max_width, box.width), control_maximum(control_maximums.width));
    horizontal.margin_start = margin.left;
    horizontal.margin_end = margin.right;
    horizontal.auto_margin_start = item.auto_margin.left;
    horizontal.auto_margin_end = item.auto_margin.right;
    horizontal.static_position = (right_to_left ? content_right : content_left) - box.x;
    horizontal.static_from_end = right_to_left;
    horizontal.end_wins = block_right_to_left;
    if (absolute_axis_reads_content(horizontal)) {
        horizontal.content_minimum =
            child.measure_for_layout(LayoutConstraint{min_content_extent(), max_content_extent()}).width;
        horizontal.content_maximum =
            child.measure_for_layout(LayoutConstraint{max_content_extent(), max_content_extent()}).width;
    }
    const AbsoluteAxisResult across = resolve_absolute_axis(horizontal);

    AbsoluteAxisInput vertical{};
    vertical.containing_extent = box.height;
    vertical.start_inset = inset.top;
    vertical.end_inset = inset.bottom;
    vertical.definite_size = item.height.kind == SizeKind::length_percentage;
    vertical.size = vertical.definite_size ? std::max(0.0, resolve_length(item.height.value, box.height)) : 0.0;
    vertical.minimum = std::max(resolve_minimum(item.min_height, box.height), control_minimum.height);
    vertical.maximum =
        std::min(resolve_maximum(item.max_height, box.height), control_maximum(control_maximums.height));
    vertical.margin_start = margin.top;
    vertical.margin_end = margin.bottom;
    vertical.auto_margin_start = item.auto_margin.top;
    vertical.auto_margin_end = item.auto_margin.bottom;
    vertical.static_position = content_top - box.y;
    vertical.vertical = true;
    if (absolute_axis_reads_content(vertical)) {
        const AvailableExtent used_width = definite_extent(LogicalLength{across.size});
        const double content = child.measure_for_layout(LayoutConstraint{used_width, max_content_extent()}).height;
        vertical.content_minimum = content;
        vertical.content_maximum = content;
    }
    const AbsoluteAxisResult down = resolve_absolute_axis(vertical);

    const Rect placed{box.x + across.position, box.y + down.position, across.size, down.size};
    return placed;
}

} // namespace gui_forms::out_of_flow
