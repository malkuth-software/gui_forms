#include "dock_layout_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::detail {

namespace {

struct NodeIdBefore final {
    bool operator()(const DockNode& node, DockNodeId id) const noexcept {
        return node.id < id;
    }
};

struct RecordIdBefore final {
    bool operator()(const DockPanelRecord& record, const DockPanelId& id) const noexcept {
        return record.id < id;
    }
};

[[nodiscard]] std::string node_text(DockNodeId id) {
    std::string text = "node " + std::to_string(id.value);
    return text;
}

[[nodiscard]] bool valid_ratio(double ratio) noexcept {
    const bool valid = std::isfinite(ratio) && ratio > 0.0 && ratio < 1.0;
    return valid;
}

} // namespace

DockNode* find_dock_node(DockLayoutState& state, DockNodeId id) noexcept {
    std::vector<DockNode>::iterator found =
        std::lower_bound(state.nodes.begin(), state.nodes.end(), id, NodeIdBefore{});
    if (found == state.nodes.end() || !((*found).id == id)) return nullptr;
    DockNode* node = &*found;
    return node;
}

const DockNode* find_dock_node(const DockLayoutState& state, DockNodeId id) noexcept {
    std::vector<DockNode>::const_iterator found =
        std::lower_bound(state.nodes.begin(), state.nodes.end(), id, NodeIdBefore{});
    if (found == state.nodes.end() || !((*found).id == id)) return nullptr;
    const DockNode* node = &*found;
    return node;
}

DockPanelRecord* find_dock_record(DockLayoutState& state, const DockPanelId& id) noexcept {
    std::vector<DockPanelRecord>::iterator found =
        std::lower_bound(state.panels.begin(), state.panels.end(), id, RecordIdBefore{});
    if (found == state.panels.end() || !((*found).id == id)) return nullptr;
    DockPanelRecord* record = &*found;
    return record;
}

const DockPanelRecord* find_dock_record(const DockLayoutState& state,
                                        const DockPanelId& id) noexcept {
    std::vector<DockPanelRecord>::const_iterator found =
        std::lower_bound(state.panels.begin(), state.panels.end(), id, RecordIdBefore{});
    if (found == state.panels.end() || !((*found).id == id)) return nullptr;
    const DockPanelRecord* record = &*found;
    return record;
}

DockFloatingSite* find_dock_site(DockLayoutState& state, DockSiteId id) noexcept {
    for (DockFloatingSite& site : state.floating_sites) {
        if (site.id == id) return &site;
    }
    return nullptr;
}

const DockFloatingSite* find_dock_site(const DockLayoutState& state, DockSiteId id) noexcept {
    for (const DockFloatingSite& site : state.floating_sites) {
        if (site.id == id) return &site;
    }
    return nullptr;
}

const DockAutoHideEntry* find_dock_auto_hidden(const DockLayoutState& state,
                                               DockNodeId stack) noexcept {
    for (const DockAutoHideEntry& entry : state.auto_hidden) {
        if (entry.stack == stack) return &entry;
    }
    return nullptr;
}

const DockPanelDescriptor* find_dock_descriptor(const std::vector<DockPanelDescriptor>& descriptors,
                                                const DockPanelId& id) noexcept {
    for (const DockPanelDescriptor& descriptor : descriptors) {
        if (descriptor.id == id) return &descriptor;
    }
    return nullptr;
}

bool is_dock_stack(DockNodeKind kind) noexcept {
    const bool stack = kind == DockNodeKind::tool_stack || kind == DockNodeKind::document_stack;
    return stack;
}

DockNodePlace dock_place_of(const DockLayoutState& state, DockNodeId node) noexcept {
    DockNodePlace place{};
    const DockNode* current = find_dock_node(state, node);
    if (current == nullptr) return place;
    bool inside_document_area = false;
    // The first split on the way up whose other branch holds the document
    // area gives the side the node stands on.
    std::optional<DockSide> side{};
    // The walk is bounded by the node count, so a corrupted parent cycle
    // cannot loop forever.
    std::size_t steps = 0;
    while (!((*current).parent == DockNodeId{})) {
        const DockNode* parent = find_dock_node(state, (*current).parent);
        if (parent == nullptr || steps > state.nodes.size()) return place;
        if ((*parent).kind == DockNodeKind::document_area) inside_document_area = true;
        if (!side && !inside_document_area && (*parent).kind == DockNodeKind::split) {
            const bool on_first = (*parent).first == (*current).id;
            const DockNodeId other = on_first ? (*parent).second : (*parent).first;
            if (dock_subtree_holds_document_area(state, other)) {
                if ((*parent).axis == DockAxis::horizontal) {
                    side = on_first ? DockSide::left : DockSide::right;
                } else {
                    side = on_first ? DockSide::top : DockSide::bottom;
                }
            }
        }
        current = parent;
        ++steps;
    }
    const DockNodeId root = (*current).id;
    if (root == state.main_root) {
        place.region = inside_document_area ? DockRegion::document_area : DockRegion::main_tools;
        if (!inside_document_area) place.side = side;
        return place;
    }
    for (const DockFloatingSite& site : state.floating_sites) {
        if (site.root == root) {
            place.region = DockRegion::floating;
            place.site = site.id;
            return place;
        }
    }
    const DockAutoHideEntry* const pinned = find_dock_auto_hidden(state, root);
    if (pinned != nullptr) {
        place.region = DockRegion::auto_hidden;
        place.side = (*pinned).edge;
    }
    return place;
}

// Depth first over the subtree with a fixed stack; each step visits one
// node, and the walk stops after as many steps as the layout has nodes.
bool dock_subtree_holds_document_area(const DockLayoutState& state, DockNodeId node) noexcept {
    std::array<DockNodeId, 64> pending{};
    std::size_t count = 0U;
    pending[count] = node;
    ++count;
    std::size_t visited = 0U;
    while (count > 0U && visited <= state.nodes.size()) {
        --count;
        const DockNode* const current = find_dock_node(state, pending[count]);
        ++visited;
        if (current == nullptr) continue;
        if ((*current).kind == DockNodeKind::document_area) return true;
        if ((*current).kind != DockNodeKind::split) continue;
        // A tree deeper than the stack is read as not holding it: the side
        // is then unknown and no side restriction applies.
        if (count + 2U > pending.size()) return false;
        pending[count] = (*current).first;
        ++count;
        pending[count] = (*current).second;
        ++count;
    }
    return false;
}

DockPanelLocation locate_dock_panel(const DockLayoutState& state, const DockPanelId& panel) noexcept {
    DockPanelLocation location{};
    for (const DockNode& node : state.nodes) {
        if (!is_dock_stack(node.kind)) continue;
        for (std::size_t index = 0; index < node.panels.size(); ++index) {
            if (!(node.panels[index] == panel)) continue;
            const DockNodePlace place = dock_place_of(state, node.id);
            location.stack = node.id;
            location.index = static_cast<std::uint32_t>(index);
            location.site = place.site;
            if (place.region == DockRegion::floating) {
                location.placement = DockPlacementKind::floating;
            } else if (place.region == DockRegion::auto_hidden) {
                location.placement = DockPlacementKind::auto_hidden;
            } else {
                location.placement = DockPlacementKind::docked;
            }
            return location;
        }
    }
    if (find_dock_record(state, panel) != nullptr) location.placement = DockPlacementKind::hidden;
    return location;
}

void collect_dock_subtree(const DockLayoutState& state, DockNodeId root,
                          std::vector<DockNodeId>& out) {
    // In pre-order (a node, its first subtree, its second), with an
    // explicit work list: an edit runs this on a scratch state that the
    // invariant check has not yet bounded (HOUSE_STYLE section 1). A
    // corrupt state whose links loop stops after one visit per node.
    std::vector<DockNodeId> pending{};
    pending.push_back(root);
    std::size_t visits = 0U;
    while (!pending.empty() && visits <= state.nodes.size()) {
        const DockNodeId id = pending.back();
        pending.pop_back();
        const DockNode* node = find_dock_node(state, id);
        if (node == nullptr) continue;
        ++visits;
        out.push_back(id);
        const DockNodeId first = (*node).first;
        const DockNodeId second = (*node).second;
        if (!(second == DockNodeId{})) pending.push_back(second);
        if (!(first == DockNodeId{})) pending.push_back(first);
    }
}

void insert_dock_node(DockLayoutState& state, DockNode&& node) {
    std::vector<DockNode>::iterator at =
        std::lower_bound(state.nodes.begin(), state.nodes.end(), node.id, NodeIdBefore{});
    state.nodes.insert(at, std::move(node));
}

void insert_dock_record(DockLayoutState& state, DockPanelRecord&& record) {
    std::vector<DockPanelRecord>::iterator at =
        std::lower_bound(state.panels.begin(), state.panels.end(), record.id, RecordIdBefore{});
    state.panels.insert(at, std::move(record));
}

void erase_dock_record(DockLayoutState& state, const DockPanelId& id) {
    std::vector<DockPanelRecord>::iterator at =
        std::lower_bound(state.panels.begin(), state.panels.end(), id, RecordIdBefore{});
    if (at != state.panels.end() && (*at).id == id) state.panels.erase(at);
}

namespace {

// The invariant check, as one pass that stops at the first broken rule.
class InvariantCheck final {
public:
    explicit InvariantCheck(const DockLayoutState& state) : state_(state) {}

    DockInvariantResult run() {
        if (!check_tables()) return result_;
        visited_.assign(state_.nodes.size(), false);
        if (!check_tree(state_.main_root, DockNodeId{}, DockRegion::main_tools)) return result_;
        if (document_areas_ != 1U) return broken("the main site does not hold exactly one document area");
        for (const DockFloatingSite& site : state_.floating_sites) {
            if (!check_tree(site.root, DockNodeId{}, DockRegion::floating)) return result_;
        }
        for (const DockAutoHideEntry& entry : state_.auto_hidden) {
            const DockNode* stack = find_dock_node(state_, entry.stack);
            if (stack == nullptr || (*stack).kind != DockNodeKind::tool_stack) {
                return broken("an auto-hidden entry is not a tool stack");
            }
            if (!(std::isfinite(entry.extent) && entry.extent > 0.0)) {
                return broken("an auto-hidden extent is not positive");
            }
            if (!check_tree(entry.stack, DockNodeId{}, DockRegion::auto_hidden)) return result_;
        }
        for (std::size_t index = 0; index < visited_.size(); ++index) {
            if (!visited_[index]) return broken(node_text(state_.nodes[index].id) + " is in no site");
        }
        return result_;
    }

private:
    // Records the first broken rule; `fail` answers the checks below,
    // `broken` the result run() returns.
    bool fail(std::string problem) {
        result_.valid = false;
        result_.problem = std::move(problem);
        return false;
    }

    DockInvariantResult broken(std::string problem) {
        static_cast<void>(fail(std::move(problem)));
        return result_;
    }

    bool check_tables() {
        if (state_.next_id == 0U || state_.next_id > dock_identity_limit) {
            return fail("next_id is outside the identity range");
        }
        for (std::size_t index = 0; index < state_.nodes.size(); ++index) {
            const DockNodeId id = state_.nodes[index].id;
            if (id.value == 0U || id.value >= state_.next_id) return fail(node_text(id) + " has an invalid ID");
            if (index > 0U && !(state_.nodes[index - 1U].id < id)) {
                return fail("nodes are not sorted by unique ID");
            }
        }
        for (std::size_t index = 0; index < state_.panels.size(); ++index) {
            if (state_.panels[index].id.value.empty()) return fail("a panel record has an empty ID");
            if (index > 0U && !(state_.panels[index - 1U].id < state_.panels[index].id)) {
                return fail("panel records are not sorted by unique ID");
            }
        }
        if (state_.floating_sites.size() > dock_maximum_floating_sites) {
            return fail("more floating sites than the limit");
        }
        for (std::size_t index = 0; index < state_.floating_sites.size(); ++index) {
            const DockFloatingSite& site = state_.floating_sites[index];
            if (site.id.value == 0U || site.id.value >= state_.next_id) {
                return fail("a floating site has an invalid ID");
            }
            const DockNodeId as_node{site.id.value};
            if (find_dock_node(state_, as_node) != nullptr) {
                return fail("a floating site shares its ID with a node");
            }
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (state_.floating_sites[earlier].id == site.id) return fail("two floating sites share an ID");
            }
            if (!site.frame.finite() || site.frame.empty()) return fail("a floating frame is not valid");
        }
        for (std::size_t index = 0; index < state_.auto_hidden.size(); ++index) {
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (state_.auto_hidden[earlier].stack == state_.auto_hidden[index].stack) {
                    return fail("a stack is auto-hidden twice");
                }
            }
        }
        return true;
    }

    bool mark(DockNodeId id) {
        for (std::size_t index = 0; index < state_.nodes.size(); ++index) {
            if (!(state_.nodes[index].id == id)) continue;
            if (visited_[index]) return fail(node_text(id) + " is reached twice");
            visited_[index] = true;
            return true;
        }
        return fail(node_text(id) + " does not exist");
    }

    bool check_panels(const DockNode& node) {
        if (node.panels.empty()) return fail(node_text(node.id) + " is an empty stack");
        if (node.selected >= node.panels.size()) return fail(node_text(node.id) + " selects past its end");
        if (!(node.first == DockNodeId{}) || !(node.second == DockNodeId{})) {
            return fail(node_text(node.id) + " is a stack with children");
        }
        for (const DockPanelId& panel : node.panels) {
            const DockPanelRecord* record = find_dock_record(state_, panel);
            if (record == nullptr) return fail("panel " + panel.value + " has no record");
            if (node.kind == DockNodeKind::tool_stack && (*record).kind != DockPanelKind::tool) {
                return fail("document " + panel.value + " is in a tool stack");
            }
            for (const DockPanelId& seen : seen_panels_) {
                if (seen == panel) return fail("panel " + panel.value + " is placed twice");
            }
            seen_panels_.push_back(panel);
        }
        return true;
    }

    // One node still to check: where it hangs and how deep it is (a site
    // root is at level one).
    struct TreeStep final {
        DockNodeId id{};
        DockNodeId parent{};
        DockRegion region{DockRegion::main_tools};
        std::size_t depth{0U};
    };

    // Checks a site's tree in pre-order (a node, its first subtree, its
    // second) with an explicit work list, refusing a node past
    // dock_maximum_depth before reading below it (HOUSE_STYLE section 1).
    // The list holds at most one pending second child per level.
    bool check_tree(DockNodeId root, DockNodeId root_parent, DockRegion root_region) {
        std::vector<TreeStep> pending{};
        pending.reserve(dock_maximum_depth + 1U);
        pending.push_back(TreeStep{root, root_parent, root_region, 1U});
        while (!pending.empty()) {
            const TreeStep step = pending.back();
            pending.pop_back();
            if (step.depth > dock_maximum_depth) {
                return fail(node_text(step.id) + " nests deeper than " + std::to_string(dock_maximum_depth) +
                            " levels");
            }
            if (!check_node(step, pending)) return false;
        }
        return true;
    }

    // One node's rules; queues its children, the first to be checked next.
    bool check_node(const TreeStep& step, std::vector<TreeStep>& pending) {
        const DockNodeId id = step.id;
        if (!mark(id)) return false;
        const DockNode& node = *find_dock_node(state_, id);
        if (!(node.parent == step.parent)) return fail(node_text(id) + " has a wrong parent link");
        if (node.kind == DockNodeKind::split) {
            if (node.first == DockNodeId{} || node.second == DockNodeId{}) {
                return fail(node_text(id) + " is a split without two children");
            }
            if (!valid_ratio(node.ratio)) return fail(node_text(id) + " has a ratio outside (0, 1)");
            if (!node.panels.empty()) return fail(node_text(id) + " is a split holding panels");
            pending.push_back(TreeStep{node.second, id, step.region, step.depth + 1U});
            pending.push_back(TreeStep{node.first, id, step.region, step.depth + 1U});
            return true;
        }
        if (node.kind == DockNodeKind::document_area) {
            if (step.region != DockRegion::main_tools) {
                return fail("a document area outside the main site's tool region");
            }
            ++document_areas_;
            if (!(node.second == DockNodeId{}) || !node.panels.empty()) {
                return fail(node_text(id) + " is a document area with stray fields");
            }
            if (node.first == DockNodeId{}) return true;
            pending.push_back(TreeStep{node.first, id, DockRegion::document_area, step.depth + 1U});
            return true;
        }
        if (step.region == DockRegion::document_area && node.kind != DockNodeKind::document_stack) {
            return fail(node_text(id) + " is a tool stack inside the document area");
        }
        if ((step.region == DockRegion::main_tools || step.region == DockRegion::auto_hidden) &&
            node.kind != DockNodeKind::tool_stack) {
            return fail(node_text(id) + " is a document stack outside the document area");
        }
        const bool panels = check_panels(node);
        return panels;
    }

    const DockLayoutState& state_;
    DockInvariantResult result_{};
    std::vector<bool> visited_{};
    std::vector<DockPanelId> seen_panels_{};
    std::size_t document_areas_{0};
};

} // namespace

const DockUnknownFields* find_unknown_fields(const DockLayoutState& state, const DockUnknownOwner& owner) noexcept {
    for (const DockUnknownFields& fields : state.unknown_fields) {
        if (fields.owner == owner) return &fields;
    }
    return nullptr;
}

bool unknown_owner_exists(const DockLayoutState& state, const DockUnknownOwner& owner) noexcept {
    const DockPanelRecord* record = nullptr;
    switch (owner.kind) {
    case DockUnknownOwnerKind::layout:
    case DockUnknownOwnerKind::main_site:
        return true;
    case DockUnknownOwnerKind::window:
        return state.main_window.has_value();
    case DockUnknownOwnerKind::floating_site:
        return find_dock_site(state, DockSiteId{owner.id}) != nullptr;
    case DockUnknownOwnerKind::auto_hidden:
        return find_dock_auto_hidden(state, DockNodeId{owner.id}) != nullptr;
    case DockUnknownOwnerKind::node:
        return find_dock_node(state, DockNodeId{owner.id}) != nullptr &&
               dock_place_of(state, DockNodeId{owner.id}).region != DockRegion::detached;
    case DockUnknownOwnerKind::panel:
        return !(locate_dock_panel(state, owner.panel).stack == DockNodeId{});
    case DockUnknownOwnerKind::panel_record:
        return find_dock_record(state, owner.panel) != nullptr;
    case DockUnknownOwnerKind::docked_memory:
        record = find_dock_record(state, owner.panel);
        return record != nullptr && (*record).docked.has_value();
    case DockUnknownOwnerKind::floating_memory:
        record = find_dock_record(state, owner.panel);
        return record != nullptr && (*record).floating.has_value();
    case DockUnknownOwnerKind::auto_hidden_memory:
        record = find_dock_record(state, owner.panel);
        return record != nullptr && (*record).auto_hidden.has_value();
    }
    return false;
}

void prune_unknown_fields(DockLayoutState& state, std::vector<DockUnknownOwner>& dropped) {
    std::size_t kept = 0U;
    for (std::size_t index = 0; index < state.unknown_fields.size(); ++index) {
        if (!unknown_owner_exists(state, state.unknown_fields[index].owner)) {
            dropped.push_back(state.unknown_fields[index].owner);
            continue;
        }
        if (kept != index) state.unknown_fields[kept] = std::move(state.unknown_fields[index]);
        ++kept;
    }
    state.unknown_fields.resize(kept);
}

DockInvariantResult check_dock_invariants(const DockLayoutState& state) {
    InvariantCheck check(state);
    DockInvariantResult result = check.run();
    return result;
}

} // namespace gui_forms::detail
