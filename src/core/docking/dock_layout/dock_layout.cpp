#include "gui_forms/docking/dock_layout/dock_layout.hpp"

#include "gui_forms/docking/dock_split_solver/dock_split_solver.hpp"
#include "dock_layout_internal.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace gui_forms {

using detail::DockNodePlace;
using detail::DockRegion;
using detail::DockRules;

DockLayout::DockLayout() {
    DockNode area{};
    area.id = DockNodeId{state_.next_id};
    area.kind = DockNodeKind::document_area;
    ++state_.next_id;
    state_.main_root = area.id;
    state_.nodes.push_back(std::move(area));
}

bool DockLayout::register_panel(const DockPanelDescriptor& descriptor) {
    if (descriptor.id.value.empty()) return false;
    if (find_descriptor(descriptor.id) != nullptr) return false;
    descriptors_.push_back(descriptor);
    return true;
}

bool DockLayout::update_panel(const DockPanelDescriptor& descriptor) {
    for (DockPanelDescriptor& existing : descriptors_) {
        if (!(existing.id == descriptor.id)) continue;
        existing = descriptor;
        return true;
    }
    return false;
}

const DockPanelDescriptor* DockLayout::find_descriptor(const DockPanelId& panel) const noexcept {
    const DockPanelDescriptor* descriptor = detail::find_dock_descriptor(descriptors_, panel);
    return descriptor;
}

void DockLayout::set_group(const DockGroupDescriptor& group) {
    for (DockGroupDescriptor& existing : groups_) {
        if (existing.id == group.id) {
            existing = group;
            return;
        }
    }
    groups_.push_back(group);
}

void DockLayout::set_main_site_restrictions(const DockSiteRestrictions& restrictions) {
    main_site_restrictions_ = restrictions;
}

void DockLayout::set_floating_site_restrictions(const DockSiteRestrictions& restrictions) {
    floating_site_restrictions_ = restrictions;
}

void DockLayout::set_user_docking_locked(bool locked) noexcept { user_docking_locked_ = locked; }

void DockLayout::set_main_window_frame(const DockWindowFrame& frame) { state_.main_window = frame; }

void DockLayout::set_application_version(std::uint32_t version) noexcept {
    state_.application_version = version;
}

// The first panel `after` puts on a side or edge its descriptor does not
// allow, where the current state had it elsewhere; none when there is none.
std::optional<DockPanelId> DockLayout::side_refusal(const DockLayoutState& after) const {
    for (const DockNode& node : after.nodes) {
        if (node.kind != DockNodeKind::tool_stack || node.panels.empty()) continue;
        const DockNodePlace place = detail::dock_place_of(after, node.id);
        const bool sided = place.region == DockRegion::main_tools || place.region == DockRegion::auto_hidden;
        if (!sided || !place.side) continue;
        for (const DockPanelId& panel : node.panels) {
            const DockPanelDescriptor* const descriptor = find_descriptor(panel);
            if (descriptor == nullptr || (*descriptor).allowed.allows_side(*place.side)) continue;
            const DockPanelLocation before = detail::locate_dock_panel(state_, panel);
            if (!(before.stack == DockNodeId{})) {
                const DockNodePlace was = detail::dock_place_of(state_, before.stack);
                if (was.region == place.region && was.side == place.side) continue;
            }
            return panel;
        }
    }
    return std::nullopt;
}

DockTransactionResult DockLayout::run(const DockTransaction& transaction, DockLayoutState& scratch) const {
    const DockRules rules{.descriptors = descriptors_,
                          .groups = groups_,
                          .main_site = main_site_restrictions_,
                          .floating_site = floating_site_restrictions_,
                          .user_docking_locked = user_docking_locked_};
    DockTransactionResult result{};
    result.generation = generation_;
    for (std::size_t index = 0; index < transaction.edits.size(); ++index) {
        DockRefusal refusal = detail::apply_dock_edit(scratch, transaction.edits[index], rules,
                                                      transaction.origin);
        if (refusal.reason != DockRefusalReason::none) {
            refusal.edit_index = index;
            result.refusal = std::move(refusal);
            return result;
        }
    }
    // The side restrictions (DockAllowedPlacements::left to bottom) are
    // known only once the edits placed their stacks: a panel the
    // transaction moved onto a side or edge it does not allow refuses the
    // transaction. A panel left where it stood is not refused, so a
    // descriptor updated after placement restricts later moves only.
    const std::optional<DockPanelId> misplaced = side_refusal(scratch);
    if (misplaced) {
        result.refusal.reason = DockRefusalReason::placement_not_allowed;
        result.refusal.layer = DockRefusalLayer::panel;
        result.refusal.panel = *misplaced;
        result.refusal.edit_index = transaction.edits.size();
        return result;
    }
    const DockInvariantResult invariants = detail::check_dock_invariants(scratch);
    if (!invariants) {
        result.refusal.reason = DockRefusalReason::invariant_violated;
        result.refusal.layer = DockRefusalLayer::model;
        result.refusal.edit_index = transaction.edits.size();
        return result;
    }
    result.applied = true;
    return result;
}

void DockLayout::commit(DockLayoutState&& replacement, DockEditOrigin origin, std::size_t edit_count,
                        bool restored, bool frames_only) {
    state_ = std::move(replacement);
    ++generation_;
    DockLayoutChange change{};
    change.generation = generation_;
    change.origin = origin;
    change.edit_count = edit_count;
    change.restored = restored;
    change.frames_only = frames_only;
    // Emitted after the commit: a handler sees the new layout and may apply
    // a further transaction, which publishes its own change.
    changed_.emit(change);
}

DockTransactionResult DockLayout::apply(const DockTransaction& transaction) {
    if (transaction.edits.empty()) {
        DockTransactionResult empty{};
        empty.applied = true;
        empty.generation = generation_;
        return empty;
    }
    DockLayoutState scratch = state_;
    DockTransactionResult result = run(transaction, scratch);
    if (!result.applied) return result;
    // Unknown fields of elements the transaction removed go with them.
    detail::prune_unknown_fields(scratch, result.dropped_unknown_fields);
    bool frames_only = true;
    for (const DockEdit& edit : transaction.edits) {
        if (edit.kind != DockEditKind::set_floating_frame) frames_only = false;
    }
    commit(std::move(scratch), transaction.origin, transaction.edits.size(), false, frames_only);
    result.generation = generation_;
    return result;
}

DockTransactionResult DockLayout::check(const DockTransaction& transaction) const {
    DockLayoutState scratch = state_;
    DockTransactionResult result = run(transaction, scratch);
    return result;
}

const DockLayoutState& DockLayout::state() const noexcept { return state_; }

std::uint64_t DockLayout::generation() const noexcept { return generation_; }

const DockNode* DockLayout::find_node(DockNodeId node) const noexcept {
    const DockNode* found = detail::find_dock_node(state_, node);
    return found;
}

const DockFloatingSite* DockLayout::find_floating_site(DockSiteId site) const noexcept {
    const DockFloatingSite* found = detail::find_dock_site(state_, site);
    return found;
}

DockPanelLocation DockLayout::locate(const DockPanelId& panel) const {
    const DockPanelLocation location = detail::locate_dock_panel(state_, panel);
    return location;
}

DockNodeId DockLayout::document_area() const noexcept {
    for (const DockNode& node : state_.nodes) {
        if (node.kind == DockNodeKind::document_area) return node.id;
    }
    return DockNodeId{};
}

std::optional<DockNodeId> DockLayout::pin_owner(const DockPanelId& panel) const {
    const DockPanelLocation location = locate(panel);
    if (location.stack == DockNodeId{}) return std::nullopt;
    const DockNodePlace place = detail::dock_place_of(state_, location.stack);
    const bool pinned_out = place.region == DockRegion::auto_hidden;
    const DockNode& stack = *find_node(location.stack);
    const bool docked_tool = place.region == DockRegion::main_tools && stack.kind == DockNodeKind::tool_stack;
    if (!pinned_out && !docked_tool) return std::nullopt;
    const std::optional<DockNodeId> owner = location.stack;
    return owner;
}

void DockLayout::collect_pinnable_items(std::vector<DockNodeId>& out) const {
    out.clear();
    std::vector<DockNodeId> order{};
    detail::collect_dock_subtree(state_, state_.main_root, order);
    for (const DockNodeId id : order) {
        const DockNode& node = *find_node(id);
        if (node.kind != DockNodeKind::tool_stack) continue;
        out.push_back(id);
    }
    for (const DockAutoHideEntry& entry : state_.auto_hidden) out.push_back(entry.stack);
}

void DockLayout::collect_visible_panels(std::vector<DockVisiblePanel>& out) const {
    out.clear();
    std::vector<DockNodeId> order{};
    std::vector<DockPlacementKind> placements{};
    detail::collect_dock_subtree(state_, state_.main_root, order);
    placements.resize(order.size(), DockPlacementKind::docked);
    for (const DockFloatingSite& site : state_.floating_sites) {
        detail::collect_dock_subtree(state_, site.root, order);
        placements.resize(order.size(), DockPlacementKind::floating);
    }
    for (const DockAutoHideEntry& entry : state_.auto_hidden) {
        order.push_back(entry.stack);
        placements.push_back(DockPlacementKind::auto_hidden);
    }
    for (std::size_t position = 0; position < order.size(); ++position) {
        const DockNode& node = *find_node(order[position]);
        if (!detail::is_dock_stack(node.kind)) continue;
        for (std::size_t index = 0; index < node.panels.size(); ++index) {
            DockVisiblePanel visible{};
            visible.panel = node.panels[index];
            visible.stack = node.id;
            visible.placement = placements[position];
            visible.selected = index == node.selected;
            out.push_back(std::move(visible));
        }
    }
}

DockInvariantResult DockLayout::check_invariants() const {
    DockInvariantResult result = detail::check_dock_invariants(state_);
    return result;
}

Size DockLayout::minimum_size(DockNodeId node_id, double seam) const {
    Size minimum{};
    const DockNode* node = find_node(node_id);
    if (node == nullptr) return minimum;
    if (detail::is_dock_stack((*node).kind)) {
        for (const DockPanelId& panel : (*node).panels) {
            const DockPanelDescriptor* descriptor = find_descriptor(panel);
            if (descriptor == nullptr) continue;
            minimum.width = std::max(minimum.width, (*descriptor).minimum_size.width);
            minimum.height = std::max(minimum.height, (*descriptor).minimum_size.height);
        }
        return minimum;
    }
    if ((*node).kind == DockNodeKind::document_area) {
        if ((*node).first == DockNodeId{}) return minimum;
        const Size content = minimum_size((*node).first, seam);
        return content;
    }
    const Size first = minimum_size((*node).first, seam);
    const Size second = minimum_size((*node).second, seam);
    if ((*node).axis == DockAxis::horizontal) {
        minimum.width = first.width + seam + second.width;
        minimum.height = std::max(first.height, second.height);
    } else {
        minimum.width = std::max(first.width, second.width);
        minimum.height = first.height + seam + second.height;
    }
    return minimum;
}

void DockLayout::arrange(DockNodeId root, Rect bounds, double seam, std::vector<DockArrangedNode>& out) const {
    out.clear();
    // Each entry is a node still to lay out, with its rectangle; the list
    // is processed in tree order (a node, then its first subtree).
    std::vector<DockArrangedNode> pending{};
    DockArrangedNode start{};
    start.node = root;
    start.bounds = bounds;
    pending.push_back(start);
    while (!pending.empty()) {
        DockArrangedNode current = pending.back();
        pending.pop_back();
        const DockNode* node = find_node(current.node);
        if (node == nullptr) continue;
        if ((*node).kind == DockNodeKind::document_area) {
            out.push_back(current);
            if (!((*node).first == DockNodeId{})) {
                DockArrangedNode content{};
                content.node = (*node).first;
                content.bounds = current.bounds;
                pending.push_back(content);
            }
            continue;
        }
        if ((*node).kind != DockNodeKind::split) {
            out.push_back(current);
            continue;
        }
        // A horizontal split sets its children side by side, sharing the
        // width.
        const bool side_by_side = (*node).axis == DockAxis::horizontal;
        const Size first_minimum = minimum_size((*node).first, seam);
        const Size second_minimum = minimum_size((*node).second, seam);
        const double extent = side_by_side ? current.bounds.width : current.bounds.height;
        const double span = std::max(extent - seam, 0.0);
        const DockSplitSizes sizes = side_by_side
            ? solve_dock_split(span, (*node).ratio, first_minimum.width, second_minimum.width)
            : solve_dock_split(span, (*node).ratio, first_minimum.height, second_minimum.height);
        current.minimums_met = sizes.minimums_met;
        out.push_back(current);
        DockArrangedNode first{};
        first.node = (*node).first;
        first.bounds = current.bounds;
        DockArrangedNode second{};
        second.node = (*node).second;
        second.bounds = current.bounds;
        if (side_by_side) {
            first.bounds.width = sizes.first;
            second.bounds.x = current.bounds.x + sizes.first + seam;
            second.bounds.width = sizes.second;
        } else {
            first.bounds.height = sizes.first;
            second.bounds.y = current.bounds.y + sizes.first + seam;
            second.bounds.height = sizes.second;
        }
        // Pushed second first so the first subtree is laid out first.
        pending.push_back(second);
        pending.push_back(first);
    }
}

Event<const DockLayoutChange&>& DockLayout::changed() noexcept { return changed_; }

} // namespace gui_forms
