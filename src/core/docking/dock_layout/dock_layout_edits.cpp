#include "dock_layout_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gui_forms::detail {

namespace {

[[nodiscard]] bool valid_ratio(double ratio) noexcept {
    const bool valid = std::isfinite(ratio) && ratio > 0.0 && ratio < 1.0;
    return valid;
}

[[nodiscard]] bool valid_frame(Rect frame) noexcept {
    const bool valid = frame.finite() && frame.width > 0.0 && frame.height > 0.0;
    return valid;
}

[[nodiscard]] DockAxis axis_for(DockSide side) noexcept {
    const bool across = side == DockSide::left || side == DockSide::right;
    const DockAxis axis = across ? DockAxis::horizontal : DockAxis::vertical;
    return axis;
}

[[nodiscard]] bool side_comes_first(DockSide side) noexcept {
    const bool first = side == DockSide::left || side == DockSide::top;
    return first;
}

[[nodiscard]] bool group_lists(const std::vector<DockGroupDescriptor>& groups, const DockGroupId& owner,
                               const DockGroupId& other) noexcept {
    for (const DockGroupDescriptor& group : groups) {
        if (!(group.id == owner)) continue;
        for (const DockGroupId& admitted : group.admits) {
            if (admitted == other) return true;
        }
    }
    return false;
}

[[nodiscard]] bool groups_admit(const std::vector<DockGroupDescriptor>& groups, const DockGroupId& left,
                                const DockGroupId& right) noexcept {
    if (left == right) return true;
    const bool admitted = group_lists(groups, left, right) || group_lists(groups, right, left);
    return admitted;
}

// Where a stack stands beside its sibling: the neighbour it would return
// to, which side of it, and its share of their split.
struct StackSurroundings final {
    DockNodeId neighbour{};
    DockSide side{DockSide::left};
    double ratio{0.25};
};

[[nodiscard]] StackSurroundings surroundings_of(const DockLayoutState& state, DockNodeId stack) noexcept {
    StackSurroundings surroundings{};
    const DockNode* node = find_dock_node(state, stack);
    if (node == nullptr) return surroundings;
    const DockNode* parent = find_dock_node(state, (*node).parent);
    if (parent == nullptr || (*parent).kind != DockNodeKind::split) return surroundings;
    const bool first = (*parent).first == stack;
    const bool across = (*parent).axis == DockAxis::horizontal;
    surroundings.neighbour = first ? (*parent).second : (*parent).first;
    if (first) {
        surroundings.side = across ? DockSide::left : DockSide::top;
        surroundings.ratio = (*parent).ratio;
    } else {
        surroundings.side = across ? DockSide::right : DockSide::bottom;
        surroundings.ratio = 1.0 - (*parent).ratio;
    }
    return surroundings;
}

// Applies one edit to a scratch state. Node pointers are looked up again
// after every change to the node table, never held across one.
class DockEditor final {
public:
    DockEditor(DockLayoutState& state, const DockRules& rules, DockEditOrigin origin)
        : state_(state), rules_(rules), origin_(origin) {}

    DockRefusal apply(const DockEdit& edit) {
        const bool moves_panels = edit.kind != DockEditKind::select &&
                                  edit.kind != DockEditKind::set_ratio &&
                                  edit.kind != DockEditKind::set_floating_frame;
        if (moves_panels && origin_ == DockEditOrigin::user && rules_.user_docking_locked) {
            return refuse(DockRefusalReason::user_docking_locked, DockRefusalLayer::site);
        }
        switch (edit.kind) {
        case DockEditKind::dock_into: return dock_into(edit.subject, edit.target, edit.index);
        case DockEditKind::dock_beside:
            return dock_beside(edit.subject, edit.target, edit.side, edit.ratio, false);
        case DockEditKind::dock_at_edge:
            return dock_beside(edit.subject, DockNodeId{}, edit.side, edit.ratio, true);
        case DockEditKind::float_subject: return float_subject(edit.subject, edit.frame, edit.monitor_id);
        case DockEditKind::auto_hide: return auto_hide(edit.subject, edit.side, edit.extent);
        case DockEditKind::unpin: return unpin(edit.subject.stack);
        case DockEditKind::hide: return hide(edit.subject.panel);
        case DockEditKind::show: return show(edit.subject.panel);
        case DockEditKind::close: return close(edit.subject.panel);
        case DockEditKind::move: return move(edit.subject.panel, edit.index);
        case DockEditKind::select: return select(edit.subject.panel);
        case DockEditKind::set_ratio: return set_ratio(edit.target, edit.ratio);
        case DockEditKind::set_floating_frame:
            return set_floating_frame(edit.site, edit.frame, edit.monitor_id);
        }
        return refuse(DockRefusalReason::invariant_violated, DockRefusalLayer::model);
    }

    // Removes a panel from its stack, remembering its place; prunes.
    void detach_panel(const DockPanelId& panel) {
        const DockPanelLocation location = locate_dock_panel(state_, panel);
        if (location.stack == DockNodeId{}) return;
        remember(location.stack);
        DockNode& stack = *find_dock_node(state_, location.stack);
        stack.panels.erase(stack.panels.begin() + static_cast<std::ptrdiff_t>(location.index));
        // The selection stays on the same panel; when that panel leaves, the
        // next one is selected, else the previous (DK decision 37, strips).
        if (location.index < stack.selected) {
            --stack.selected;
        } else if (stack.selected >= stack.panels.size() && !stack.panels.empty()) {
            stack.selected = static_cast<std::uint32_t>(stack.panels.size() - 1U);
        }
        if (stack.panels.empty()) {
            unlink_stack(location.stack);
            erase_node(location.stack);
        }
    }

private:
    [[nodiscard]] DockRefusal refuse(DockRefusalReason reason, DockRefusalLayer layer,
                                     const DockPanelId& panel = {}) const {
        DockRefusal refusal{};
        refusal.reason = reason;
        refusal.layer = layer;
        refusal.panel = panel;
        return refusal;
    }

    [[nodiscard]] static DockRefusal accepted() {
        const DockRefusal none{};
        return none;
    }

    [[nodiscard]] static bool is_refusal(const DockRefusal& refusal) noexcept {
        const bool refused = refusal.reason != DockRefusalReason::none;
        return refused;
    }

    // The table ---------------------------------------------------------

    // Assigns the next node or site ID to id. Once next_id has reached
    // dock_identity_limit the edit is refused instead, and id and next_id
    // are left as they were; the scratch state is then discarded.
    DockRefusal new_node_id(DockNodeId& id) {
        if (state_.next_id >= dock_identity_limit) {
            return refuse(DockRefusalReason::identities_exhausted, DockRefusalLayer::model);
        }
        id = DockNodeId{state_.next_id};
        ++state_.next_id;
        return accepted();
    }

    // Inserts a new stack and assigns its ID to id, or refuses as
    // new_node_id does, inserting nothing.
    DockRefusal add_stack(DockNodeKind kind, std::vector<DockPanelId> panels, std::uint32_t selected,
                          DockNodeId& id) {
        DockNode node{};
        const DockRefusal assigned = new_node_id(node.id);
        if (is_refusal(assigned)) return assigned;
        node.kind = kind;
        node.panels = std::move(panels);
        node.selected = selected;
        id = node.id;
        insert_dock_node(state_, std::move(node));
        return accepted();
    }

    void erase_node(DockNodeId id) {
        for (std::size_t index = 0; index < state_.nodes.size(); ++index) {
            if (state_.nodes[index].id == id) {
                state_.nodes.erase(state_.nodes.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
        }
    }

    // Makes replacement stand where old stood: in old's parent, or as the
    // root of old's site.
    void replace_child(DockNodeId parent, DockNodeId old, DockNodeId replacement) {
        if (DockNode* replacement_node = find_dock_node(state_, replacement)) {
            (*replacement_node).parent = parent;
        }
        if (!(parent == DockNodeId{})) {
            DockNode& node = *find_dock_node(state_, parent);
            if (node.first == old) {
                node.first = replacement;
            } else if (node.second == old) {
                node.second = replacement;
            }
            return;
        }
        if (state_.main_root == old) {
            state_.main_root = replacement;
            return;
        }
        for (DockFloatingSite& site : state_.floating_sites) {
            if (site.root == old) {
                site.root = replacement;
                return;
            }
        }
    }

    // Writes, for every panel of a stack, the memory of the place the stack
    // stands in now (DK decision 22).
    void remember(DockNodeId stack_id) {
        const DockNodePlace place = dock_place_of(state_, stack_id);
        const StackSurroundings surroundings = surroundings_of(state_, stack_id);
        const DockNode& stack = *find_dock_node(state_, stack_id);
        const DockAutoHideEntry* entry = find_dock_auto_hidden(state_, stack_id);
        const DockFloatingSite* site = find_dock_site(state_, place.site);
        // The place decides the memory once; every panel of the stack gets it.
        if (place.region == DockRegion::auto_hidden && entry != nullptr) {
            const DockAutoHideMemory memory{.stack = stack_id, .edge = (*entry).edge, .extent = (*entry).extent};
            for (const DockPanelId& panel : stack.panels) {
                DockPanelRecord* record = find_dock_record(state_, panel);
                if (record == nullptr) continue;
                (*record).auto_hidden = memory;
                (*record).last_placement = DockPlacementKind::auto_hidden;
            }
            return;
        }
        if (place.region == DockRegion::floating && site != nullptr) {
            DockFloatingMemory memory{};
            memory.site = (*site).id;
            memory.frame = (*site).frame;
            memory.monitor_id = (*site).monitor_id;
            for (const DockPanelId& panel : stack.panels) {
                DockPanelRecord* record = find_dock_record(state_, panel);
                if (record == nullptr) continue;
                (*record).floating = memory;
                (*record).last_placement = DockPlacementKind::floating;
            }
            return;
        }
        if (place.region != DockRegion::main_tools && place.region != DockRegion::document_area) return;
        // A docked panel also remembers its index in the stack.
        for (std::size_t index = 0; index < stack.panels.size(); ++index) {
            DockPanelRecord* record = find_dock_record(state_, stack.panels[index]);
            if (record == nullptr) continue;
            const DockDockedMemory memory{.stack = stack_id,
                                          .index = static_cast<std::uint32_t>(index),
                                          .neighbour = surroundings.neighbour,
                                          .side = surroundings.side,
                                          .ratio = surroundings.ratio};
            (*record).docked = memory;
            (*record).last_placement = DockPlacementKind::docked;
        }
    }

    // Takes a stack out of where it stands, keeping the node, and prunes:
    // a split left with one child is replaced by it, an emptied floating
    // site is removed, an auto-hide entry is removed.
    void unlink_stack(DockNodeId stack_id) {
        for (std::size_t index = 0; index < state_.auto_hidden.size(); ++index) {
            if (state_.auto_hidden[index].stack == stack_id) {
                state_.auto_hidden.erase(state_.auto_hidden.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
        }
        DockNode& stack = *find_dock_node(state_, stack_id);
        const DockNodeId parent_id = stack.parent;
        stack.parent = DockNodeId{};
        if (parent_id == DockNodeId{}) {
            for (std::size_t index = 0; index < state_.floating_sites.size(); ++index) {
                if (state_.floating_sites[index].root == stack_id) {
                    state_.floating_sites.erase(state_.floating_sites.begin() +
                                                static_cast<std::ptrdiff_t>(index));
                    return;
                }
            }
            return;
        }
        DockNode& parent = *find_dock_node(state_, parent_id);
        if (parent.kind == DockNodeKind::document_area) {
            parent.first = DockNodeId{};
            return;
        }
        const DockNodeId sibling = parent.first == stack_id ? parent.second : parent.first;
        const DockNodeId grandparent = parent.parent;
        replace_child(grandparent, parent_id, sibling);
        erase_node(parent_id);
    }

    // Puts item beside target on side, in a new split; item takes ratio of
    // the split's span.
    // Refuses, changing nothing, when no ID is left for the split.
    DockRefusal attach_beside(DockNodeId target_id, DockSide side, double ratio, DockNodeId item) {
        const bool item_first = side_comes_first(side);
        const double split_ratio = item_first ? ratio : 1.0 - ratio;
        DockNodeId split_identity{};
        const DockRefusal assigned = new_node_id(split_identity);
        if (is_refusal(assigned)) return assigned;
        attach_in_split(target_id, side, item, split_identity, split_ratio);
        return accepted();
    }

    // Whether no node or floating site uses an identity that was assigned
    // before (node and site IDs share one count).
    [[nodiscard]] bool identity_free(DockNodeId id) const noexcept {
        if (id == DockNodeId{} || id.value >= state_.next_id) return false;
        if (find_dock_node(state_, id) != nullptr) return false;
        const DockSiteId as_site{id.value};
        const bool free = find_dock_site(state_, as_site) == nullptr;
        return free;
    }

    // Puts item beside target on side, in a split with the given identity
    // whose first child takes split_ratio of its span.
    void attach_in_split(DockNodeId target_id, DockSide side, DockNodeId item, DockNodeId split_identity,
                         double split_ratio) {
        DockNode split{};
        split.id = split_identity;
        split.kind = DockNodeKind::split;
        split.axis = axis_for(side);
        const bool item_first = side_comes_first(side);
        split.first = item_first ? item : target_id;
        split.second = item_first ? target_id : item;
        split.ratio = split_ratio;
        const DockNodeId split_id = split.id;
        const DockNodeId parent = (*find_dock_node(state_, target_id)).parent;
        insert_dock_node(state_, std::move(split));
        replace_child(parent, target_id, split_id);
        (*find_dock_node(state_, target_id)).parent = split_id;
        (*find_dock_node(state_, item)).parent = split_id;
    }

    // Restrictions ------------------------------------------------------

    [[nodiscard]] const DockSiteRestrictions& site_rules(DockRegion region) const noexcept {
        if (region == DockRegion::floating) return rules_.floating_site;
        return rules_.main_site;
    }

    // Whether one panel may stand in a stack of this kind in this region:
    // the panel layer (kind and allowed placements) and the site layer.
    [[nodiscard]] DockRefusal admit_panel(const DockPanelId& panel_id, DockNodeKind stack_kind,
                                          const DockNodePlace& place) const {
        const DockRegion region = place.region;
        const DockPanelDescriptor* descriptor = find_dock_descriptor(rules_.descriptors, panel_id);
        if (descriptor == nullptr) return refuse(DockRefusalReason::unknown_panel, DockRefusalLayer::model, panel_id);
        const DockPanelDescriptor& panel = *descriptor;
        const bool is_tool = panel.kind == DockPanelKind::tool;
        if (stack_kind == DockNodeKind::tool_stack && !is_tool) {
            return refuse(DockRefusalReason::kind_mismatch, DockRefusalLayer::panel, panel_id);
        }
        if (stack_kind == DockNodeKind::document_stack && is_tool && !panel.dock_as_document) {
            return refuse(DockRefusalReason::kind_mismatch, DockRefusalLayer::panel, panel_id);
        }
        bool allowed = true;
        if (region == DockRegion::main_tools) allowed = panel.allowed.docked;
        if (region == DockRegion::document_area) allowed = panel.allowed.document_area;
        if (region == DockRegion::floating) allowed = panel.allowed.floating;
        if (region == DockRegion::auto_hidden) allowed = panel.allowed.auto_hidden;
        const bool sided = region == DockRegion::main_tools || region == DockRegion::auto_hidden;
        if (allowed && sided && place.side && !panel.allowed.allows_side(*place.side)) allowed = false;
        if (!allowed) return refuse(DockRefusalReason::placement_not_allowed, DockRefusalLayer::panel, panel_id);
        const DockSiteRestrictions& site = site_rules(region);
        const bool kind_accepted = is_tool ? site.accepts_tools : site.accepts_documents;
        if (!kind_accepted) return refuse(DockRefusalReason::site_refuses_kind, DockRefusalLayer::site, panel_id);
        if (!site.admitted_groups.empty()) {
            const std::vector<DockGroupId>::const_iterator found =
                std::find(site.admitted_groups.begin(), site.admitted_groups.end(), panel.group);
            if (found == site.admitted_groups.end()) {
                return refuse(DockRefusalReason::site_refuses_group, DockRefusalLayer::site, panel_id);
            }
        }
        return accepted();
    }

    // Whether a stack's panels may share it: each allows tabbing (panel
    // layer) and every pair's groups admit each other (group layer).
    [[nodiscard]] DockRefusal admit_members(DockNodeId stack_id) const {
        const DockNode& stack = *find_dock_node(state_, stack_id);
        if (stack.panels.size() < 2U) return accepted();
        for (std::size_t index = 0; index < stack.panels.size(); ++index) {
            const DockPanelDescriptor* panel = find_dock_descriptor(rules_.descriptors, stack.panels[index]);
            if (panel == nullptr) continue;
            if (!(*panel).allowed.tabbed) {
                return refuse(DockRefusalReason::tabbing_not_allowed, DockRefusalLayer::panel, (*panel).id);
            }
            for (std::size_t other = 0; other < index; ++other) {
                const DockPanelDescriptor* earlier =
                    find_dock_descriptor(rules_.descriptors, stack.panels[other]);
                if (earlier == nullptr) continue;
                if (!groups_admit(rules_.groups, (*panel).group, (*earlier).group)) {
                    return refuse(DockRefusalReason::group_not_admitted, DockRefusalLayer::group, (*panel).id);
                }
            }
        }
        return accepted();
    }

    [[nodiscard]] DockRefusal admit_stack(DockNodeId stack_id, const DockNodePlace& place) const {
        const DockNode& stack = *find_dock_node(state_, stack_id);
        for (const DockPanelId& panel : stack.panels) {
            const DockRefusal panel_refusal = admit_panel(panel, stack.kind, place);
            if (is_refusal(panel_refusal)) return panel_refusal;
        }
        const DockRefusal members = admit_members(stack_id);
        return members;
    }

    // Subjects ----------------------------------------------------------

    // Checks a subject exists and is movable before anything changes.
    [[nodiscard]] DockRefusal check_subject(const DockSubject& subject) const {
        if (subject.kind == DockSubjectKind::stack) {
            const DockNode* stack = find_dock_node(state_, subject.stack);
            if (stack == nullptr) return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
            if (!is_dock_stack((*stack).kind)) return refuse(DockRefusalReason::not_a_stack, DockRefusalLayer::model);
            return accepted();
        }
        if (find_dock_descriptor(rules_.descriptors, subject.panel) == nullptr) {
            return refuse(DockRefusalReason::unknown_panel, DockRefusalLayer::model, subject.panel);
        }
        return accepted();
    }

    // Ensures a record for a panel entering the layout.
    void ensure_record(const DockPanelId& panel_id) {
        if (find_dock_record(state_, panel_id) != nullptr) return;
        const DockPanelDescriptor& descriptor = *find_dock_descriptor(rules_.descriptors, panel_id);
        DockPanelRecord record{};
        record.id = panel_id;
        record.kind = descriptor.kind;
        insert_dock_record(state_, std::move(record));
    }

    // Takes a subject out of its place. For a panel the result is the
    // panel alone (to be put in a stack); for a stack, the unlinked stack.
    void take_subject(const DockSubject& subject) {
        if (subject.kind == DockSubjectKind::stack) {
            remember(subject.stack);
            unlink_stack(subject.stack);
            return;
        }
        detach_panel(subject.panel);
        ensure_record(subject.panel);
    }

    // The kind of stack a panel forms on its own in a region.
    [[nodiscard]] DockNodeKind stack_kind_for(const DockPanelId& panel_id, DockRegion region) const {
        if (region == DockRegion::document_area) return DockNodeKind::document_stack;
        if (region == DockRegion::floating) {
            const DockPanelDescriptor& descriptor = *find_dock_descriptor(rules_.descriptors, panel_id);
            const DockNodeKind kind = descriptor.kind == DockPanelKind::document
                                          ? DockNodeKind::document_stack
                                          : DockNodeKind::tool_stack;
            return kind;
        }
        return DockNodeKind::tool_stack;
    }

    // Makes the stack that will be placed: a new stack for a panel subject,
    // or the subject stack itself, checked for the region it is going to.
    DockRefusal stack_for_region(const DockSubject& subject, DockRegion region, DockNodeId& item) {
        if (subject.kind == DockSubjectKind::stack) {
            const DockNode& stack = *find_dock_node(state_, subject.stack);
            const bool tool = stack.kind == DockNodeKind::tool_stack;
            if (region == DockRegion::document_area && tool) {
                return refuse(DockRefusalReason::kind_mismatch, DockRefusalLayer::panel);
            }
            if ((region == DockRegion::main_tools || region == DockRegion::auto_hidden) && !tool) {
                return refuse(DockRefusalReason::kind_mismatch, DockRefusalLayer::panel);
            }
            item = subject.stack;
        } else {
            const DockNodeKind kind = stack_kind_for(subject.panel, region);
            std::vector<DockPanelId> panels{subject.panel};
            const DockRefusal added = add_stack(kind, std::move(panels), 0, item);
            if (is_refusal(added)) return added;
        }
        const DockNodePlace checked_place{.region = region, .site = {}, .side = std::nullopt};
        const DockRefusal admitted = admit_stack(item, checked_place);
        return admitted;
    }

    // Edits -------------------------------------------------------------

    // The first stack of a subtree in tree order, or none.
    [[nodiscard]] DockNodeId first_stack_in(DockNodeId root) const {
        std::vector<DockNodeId> order{};
        collect_dock_subtree(state_, root, order);
        for (const DockNodeId id : order) {
            if (is_dock_stack((*find_dock_node(state_, id)).kind)) return id;
        }
        return DockNodeId{};
    }

    DockRefusal dock_into(const DockSubject& subject, DockNodeId target_id, std::uint32_t index) {
        const DockRefusal subject_refusal = check_subject(subject);
        if (is_refusal(subject_refusal)) return subject_refusal;
        const DockNode* target = find_dock_node(state_, target_id);
        if (target == nullptr) return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
        if ((*target).kind == DockNodeKind::split) return refuse(DockRefusalReason::not_a_stack, DockRefusalLayer::model);
        if (subject.kind == DockSubjectKind::stack && subject.stack == target_id) {
            return refuse(DockRefusalReason::target_is_subject, DockRefusalLayer::model);
        }
        const DockNodePlace target_place = dock_place_of(state_, target_id);
        if (target_place.region == DockRegion::detached) {
            return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
        }

        // The panels that will join, in order, and the one to select.
        std::vector<DockPanelId> incoming{};
        DockPanelId selected_panel{};
        if (subject.kind == DockSubjectKind::stack) {
            const DockNode& stack = *find_dock_node(state_, subject.stack);
            incoming = stack.panels;
            selected_panel = stack.panels[stack.selected];
        } else {
            incoming.push_back(subject.panel);
            selected_panel = subject.panel;
        }
        take_subject(subject);
        if (subject.kind == DockSubjectKind::stack) erase_node(subject.stack);
        // The target is pruned when the subject was its only panel (a panel
        // docked into its own stack): that is docking into itself.
        if (find_dock_node(state_, target_id) == nullptr) {
            return refuse(DockRefusalReason::target_is_subject, DockRefusalLayer::model);
        }

        DockNodeId stack_id = target_id;
        if ((*find_dock_node(state_, target_id)).kind == DockNodeKind::document_area) {
            const DockNodeId content = (*find_dock_node(state_, target_id)).first;
            stack_id = content == DockNodeId{} ? DockNodeId{} : first_stack_in(content);
            if (stack_id == DockNodeId{}) {
                const DockRefusal added = add_stack(DockNodeKind::document_stack, {}, 0, stack_id);
                if (is_refusal(added)) return added;
                DockNode& area = *find_dock_node(state_, target_id);
                area.first = stack_id;
                (*find_dock_node(state_, stack_id)).parent = target_id;
            }
        }
        const DockNodePlace place = dock_place_of(state_, stack_id);
        const DockNodeKind stack_kind = (*find_dock_node(state_, stack_id)).kind;
        for (const DockPanelId& panel : incoming) {
            const DockRefusal admitted = admit_panel(panel, stack_kind, place);
            if (is_refusal(admitted)) return admitted;
        }
        DockNode& stack = *find_dock_node(state_, stack_id);
        const std::size_t at = std::min<std::size_t>(index, stack.panels.size());
        stack.panels.insert(stack.panels.begin() + static_cast<std::ptrdiff_t>(at), incoming.begin(),
                            incoming.end());
        const std::vector<DockPanelId>::iterator chosen =
            std::find(stack.panels.begin(), stack.panels.end(), selected_panel);
        stack.selected = static_cast<std::uint32_t>(chosen - stack.panels.begin());
        const DockRefusal members = admit_members(stack_id);
        return members;
    }

    DockRefusal dock_beside(const DockSubject& subject, DockNodeId target_id, DockSide side, double ratio,
                            bool at_main_edge) {
        const DockRefusal subject_refusal = check_subject(subject);
        if (is_refusal(subject_refusal)) return subject_refusal;
        if (!valid_ratio(ratio)) return refuse(DockRefusalReason::invalid_ratio, DockRefusalLayer::model);
        if (!at_main_edge) {
            if (find_dock_node(state_, target_id) == nullptr) {
                return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
            }
            const DockNodePlace target_place = dock_place_of(state_, target_id);
            const bool in_site = target_place.region == DockRegion::main_tools ||
                                 target_place.region == DockRegion::document_area ||
                                 target_place.region == DockRegion::floating;
            if (!in_site) return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
            if (subject.kind == DockSubjectKind::stack && subject.stack == target_id) {
                return refuse(DockRefusalReason::target_is_subject, DockRefusalLayer::model);
            }
        }
        take_subject(subject);
        if (at_main_edge) target_id = state_.main_root;
        // Taking the subject out prunes the target when the subject was all
        // it held (its only panel, or a split's side): beside itself.
        if (find_dock_node(state_, target_id) == nullptr) {
            return refuse(DockRefusalReason::target_is_subject, DockRefusalLayer::model);
        }
        const DockNodePlace place = dock_place_of(state_, target_id);
        DockNodeId item{};
        const DockRefusal admitted = stack_for_region(subject, place.region, item);
        if (is_refusal(admitted)) return admitted;
        const DockRefusal attached = attach_beside(target_id, side, ratio, item);
        return attached;
    }

    DockRefusal float_subject(const DockSubject& subject, Rect frame, const std::string& monitor_id) {
        const DockRefusal subject_refusal = check_subject(subject);
        if (is_refusal(subject_refusal)) return subject_refusal;
        if (!valid_frame(frame)) return refuse(DockRefusalReason::invalid_frame, DockRefusalLayer::model);
        take_subject(subject);
        if (state_.floating_sites.size() >= dock_maximum_floating_sites) {
            return refuse(DockRefusalReason::floating_site_limit, DockRefusalLayer::model);
        }
        DockNodeId item{};
        const DockRefusal admitted = stack_for_region(subject, DockRegion::floating, item);
        if (is_refusal(admitted)) return admitted;
        DockNodeId site_identity{};
        const DockRefusal assigned = new_node_id(site_identity);
        if (is_refusal(assigned)) return assigned;
        DockFloatingSite site{};
        site.id = DockSiteId{site_identity.value};
        site.root = item;
        site.frame = frame;
        site.monitor_id = monitor_id;
        state_.floating_sites.push_back(std::move(site));
        return accepted();
    }

    DockRefusal auto_hide(const DockSubject& subject, DockSide edge, double extent) {
        const DockRefusal subject_refusal = check_subject(subject);
        if (is_refusal(subject_refusal)) return subject_refusal;
        if (!(std::isfinite(extent) && extent > 0.0)) {
            return refuse(DockRefusalReason::invalid_extent, DockRefusalLayer::model);
        }
        DockNodeId stack_id = subject.stack;
        if (subject.kind == DockSubjectKind::panel) {
            const DockPanelLocation location = locate_dock_panel(state_, subject.panel);
            if (location.stack == DockNodeId{}) {
                return refuse(DockRefusalReason::not_placed, DockRefusalLayer::model, subject.panel);
            }
            stack_id = location.stack;
            // Only a root-level item carries a pin: a panel inside a group
            // is pinned with its group (DK decision 28).
            if ((*find_dock_node(state_, stack_id)).panels.size() > 1U) {
                return refuse(DockRefusalReason::not_root_level, DockRefusalLayer::model, subject.panel);
            }
        }
        const DockNodePlace place = dock_place_of(state_, stack_id);
        if (place.region != DockRegion::main_tools) {
            return refuse(DockRefusalReason::not_in_main_site, DockRefusalLayer::model);
        }
        const DockNodePlace checked_place{.region = DockRegion::auto_hidden, .site = {}, .side = edge};
        const DockRefusal admitted = admit_stack(stack_id, checked_place);
        if (is_refusal(admitted)) return admitted;
        const StackSurroundings surroundings = surroundings_of(state_, stack_id);
        DockAutoHideEntry entry{};
        entry.stack = stack_id;
        entry.edge = edge;
        entry.extent = extent;
        entry.neighbour = surroundings.neighbour;
        entry.side = surroundings.side;
        entry.ratio = surroundings.ratio;
        // The split unlink_stack removes, so that unpinning can recreate it.
        const DockNode* parent = find_dock_node(state_, (*find_dock_node(state_, stack_id)).parent);
        if (parent != nullptr && (*parent).kind == DockNodeKind::split) {
            entry.split = (*parent).id;
            entry.split_ratio = (*parent).ratio;
        }
        // The panels' remembered places are left as they are: a pin is
        // undone by unpinning, not by show (DockAutoHideEntry).
        unlink_stack(stack_id);
        state_.auto_hidden.push_back(entry);
        return accepted();
    }

    DockRefusal unpin(DockNodeId stack_id) {
        const DockAutoHideEntry* found = find_dock_auto_hidden(state_, stack_id);
        if (found == nullptr) return refuse(DockRefusalReason::not_auto_hidden, DockRefusalLayer::model);
        const DockAutoHideEntry entry = *found;
        const DockNodePlace checked_place{.region = DockRegion::main_tools, .site = {}, .side = std::nullopt};
        const DockRefusal admitted = admit_stack(stack_id, checked_place);
        if (is_refusal(admitted)) return admitted;
        unlink_stack(stack_id);
        DockNodeId target = entry.neighbour;
        DockSide side = entry.side;
        const bool neighbour_usable = find_dock_node(state_, target) != nullptr &&
                                      dock_place_of(state_, target).region == DockRegion::main_tools;
        if (!neighbour_usable) {
            target = state_.main_root;
            side = entry.edge;
        }
        // Beside the same neighbour the old split comes back with its
        // identity and exact ratio, so the layout is as it was before the pin.
        if (neighbour_usable && identity_free(entry.split) && valid_ratio(entry.split_ratio)) {
            attach_in_split(target, side, stack_id, entry.split, entry.split_ratio);
            return accepted();
        }
        const double ratio = valid_ratio(entry.ratio) ? entry.ratio : 0.25;
        const DockRefusal attached = attach_beside(target, side, ratio, stack_id);
        return attached;
    }

    DockRefusal hide(const DockPanelId& panel_id) {
        const DockPanelDescriptor* descriptor = find_dock_descriptor(rules_.descriptors, panel_id);
        if (descriptor == nullptr) return refuse(DockRefusalReason::unknown_panel, DockRefusalLayer::model, panel_id);
        const DockPanelLocation location = locate_dock_panel(state_, panel_id);
        if (location.stack == DockNodeId{}) return refuse(DockRefusalReason::not_placed, DockRefusalLayer::model, panel_id);
        if (!(*descriptor).allowed.hidden) {
            return refuse(DockRefusalReason::placement_not_allowed, DockRefusalLayer::panel, panel_id);
        }
        detach_panel(panel_id);
        return accepted();
    }

    DockRefusal close(const DockPanelId& panel_id) {
        const DockPanelDescriptor* descriptor = find_dock_descriptor(rules_.descriptors, panel_id);
        if (descriptor == nullptr) return refuse(DockRefusalReason::unknown_panel, DockRefusalLayer::model, panel_id);
        const DockPanelLocation location = locate_dock_panel(state_, panel_id);
        if (location.placement == DockPlacementKind::absent) {
            return refuse(DockRefusalReason::not_placed, DockRefusalLayer::model, panel_id);
        }
        if (!(*descriptor).allowed.closed) {
            return refuse(DockRefusalReason::placement_not_allowed, DockRefusalLayer::panel, panel_id);
        }
        detach_panel(panel_id);
        // A closed document is gone; a closed tool panel keeps its
        // placeholder so it reopens where it was.
        if ((*descriptor).kind == DockPanelKind::document) erase_dock_record(state_, panel_id);
        return accepted();
    }

    DockRefusal move(const DockPanelId& panel_id, std::uint32_t index) {
        const DockPanelLocation location = locate_dock_panel(state_, panel_id);
        if (location.stack == DockNodeId{}) return refuse(DockRefusalReason::not_placed, DockRefusalLayer::model, panel_id);
        DockNode& stack = *find_dock_node(state_, location.stack);
        const DockPanelId selected_panel = stack.panels[stack.selected];
        stack.panels.erase(stack.panels.begin() + static_cast<std::ptrdiff_t>(location.index));
        const std::size_t at = std::min<std::size_t>(index, stack.panels.size());
        stack.panels.insert(stack.panels.begin() + static_cast<std::ptrdiff_t>(at), panel_id);
        const std::vector<DockPanelId>::iterator chosen =
            std::find(stack.panels.begin(), stack.panels.end(), selected_panel);
        stack.selected = static_cast<std::uint32_t>(chosen - stack.panels.begin());
        return accepted();
    }

    DockRefusal select(const DockPanelId& panel_id) {
        const DockPanelLocation location = locate_dock_panel(state_, panel_id);
        if (location.stack == DockNodeId{}) return refuse(DockRefusalReason::not_placed, DockRefusalLayer::model, panel_id);
        (*find_dock_node(state_, location.stack)).selected = location.index;
        return accepted();
    }

    DockRefusal set_ratio(DockNodeId split_id, double ratio) {
        DockNode* split = find_dock_node(state_, split_id);
        if (split == nullptr) return refuse(DockRefusalReason::unknown_node, DockRefusalLayer::model);
        if ((*split).kind != DockNodeKind::split) return refuse(DockRefusalReason::not_a_split, DockRefusalLayer::model);
        if (!valid_ratio(ratio)) return refuse(DockRefusalReason::invalid_ratio, DockRefusalLayer::model);
        (*split).ratio = ratio;
        return accepted();
    }

    DockRefusal set_floating_frame(DockSiteId site_id, Rect frame, const std::string& monitor_id) {
        DockFloatingSite* site = find_dock_site(state_, site_id);
        if (site == nullptr) return refuse(DockRefusalReason::unknown_site, DockRefusalLayer::model);
        if (!valid_frame(frame)) return refuse(DockRefusalReason::invalid_frame, DockRefusalLayer::model);
        (*site).frame = frame;
        (*site).monitor_id = monitor_id;
        return accepted();
    }

    // show: puts a hidden or new panel back where it was last, trying its
    // remembered places in order, then its descriptor's default. Each try
    // runs on its own copy, so a refused try leaves nothing behind.
    bool try_edit(const DockEdit& edit, DockRefusal& last) {
        DockLayoutState trial = state_;
        DockEditor editor(trial, rules_, DockEditOrigin::program);
        const DockRefusal refusal = editor.apply(edit);
        if (is_refusal(refusal)) {
            last = refusal;
            return false;
        }
        state_ = std::move(trial);
        return true;
    }

    // A new auto-hidden stack holding one panel, on edge.
    DockRefusal auto_hide_alone(const DockPanelId& panel_id, DockSide edge, double extent) {
        if (!(std::isfinite(extent) && extent > 0.0)) {
            return refuse(DockRefusalReason::invalid_extent, DockRefusalLayer::model);
        }
        ensure_record(panel_id);
        std::vector<DockPanelId> panels{panel_id};
        DockNodeId stack_id{};
        const DockRefusal added = add_stack(DockNodeKind::tool_stack, std::move(panels), 0, stack_id);
        if (is_refusal(added)) return added;
        const DockNodePlace checked_place{.region = DockRegion::auto_hidden, .site = {}, .side = edge};
        const DockRefusal admitted = admit_stack(stack_id, checked_place);
        if (is_refusal(admitted)) return admitted;
        DockAutoHideEntry entry{};
        entry.stack = stack_id;
        entry.edge = edge;
        entry.extent = extent;
        entry.side = edge;
        state_.auto_hidden.push_back(entry);
        return accepted();
    }

    bool try_auto_hide_alone(const DockPanelId& panel_id, DockSide edge, double extent, DockRefusal& last) {
        DockLayoutState trial = state_;
        DockEditor editor(trial, rules_, DockEditOrigin::program);
        const DockRefusal refusal = editor.auto_hide_alone(panel_id, edge, extent);
        if (is_refusal(refusal)) {
            last = refusal;
            return false;
        }
        state_ = std::move(trial);
        return true;
    }

    bool try_remembered(const DockPanelId& panel_id, const DockPanelRecord& record, DockRefusal& last) {
        const DockSubject subject = DockSubject::of_panel(panel_id);
        if (record.last_placement == DockPlacementKind::docked && record.docked.has_value()) {
            const DockDockedMemory& memory = *record.docked;
            const DockNode* stack = find_dock_node(state_, memory.stack);
            if (stack != nullptr && is_dock_stack((*stack).kind) &&
                dock_place_of(state_, memory.stack).region != DockRegion::detached) {
                if (try_edit(DockEdit::dock_into(subject, memory.stack, memory.index), last)) return true;
            }
            if (find_dock_node(state_, memory.neighbour) != nullptr) {
                const double ratio = valid_ratio(memory.ratio) ? memory.ratio : 0.25;
                if (try_edit(DockEdit::dock_beside(subject, memory.neighbour, memory.side, ratio), last)) {
                    return true;
                }
            }
        }
        if (record.last_placement == DockPlacementKind::floating && record.floating.has_value()) {
            const DockFloatingMemory& memory = *record.floating;
            const DockFloatingSite* site = find_dock_site(state_, memory.site);
            if (site != nullptr) {
                const DockNodeId stack = first_stack_in((*site).root);
                if (try_edit(DockEdit::dock_into(subject, stack), last)) return true;
            }
            if (try_edit(DockEdit::float_subject(subject, memory.frame, memory.monitor_id), last)) return true;
        }
        if (record.last_placement == DockPlacementKind::auto_hidden && record.auto_hidden.has_value()) {
            const DockAutoHideMemory& memory = *record.auto_hidden;
            if (find_dock_auto_hidden(state_, memory.stack) != nullptr) {
                if (try_edit(DockEdit::dock_into(subject, memory.stack), last)) return true;
            }
            if (try_auto_hide_alone(panel_id, memory.edge, memory.extent, last)) return true;
        }
        return false;
    }

    bool try_default(const DockPanelDescriptor& descriptor, DockRefusal& last) {
        const DockSubject subject = DockSubject::of_panel(descriptor.id);
        const DockDefaultPlacement& placement = descriptor.default_placement;
        if (descriptor.kind == DockPanelKind::document) {
            const bool docked = try_edit(DockEdit::dock_into(subject, document_area_id()), last);
            return docked;
        }
        if (placement.kind == DockPlacementKind::floating) {
            const bool floated = try_edit(DockEdit::float_subject(subject, placement.frame, placement.monitor_id), last);
            return floated;
        }
        if (placement.kind == DockPlacementKind::auto_hidden) {
            const bool hidden = try_auto_hide_alone(descriptor.id, placement.side, placement.extent, last);
            return hidden;
        }
        const bool edged = try_edit(DockEdit::dock_at_edge(subject, placement.side, placement.ratio), last);
        return edged;
    }

    [[nodiscard]] DockNodeId document_area_id() const {
        for (const DockNode& node : state_.nodes) {
            if (node.kind == DockNodeKind::document_area) return node.id;
        }
        return DockNodeId{};
    }

    DockRefusal show(const DockPanelId& panel_id) {
        const DockPanelDescriptor* descriptor = find_dock_descriptor(rules_.descriptors, panel_id);
        if (descriptor == nullptr) return refuse(DockRefusalReason::unknown_panel, DockRefusalLayer::model, panel_id);
        const DockPanelLocation location = locate_dock_panel(state_, panel_id);
        if (!(location.stack == DockNodeId{})) {
            return refuse(DockRefusalReason::already_placed, DockRefusalLayer::model, panel_id);
        }
        DockRefusal last = refuse(DockRefusalReason::placement_not_allowed, DockRefusalLayer::panel, panel_id);
        const DockPanelRecord* record = find_dock_record(state_, panel_id);
        if (record != nullptr) {
            const DockPanelRecord copy = *record;
            if (try_remembered(panel_id, copy, last)) return accepted();
        }
        if (try_default(*descriptor, last)) return accepted();
        return last;
    }

    DockLayoutState& state_;
    const DockRules& rules_;
    DockEditOrigin origin_{};
};

} // namespace

DockRefusal apply_dock_edit(DockLayoutState& state, const DockEdit& edit, const DockRules& rules,
                            DockEditOrigin origin) {
    DockEditor editor(state, rules, origin);
    DockRefusal refusal = editor.apply(edit);
    return refusal;
}

void detach_dock_panel(DockLayoutState& state, const DockPanelId& panel) {
    const std::vector<DockPanelDescriptor> no_descriptors{};
    const std::vector<DockGroupDescriptor> no_groups{};
    const DockSiteRestrictions no_restrictions{};
    const DockRules rules{.descriptors = no_descriptors,
                          .groups = no_groups,
                          .main_site = no_restrictions,
                          .floating_site = no_restrictions,
                          .user_docking_locked = false};
    DockEditor editor(state, rules, DockEditOrigin::program);
    editor.detach_panel(panel);
}

} // namespace gui_forms::detail
