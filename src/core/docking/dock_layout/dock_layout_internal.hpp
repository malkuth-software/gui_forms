#pragma once

#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gui_forms::detail {

// The registry and restrictions an edit is checked against, borrowed from a
// DockLayout for the duration of one transaction or restore.
struct DockRules final {
    const std::vector<DockPanelDescriptor>& descriptors;
    const std::vector<DockGroupDescriptor>& groups;
    const DockSiteRestrictions& main_site;
    const DockSiteRestrictions& floating_site;
    bool user_docking_locked;
};

// Where a node stands. A node's region is document_area when one of its
// strict ancestors is the document area (so the document area itself is in
// main_tools); detached means it is in no site.
enum class DockRegion : std::uint8_t { main_tools, document_area, floating, auto_hidden, detached };

struct DockNodePlace final {
    DockRegion region{DockRegion::detached};
    DockSiteId site{};
    // main_tools: the side of the document area the node stands on (none
    // without a document area beside it); auto_hidden: the window edge.
    std::optional<DockSide> side{};
};

// Lookups. Pointers borrow from the state and are invalidated by any change
// to the vector they point into (adding a node can reallocate nodes). A
// temporary state is refused (house style section 1).
[[nodiscard]] DockNode* find_dock_node(DockLayoutState& state, DockNodeId id) noexcept;
// Whether the subtree under `node` holds the document area.
[[nodiscard]] bool dock_subtree_holds_document_area(const DockLayoutState& state, DockNodeId node) noexcept;
[[nodiscard]] const DockNode* find_dock_node(const DockLayoutState& state, DockNodeId id) noexcept;
const DockNode* find_dock_node(DockLayoutState&& state, DockNodeId id) = delete;
[[nodiscard]] DockPanelRecord* find_dock_record(DockLayoutState& state, const DockPanelId& id) noexcept;
[[nodiscard]] const DockPanelRecord* find_dock_record(const DockLayoutState& state,
                                                     const DockPanelId& id) noexcept;
const DockPanelRecord* find_dock_record(DockLayoutState&& state, const DockPanelId& id) = delete;
[[nodiscard]] DockFloatingSite* find_dock_site(DockLayoutState& state, DockSiteId id) noexcept;
[[nodiscard]] const DockFloatingSite* find_dock_site(const DockLayoutState& state,
                                                     DockSiteId id) noexcept;
const DockFloatingSite* find_dock_site(DockLayoutState&& state, DockSiteId id) = delete;
[[nodiscard]] const DockAutoHideEntry* find_dock_auto_hidden(const DockLayoutState& state,
                                                             DockNodeId stack) noexcept;
const DockAutoHideEntry* find_dock_auto_hidden(DockLayoutState&& state, DockNodeId stack) = delete;
// The result borrows from `descriptors`; a temporary list is refused (house
// style section 1).
[[nodiscard]] const DockPanelDescriptor* find_dock_descriptor(
    const std::vector<DockPanelDescriptor>& descriptors, const DockPanelId& id) noexcept;
const DockPanelDescriptor* find_dock_descriptor(std::vector<DockPanelDescriptor>&& descriptors,
                                                const DockPanelId& id) = delete;

[[nodiscard]] DockNodePlace dock_place_of(const DockLayoutState& state, DockNodeId node) noexcept;
[[nodiscard]] DockPanelLocation locate_dock_panel(const DockLayoutState& state,
                                                  const DockPanelId& panel) noexcept;
[[nodiscard]] bool is_dock_stack(DockNodeKind kind) noexcept;

// Appends a subtree's node IDs in tree order (a node, then its first child's
// subtree, then its second's).
void collect_dock_subtree(const DockLayoutState& state, DockNodeId root,
                          std::vector<DockNodeId>& out);

// Inserts a node keeping nodes sorted by ID; records likewise by panel ID.
void insert_dock_node(DockLayoutState& state, DockNode&& node);
void insert_dock_record(DockLayoutState& state, DockPanelRecord&& record);
void erase_dock_record(DockLayoutState& state, const DockPanelId& id);

// Applies one edit to state, which the caller owns as a scratch copy. A
// refusal (reason other than none) may leave state partly edited: the
// caller discards the copy.
[[nodiscard]] DockRefusal apply_dock_edit(DockLayoutState& state, const DockEdit& edit,
                                          const DockRules& rules, DockEditOrigin origin);

// Removes a panel from the stack holding it, remembering where it was, and
// prunes what that leaves empty. Restore uses it to turn panels into
// placeholders. Does nothing when the panel is in no stack.
void detach_dock_panel(DockLayoutState& state, const DockPanelId& panel);

[[nodiscard]] DockInvariantResult check_dock_invariants(const DockLayoutState& state);

// Kept unknown fields (DK decision 48). The entry for an owner, or null;
// whether the owner's element is still in the layout; and dropping every
// entry whose element is gone, appending their owners to `dropped`.
[[nodiscard]] const DockUnknownFields* find_unknown_fields(const DockLayoutState& state,
                                                          const DockUnknownOwner& owner) noexcept;
const DockUnknownFields* find_unknown_fields(DockLayoutState&& state, const DockUnknownOwner& owner) = delete;
[[nodiscard]] bool unknown_owner_exists(const DockLayoutState& state, const DockUnknownOwner& owner) noexcept;
void prune_unknown_fields(DockLayoutState& state, std::vector<DockUnknownOwner>& dropped);

} // namespace gui_forms::detail
