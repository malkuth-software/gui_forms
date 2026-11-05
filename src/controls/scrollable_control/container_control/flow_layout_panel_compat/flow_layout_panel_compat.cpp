#include "gui_forms/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat.hpp"
#include "../container_layout_utilities.hpp"
#include "../../scrollable_anatomy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

using namespace container_layout_detail;

FlowLayoutPanelCompat::FlowLayoutPanelCompat(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

void FlowLayoutPanelCompat::set_flow_direction(FlowDirection direction) {
    require_mutable();
    if (flow_direction_ == direction) return;
    flow_direction_ = direction;
    invalidate(invalidation::bounds);
}

void FlowLayoutPanelCompat::set_wrap_contents(bool wrap) {
    require_mutable();
    if (wrap_contents_ == wrap) return;
    wrap_contents_ = wrap;
    invalidate(invalidation::bounds);
}

void FlowLayoutPanelCompat::set_item_spacing(Size spacing) {
    require_mutable();
    if (!std::isfinite(spacing.width) || !std::isfinite(spacing.height) ||
        spacing.width < 0.0 || spacing.height < 0.0 ||
        spacing.width > 256.0 || spacing.height > 256.0) {
        throw std::invalid_argument(
            "FlowLayoutPanelCompat item spacing must be finite and between zero and 256");
    }
    if (item_spacing_ == spacing) return;
    item_spacing_ = spacing;
    invalidate(invalidation::bounds);
}

void FlowLayoutPanelCompat::set_main_alignment(FlowMainAlignment alignment) {
    require_mutable();
    if (alignment > FlowMainAlignment::space_evenly) {
        throw std::invalid_argument("FlowLayoutPanelCompat main alignment is unknown");
    }
    if (main_alignment_ == alignment) return;
    main_alignment_ = alignment;
    invalidate(invalidation::bounds);
}

void FlowLayoutPanelCompat::set_cross_alignment(FlowCrossAlignment alignment) {
    require_mutable();
    if (alignment > FlowCrossAlignment::stretch) {
        throw std::invalid_argument("FlowLayoutPanelCompat cross alignment is unknown");
    }
    if (cross_alignment_ == alignment) return;
    cross_alignment_ = alignment;
    invalidate(invalidation::bounds);
}

void FlowLayoutPanelCompat::set_flow_break(const Control& child, bool flow_break) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument(
            "FlowLayoutPanelCompat flow break requires a live direct child");
    }
    const std::uint64_t id = child.runtime_id().value;
    if (FlowLayoutPanelCompat::flow_break(child) == flow_break) return;
    records_[id].flow_break = flow_break;
    erase_default_record(id);
    invalidate(invalidation::bounds);
}

bool FlowLayoutPanelCompat::flow_break(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return false;
    const ItemMap::const_iterator found = records_.find(child.runtime_id().value);
    return found != records_.end() && (*found).second.flow_break;
}

void FlowLayoutPanelCompat::erase_default_record(std::uint64_t id) {
    const ItemMap::iterator found = records_.find(id);
    if (found == records_.end()) return;
    if (!(*found).second.flow_break && (*found).second.grow == 0.0) records_.erase(found);
}

void FlowLayoutPanelCompat::set_flex_grow(const Control& child, double grow) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument(
            "FlowLayoutPanelCompat flex grow requires a live direct child");
    }
    if (!std::isfinite(grow) || grow < 0.0 || grow > 1024.0) {
        throw std::invalid_argument(
            "FlowLayoutPanelCompat flex grow must be finite and between zero and 1024");
    }
    const std::uint64_t id = child.runtime_id().value;
    if (flex_grow(child) == grow) return;
    records_[id].grow = grow;
    erase_default_record(id);
    invalidate(invalidation::bounds);
}

double FlowLayoutPanelCompat::flex_grow(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return 0.0;
    const ItemMap::const_iterator found = records_.find(child.runtime_id().value);
    return found == records_.end() ? 0.0 : (*found).second.grow;
}

// Drops the records of children that have left, by marking the live ones
// with this pass's number (no per-pass set).
void FlowLayoutPanelCompat::reconcile_records(std::uint64_t pass) {
    if (records_.empty()) return;
    for (const Control::Ptr& child : children()) {
        if (!child || !(*child).is_alive() || (*child).parent().get() != this) continue;
        const ItemMap::iterator found = records_.find((*child).runtime_id().value);
        if (found != records_.end()) (*found).second.seen_pass = pass;
    }
    ItemMap::iterator entry = records_.begin();
    while (entry != records_.end()) {
        if ((*entry).second.seen_pass != pass) {
            entry = records_.erase(entry);
        } else {
            ++entry;
        }
    }
}

namespace {

// Where a line's first item starts along the line, and the extra space
// between items, for `count` items with `free` space left over.
struct MainPlacement final {
    double origin{};
    double spacing{};
};

MainPlacement place_along(FlowMainAlignment alignment, double free, std::size_t count) noexcept {
    MainPlacement placement{};
    switch (alignment) {
    case FlowMainAlignment::start:
        return placement;
    case FlowMainAlignment::center:
        placement.origin = free * 0.5;
        return placement;
    case FlowMainAlignment::end:
        placement.origin = free;
        return placement;
    case FlowMainAlignment::space_between:
        if (count > 1U) placement.spacing = free / static_cast<double>(count - 1U);
        return placement;
    case FlowMainAlignment::space_around:
        if (count != 0U) {
            placement.spacing = free / static_cast<double>(count);
            placement.origin = placement.spacing * 0.5;
        }
        return placement;
    case FlowMainAlignment::space_evenly:
        if (count != 0U) {
            placement.spacing = free / static_cast<double>(count + 1U);
            placement.origin = placement.spacing;
        }
        return placement;
    }
    // An alignment outside the enumeration packs the line at its start.
    return placement;
}

// Where an item starts across its line and how much of the line it takes:
// its `desired` extent, or the line less its `margin` when stretched.
// `item` is its desired extent with margins.
struct CrossPlacement final {
    double origin{};
    double extent{};
};

CrossPlacement place_across(FlowCrossAlignment alignment, double line, double item,
                            double desired, double margin) noexcept {
    CrossPlacement placement{0.0, desired};
    switch (alignment) {
    case FlowCrossAlignment::start:
        return placement;
    case FlowCrossAlignment::center:
        placement.origin = std::max(0.0, line - item) * 0.5;
        return placement;
    case FlowCrossAlignment::end:
        placement.origin = std::max(0.0, line - item);
        return placement;
    case FlowCrossAlignment::stretch:
        placement.extent = std::max(0.0, line - margin);
        return placement;
    }
    // An alignment outside the enumeration keeps the item at the line's start.
    return placement;
}

} // namespace

Size FlowLayoutPanelCompat::layout_children(Size available, bool assign) {
    if (workspace_busy_) {
        // A child's measure laid this panel out again inside a pass: the
        // nested pass takes its own storage, so the outer pass's survives.
        Workspace nested{};
        const Size nested_size = layout_pass(available, assign, nested);
        return nested_size;
    }
    workspace_busy_ = true;
    struct Release final {
        bool& busy;
        ~Release() { busy = false; }
    } release{workspace_busy_};
    const Size size = layout_pass(available, assign, workspace_);
    return size;
}

Size FlowLayoutPanelCompat::layout_pass(Size available, bool assign, Workspace& workspace) {
    reconcile_records(++pass_);
    const Insets inset = padding.read();
    const Size inner{std::max(0.0, available.width - horizontal_extent(inset)),
                     std::max(0.0, available.height - vertical_extent(inset))};
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    const bool horizontal = flow_direction_ == FlowDirection::left_to_right ||
                            flow_direction_ == FlowDirection::right_to_left;
    const double main_limit = horizontal ? inner.width : inner.height;
    const double cross_limit = horizontal ? inner.height : inner.width;
    const double main_spacing = horizontal ? item_spacing_.width
                                           : item_spacing_.height;
    const double cross_spacing = horizontal ? item_spacing_.height
                                            : item_spacing_.width;

    // The children, held for the pass: a measure callback may change the
    // tree, so each is revalidated after it is measured.
    const std::span<const Control::Ptr> all = children();
    workspace.retained.assign(all.begin(), all.end());
    workspace.items.clear();
    workspace.items.reserve(workspace.retained.size());
    workspace.lines.clear();
    FlowLine line{};
    for (const Control::Ptr& child : workspace.retained) {
        if (!child || !is_current_layout_child(child) || !(*child).visible.read()) continue;
        const Size desired = preferred_child_size(child, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
        const FlowItem item{child, desired, (*child).margin.read(), flex_grow(*child), flow_break(*child)};
        const double item_main = horizontal
            ? horizontal_extent(item.margin) + item.desired.width
            : vertical_extent(item.margin) + item.desired.height;
        const double item_cross = horizontal
            ? vertical_extent(item.margin) + item.desired.height
            : horizontal_extent(item.margin) + item.desired.width;
        const double preceding_spacing = line.count == 0U ? 0.0 : main_spacing;
        if (line.count != 0U && wrap_contents_ &&
            line.main + preceding_spacing + item_main > main_limit) {
            workspace.lines.push_back(line);
            line = FlowLine{workspace.items.size(), 0U, 0.0, 0.0};
        }
        if (line.count != 0U) line.main += main_spacing;
        line.main += item_main;
        line.cross = std::max(line.cross, item_cross);
        workspace.items.push_back(item);
        ++line.count;
        if (item.break_after) {
            workspace.lines.push_back(line);
            line = FlowLine{workspace.items.size(), 0U, 0.0, 0.0};
        }
    }
    if (line.count != 0U) workspace.lines.push_back(line);
    // The items are held by the workspace now; the snapshot's references
    // are not needed past this point.
    workspace.retained.clear();

    double cross_origin = 0.0;
    double content_main = 0.0;
    for (std::size_t line_index = 0U; line_index < workspace.lines.size(); ++line_index) {
        const FlowLine& current = workspace.lines[line_index];
        const std::span<const FlowItem> line_items =
            std::span<const FlowItem>(workspace.items).subspan(current.first, current.count);
        const double available_main = std::max(0.0, main_limit - current.main);
        double total_grow = 0.0;
        for (const FlowItem& item : line_items) total_grow += item.grow;
        const double free_main = assign && total_grow > 0.0
            ? 0.0 : available_main;
        const MainPlacement main_placement =
            place_along(main_alignment_, free_main, line_items.size());
        double main_origin = main_placement.origin;
        const double distributed_spacing = main_placement.spacing;
        const double line_cross = assign && workspace.lines.size() == 1U
            ? cross_limit : current.cross;
        for (std::size_t item_index = 0U; item_index < line_items.size();
             ++item_index) {
            const FlowItem& item = line_items[item_index];
            if (item_index != 0U) {
                main_origin += main_spacing + distributed_spacing;
            }
            const double item_cross = horizontal
                ? vertical_extent(item.margin) + item.desired.height
                : horizontal_extent(item.margin) + item.desired.width;
            const double desired_cross = horizontal
                ? item.desired.height : item.desired.width;
            const double margin_cross = horizontal
                ? vertical_extent(item.margin) : horizontal_extent(item.margin);
            const double assigned_main = (horizontal
                ? item.desired.width : item.desired.height) +
                (assign && total_grow > 0.0
                    ? available_main * item.grow / total_grow : 0.0);
            const CrossPlacement cross_placement = place_across(
                cross_alignment_, line_cross, item_cross, desired_cross, margin_cross);
            const double cross_item_origin = cross_placement.origin;
            const double assigned_cross = cross_placement.extent;
            Rect slot;
            if (horizontal) {
                slot.width = assigned_main;
                slot.height = assigned_cross;
                slot.y = inset.top + cross_origin + cross_item_origin +
                         item.margin.top;
                if (flow_direction_ == FlowDirection::left_to_right) {
                    slot.x = inset.left + main_origin + item.margin.left;
                } else {
                    slot.x = inset.left + inner.width - main_origin -
                             item.margin.right - assigned_main;
                }
                main_origin += horizontal_extent(item.margin) + assigned_main;
            } else {
                slot.width = assigned_cross;
                slot.height = assigned_main;
                slot.x = inset.left + cross_origin + cross_item_origin +
                         item.margin.left;
                if (flow_direction_ == FlowDirection::top_down) {
                    slot.y = inset.top + main_origin + item.margin.top;
                } else {
                    slot.y = inset.top + inner.height - main_origin -
                             item.margin.bottom - assigned_main;
                }
                main_origin += vertical_extent(item.margin) + assigned_main;
            }
            if (assign && is_current_layout_child(item.control)) {
                // Right to left mirrors the scrolled slot about the
                // viewport, once, at output.
                const Point offset = scroll_position();
                slot.x -= offset.x;
                slot.y -= offset.y;
                if (right_to_left) slot.x = available.width - slot.x - slot.width;
                set_child_layout(item.control, slot);
            }
        }
        content_main = std::max(content_main, current.main);
        cross_origin += current.cross;
        if (line_index + 1U < workspace.lines.size()) cross_origin += cross_spacing;
    }
    // Releases the pass's references to the children; the capacity stays.
    workspace.items.clear();
    const Size content = horizontal ? Size{content_main, cross_origin}
                                    : Size{cross_origin, content_main};
    return {content.width + horizontal_extent(inset),
            content.height + vertical_extent(inset)};
}

Size FlowLayoutPanelCompat::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    if (auto_size.read()) {
        Size desired = layout_children(available, false);
        if (auto_size_mode.read() == AutoSizeMode::grow_only) {
            const Rect requested = requested_bounds();
            desired.width = std::max(desired.width, requested.width);
            desired.height = std::max(desired.height, requested.height);
        }
        Size result{std::min(available.width, desired.width),
                    std::min(available.height, desired.height)};
        const Size minimum = minimum_size.read();
        const Size maximum = maximum_size.read();
        result.width = std::max(result.width, minimum.width);
        result.height = std::max(result.height, minimum.height);
        if (maximum.width > 0.0) {
            result.width = std::min(result.width, maximum.width);
        }
        if (maximum.height > 0.0) {
            result.height = std::min(result.height, maximum.height);
        }
        return result;
    }
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void FlowLayoutPanelCompat::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Size client{final_bounds.width, final_bounds.height};
    Size available = client;
    // A vertical bar can cause another wrapped row. Re-measure against the
    // viewport until the two scrollbar decisions settle (at most three passes).
    for (int pass = 0; pass < 3; ++pass) {
        Size content = layout_children(available, false);
        if (!is_alive()) return;
        const Size extra = auto_scroll_margin();
        content.width += extra.width;
        content.height += extra.height;
        arrange_scroll_viewport(client, content);
        const Rect viewport = scroll_snapshot().viewport_rectangle;
        const Size next{viewport.width, viewport.height};
        if (next == available) break;
        available = next;
    }
    static_cast<void>(layout_children(available, true));
}

SemanticDescriptor FlowLayoutPanelCompat::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr ControlAnatomy flow_layout_panel_compat_anatomy{
    .control_id = "flow_layout_panel_compat",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& FlowLayoutPanelCompat::anatomy() const noexcept {
    return flow_layout_panel_compat_anatomy;
}

} // namespace gui_forms
