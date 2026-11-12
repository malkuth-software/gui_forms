#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

// Identities. A panel ID is the application's stable string for a panel
// (for example "tools.properties" or "doc:file:///a.txt"); it is what a saved
// layout records and what a factory is asked to build. A dock group ID names
// a set of panels that may share a stack (DK decision 21); the empty string
// is the default group. Node and site IDs are numbers the layout assigns; they
// are saved with the layout and stay stable across edits and restores. Zero
// means "none".
struct GUI_FORMS_API DockPanelId final {
    std::string value;

    DockPanelId() = default;
    explicit DockPanelId(std::string identity) : value(std::move(identity)) {}

    friend bool operator==(const DockPanelId& left, const DockPanelId& right) noexcept {
        return left.value == right.value;
    }
    friend bool operator<(const DockPanelId& left, const DockPanelId& right) noexcept {
        return left.value < right.value;
    }
};

struct GUI_FORMS_API DockGroupId final {
    std::string value;

    DockGroupId() = default;
    explicit DockGroupId(std::string identity) : value(std::move(identity)) {}

    friend bool operator==(const DockGroupId& left, const DockGroupId& right) noexcept {
        return left.value == right.value;
    }
};

struct GUI_FORMS_API DockNodeId final {
    std::uint32_t value{};

    friend bool operator==(DockNodeId left, DockNodeId right) noexcept {
        return left.value == right.value;
    }
    friend bool operator<(DockNodeId left, DockNodeId right) noexcept {
        return left.value < right.value;
    }
};

struct GUI_FORMS_API DockSiteId final {
    std::uint32_t value{};

    friend bool operator==(DockSiteId left, DockSiteId right) noexcept {
        return left.value == right.value;
    }
};

enum class DockPanelKind : std::uint8_t { tool, document };

// horizontal: the first child is on the left and the second on the right;
// vertical: the first child is on top.
enum class DockAxis : std::uint8_t { horizontal, vertical };

enum class DockSide : std::uint8_t { left, top, right, bottom };

enum class DockNodeKind : std::uint8_t { split, tool_stack, document_stack, document_area };

// Where a panel is. hidden: the layout keeps a placeholder for it (DK
// decision 22) but it is in no stack; absent: the layout has never held it
// (or it was a closed document).
enum class DockPlacementKind : std::uint8_t { docked, floating, auto_hidden, hidden, absent };

// What a panel permits (the first of the three restriction layers). Each
// field defaults to permitted.
struct GUI_FORMS_API DockAllowedPlacements final {
    // In a tool stack of the main site, outside the document area.
    bool docked{true};
    // In the document area; a tool panel also needs dock_as_document.
    bool document_area{true};
    bool floating{true};
    bool auto_hidden{true};
    // Sharing a stack with other panels.
    bool tabbed{true};
    bool hidden{true};
    bool closed{true};
    // The sides of the document area a docked stack holding the panel may
    // stand on, and the window edges it may be auto-hidden to (DockPanel
    // Suite's DockLeft, DockTop, DockRight and DockBottom areas).
    bool left{true};
    bool top{true};
    bool right{true};
    bool bottom{true};

    friend bool operator==(const DockAllowedPlacements& first,
                           const DockAllowedPlacements& second) noexcept {
        return first.docked == second.docked && first.document_area == second.document_area &&
               first.floating == second.floating && first.auto_hidden == second.auto_hidden &&
               first.tabbed == second.tabbed && first.hidden == second.hidden &&
               first.closed == second.closed && first.left == second.left && first.top == second.top &&
               first.right == second.right && first.bottom == second.bottom;
    }

    [[nodiscard]] bool allows_side(DockSide side) const noexcept {
        if (side == DockSide::left) return left;
        if (side == DockSide::top) return top;
        if (side == DockSide::right) return right;
        return bottom;
    }
};

// Where show() puts a panel that the layout has no memory of. A document
// always goes to the document area; for a tool panel, docked means against
// the main site's outer edge at side with ratio, auto_hidden means on edge
// side with extent, floating means frame on monitor_id.
struct GUI_FORMS_API DockDefaultPlacement final {
    DockPlacementKind kind{DockPlacementKind::docked};
    DockSide side{DockSide::right};
    // The new item's share of the split it creates, in (0, 1).
    double ratio{0.25};
    // An auto-hidden flyout's extent, in logical units.
    double extent{240.0};
    // A floating site's frame, in screen logical units.
    Rect frame{100.0, 100.0, 320.0, 240.0};
    std::string monitor_id{};
};

struct GUI_FORMS_API DockPanelDescriptor final {
    DockPanelId id{};
    DockPanelKind kind{DockPanelKind::tool};
    // A tool panel that may also stand in a document stack (DK decision 20).
    bool dock_as_document{false};
    DockGroupId group{};
    DockAllowedPlacements allowed{};
    // When the window is too small, tool panels collapse to auto-hide in
    // increasing collapse_priority (DK decision 23); the layout model only
    // records it.
    std::int32_t collapse_priority{0};
    // The panel's smallest content size, in logical units; the split solver
    // keeps each side of a split at least this large where it can.
    Size minimum_size{};
    DockDefaultPlacement default_placement{};
    // Whether its caption and tab show a close button. Hidden, the panel
    // still closes or hides through its position menu and the program.
    bool close_button_visible{true};
};

// Which other groups a dock group admits into a stack beside its own panels
// (the second restriction layer). Two panels may share a stack when their
// groups are equal or either group's descriptor lists the other. A group
// without a descriptor admits only itself.
struct GUI_FORMS_API DockGroupDescriptor final {
    DockGroupId id{};
    std::vector<DockGroupId> admits{};
};

// What one site accepts (the third restriction layer). The main site has one
// record; every floating site shares another.
struct GUI_FORMS_API DockSiteRestrictions final {
    bool accepts_tools{true};
    bool accepts_documents{true};
    // Empty: every group.
    std::vector<DockGroupId> admitted_groups{};
};

// Remembered placements (DK decision 22). Each is written when a panel
// leaves that kind of place, and read by show() to put it back.
struct GUI_FORMS_API DockDockedMemory final {
    // The stack the panel was in, and its index there.
    DockNodeId stack{};
    std::uint32_t index{};
    // The node beside that stack and which side of it the stack was on, with
    // the stack's share of their split; used when the stack no longer exists.
    DockNodeId neighbour{};
    DockSide side{DockSide::left};
    double ratio{0.25};

    friend bool operator==(const DockDockedMemory& left, const DockDockedMemory& right) noexcept {
        return left.stack == right.stack && left.index == right.index &&
               left.neighbour == right.neighbour && left.side == right.side &&
               left.ratio == right.ratio;
    }
};

struct GUI_FORMS_API DockFloatingMemory final {
    DockSiteId site{};
    Rect frame{};
    std::string monitor_id{};

    friend bool operator==(const DockFloatingMemory& left,
                           const DockFloatingMemory& right) noexcept {
        return left.site == right.site && left.frame == right.frame &&
               left.monitor_id == right.monitor_id;
    }
};

struct GUI_FORMS_API DockAutoHideMemory final {
    DockNodeId stack{};
    DockSide edge{DockSide::left};
    double extent{240.0};

    friend bool operator==(const DockAutoHideMemory& left,
                           const DockAutoHideMemory& right) noexcept {
        return left.stack == right.stack && left.edge == right.edge &&
               left.extent == right.extent;
    }
};

// The layout's record of one panel: kept while the panel is placed and while
// it is hidden (then it is the placeholder), including for panel IDs no
// descriptor is registered for (a removed plugin's panel, kept so that its
// return restores it; DK decision 34).
struct GUI_FORMS_API DockPanelRecord final {
    DockPanelId id{};
    DockPanelKind kind{DockPanelKind::tool};
    // Where show() returns a hidden panel: docked, floating or auto_hidden.
    DockPlacementKind last_placement{DockPlacementKind::docked};
    std::optional<DockDockedMemory> docked{};
    std::optional<DockFloatingMemory> floating{};
    std::optional<DockAutoHideMemory> auto_hidden{};

    friend bool operator==(const DockPanelRecord& left, const DockPanelRecord& right) noexcept {
        return left.id == right.id && left.kind == right.kind &&
               left.last_placement == right.last_placement && left.docked == right.docked &&
               left.floating == right.floating && left.auto_hidden == right.auto_hidden;
    }
};

// One node of a site's tree. Fields that do not apply to a node's kind keep
// their defaults (the invariant check verifies this).
struct GUI_FORMS_API DockNode final {
    DockNodeId id{};
    DockNodeKind kind{DockNodeKind::tool_stack};
    // The node containing this one; none for a site root and for an
    // auto-hidden stack.
    DockNodeId parent{};
    // split: the axis and the first child's share of the span, in (0, 1).
    DockAxis axis{DockAxis::horizontal};
    double ratio{0.5};
    // split: both children. document_area: first is its content, or none
    // while it holds no documents.
    DockNodeId first{};
    DockNodeId second{};
    // tool_stack and document_stack: the panels in tab order, never empty,
    // and the index of the selected one.
    std::vector<DockPanelId> panels{};
    std::uint32_t selected{};

    friend bool operator==(const DockNode& left, const DockNode& right) noexcept {
        return left.id == right.id && left.kind == right.kind && left.parent == right.parent &&
               left.axis == right.axis && left.ratio == right.ratio && left.first == right.first &&
               left.second == right.second && left.panels == right.panels &&
               left.selected == right.selected;
    }
};

// The main window's frame, recorded by the program before it saves so that a
// restore can place the window again; the layout never moves a window.
struct GUI_FORMS_API DockWindowFrame final {
    Rect frame{};
    std::string monitor_id{};
    bool maximized{false};

    friend bool operator==(const DockWindowFrame& left, const DockWindowFrame& right) noexcept {
        return left.frame == right.frame && left.monitor_id == right.monitor_id &&
               left.maximized == right.maximized;
    }
};

struct GUI_FORMS_API DockFloatingSite final {
    DockSiteId id{};
    DockNodeId root{};
    // In screen logical units.
    Rect frame{};
    std::string monitor_id{};
    bool maximized{false};

    friend bool operator==(const DockFloatingSite& left, const DockFloatingSite& right) noexcept {
        return left.id == right.id && left.root == right.root && left.frame == right.frame &&
               left.monitor_id == right.monitor_id && left.maximized == right.maximized;
    }
};

// A tool stack pinned out to an edge of the main site (auto-hide). The stack
// keeps its node ID while it is out, so unpinning returns the same stack.
// Pinning out and unpinning are inverse edits: unpinning beside the same
// neighbour recreates the split the stack stood in with its old identity and
// ratio, and neither edit writes the panels' remembered places, so a pin
// followed by an unpin leaves the layout exactly as it was.
struct GUI_FORMS_API DockAutoHideEntry final {
    DockNodeId stack{};
    DockSide edge{DockSide::left};
    // The flyout's extent, in logical units.
    double extent{240.0};
    // Where the stack stood before it was pinned out: the node beside it,
    // which side of that node, and the stack's share of their split.
    DockNodeId neighbour{};
    DockSide side{DockSide::left};
    double ratio{0.25};
    // The split the stack stood in (removed while the stack is out) and that
    // split's own ratio (its first child's share); none when the stack stood
    // alone, for example in a restored layout written before these were
    // saved.
    DockNodeId split{};
    double split_ratio{0.5};

    friend bool operator==(const DockAutoHideEntry& left,
                           const DockAutoHideEntry& right) noexcept {
        return left.stack == right.stack && left.edge == right.edge &&
               left.extent == right.extent && left.neighbour == right.neighbour &&
               left.side == right.side && left.ratio == right.ratio &&
               left.split == right.split && left.split_ratio == right.split_ratio;
    }
};

// The known element of a saved layout that unknown fields were found on (DK
// decision 48): attributes and child elements a newer format added, kept by
// restore and written back in place.
enum class DockUnknownOwnerKind : std::uint8_t {
    // <dock_layout>, <main_site> and the main site's <window>.
    layout,
    main_site,
    window,
    // <floating_site>, by its ID.
    floating_site,
    // An <auto_hidden> entry, by its stack's ID.
    auto_hidden,
    // A <split>, stack or <document_area>, by the node's ID.
    node,
    // A <panel> in a stack, by the panel: the fields follow the panel to
    // whichever stack holds it.
    panel,
    // A <panel_record> and its <docked>, <floating> and <auto_hidden>
    // memories, by the panel.
    panel_record,
    docked_memory,
    floating_memory,
    auto_hidden_memory,
};

struct GUI_FORMS_API DockUnknownOwner final {
    DockUnknownOwnerKind kind{DockUnknownOwnerKind::layout};
    // floating_site: the site's ID; auto_hidden: its stack's; node: the
    // node's; otherwise zero.
    std::uint32_t id{};
    // panel, panel_record and the memories: the panel; otherwise empty.
    DockPanelId panel{};

    friend bool operator==(const DockUnknownOwner& left, const DockUnknownOwner& right) noexcept {
        return left.kind == right.kind && left.id == right.id && left.panel == right.panel;
    }
};

// One unknown child element, whole, and how many of its parent's known
// children came before it in the file; it is written back after that many
// known children (or last, when there are fewer now).
struct GUI_FORMS_API DockUnknownChild final {
    std::size_t after_known{};
    XmlElement element{};

    friend bool operator==(const DockUnknownChild& left, const DockUnknownChild& right) noexcept {
        return left.after_known == right.after_known && left.element == right.element;
    }
};

// The unknown attributes (in file order; written after the known ones) and
// child elements of one known element.
struct GUI_FORMS_API DockUnknownFields final {
    DockUnknownOwner owner{};
    std::vector<XmlAttribute> attributes{};
    std::vector<DockUnknownChild> children{};

    friend bool operator==(const DockUnknownFields& left, const DockUnknownFields& right) noexcept {
        return left.owner == right.owner && left.attributes == right.attributes && left.children == right.children;
    }
};

// The whole layout as plain records: one main site (its root is the
// document area or a split that contains it), floating sites, auto-hidden
// stacks and panel records. nodes and panels are kept sorted by ID.
struct GUI_FORMS_API DockLayoutState final {
    std::vector<DockNode> nodes{};
    DockNodeId main_root{};
    std::optional<DockWindowFrame> main_window{};
    std::vector<DockFloatingSite> floating_sites{};
    std::vector<DockAutoHideEntry> auto_hidden{};
    std::vector<DockPanelRecord> panels{};
    // The next node or site ID to assign; node and site IDs share one count.
    // It never exceeds dock_identity_limit, and reaching it means the layout
    // has no identity left to assign.
    std::uint32_t next_id{1};
    // The application's own layout version, saved with the layout.
    std::uint32_t application_version{0};
    // Fields of a newer format this framework does not know, kept from a
    // restore and written back (DK decision 48), one entry per element that
    // had any, in file order. An edit that removes an element drops its
    // entry (DockTransactionResult::diagnostics).
    std::vector<DockUnknownFields> unknown_fields{};

    friend bool operator==(const DockLayoutState& left, const DockLayoutState& right) noexcept {
        return left.nodes == right.nodes && left.main_root == right.main_root &&
               left.main_window == right.main_window &&
               left.floating_sites == right.floating_sites &&
               left.auto_hidden == right.auto_hidden && left.panels == right.panels &&
               left.next_id == right.next_id &&
               left.application_version == right.application_version &&
               left.unknown_fields == right.unknown_fields;
    }
};

// The most floating sites a layout holds (DK decision 6).
inline constexpr std::size_t dock_maximum_floating_sites = 32;
// The deepest a site's node tree may nest, its root at level one; the main
// site counts through its document area into the document tree. Every
// split adds a level, and 32 levels of halving leave a pane far below any
// minimum size, so no usable layout reaches it. The invariant check
// (DockLayout::check_invariants) refuses a deeper tree, so a transaction
// or a restore that would build one changes nothing. The walks over a
// committed tree (minimum sizes, writing, the dock site's placement) may
// recurse once per level: a few hundred bytes each, at most about 32 KiB.
inline constexpr std::size_t dock_maximum_depth = 32;
// Node and site IDs are below this value. An edit that needs a new ID once
// next_id has reached it is refused (identities_exhausted); IDs are never
// reused, so the count never wraps to the null ID.
inline constexpr std::uint32_t dock_identity_limit = 0xFFFFFFFEU;
// An index meaning "after the last panel".
inline constexpr std::uint32_t dock_append_index = 0xFFFFFFFFU;

// What an edit moves: one panel, or a whole stack (a caption drag moves the
// stack, a tab drag one panel; DK decision 26).
enum class DockSubjectKind : std::uint8_t { panel, stack };

struct GUI_FORMS_API DockSubject final {
    DockSubjectKind kind{DockSubjectKind::panel};
    DockPanelId panel{};
    DockNodeId stack{};

    [[nodiscard]] static DockSubject of_panel(DockPanelId panel_id) {
        DockSubject subject{};
        subject.kind = DockSubjectKind::panel;
        subject.panel = std::move(panel_id);
        return subject;
    }
    [[nodiscard]] static DockSubject of_stack(DockNodeId stack_id) {
        DockSubject subject{};
        subject.kind = DockSubjectKind::stack;
        subject.stack = stack_id;
        return subject;
    }
};

enum class DockEditKind : std::uint8_t {
    dock_into,
    dock_beside,
    dock_at_edge,
    float_subject,
    auto_hide,
    unpin,
    hide,
    show,
    close,
    move,
    select,
    set_ratio,
    set_floating_frame,
};

// One primitive edit. Build edits with the named factories; each sets the
// fields its kind reads and leaves the rest at their defaults.
struct GUI_FORMS_API DockEdit final {
    DockEditKind kind{DockEditKind::show};
    DockSubject subject{};
    // dock_into: a stack or a document area. dock_beside: any node of a
    // main or floating site. set_ratio: a split.
    DockNodeId target{};
    // set_floating_frame: the floating site.
    DockSiteId site{};
    // dock_beside and dock_at_edge: the side the new item takes; auto_hide:
    // the window edge.
    DockSide side{DockSide::left};
    // dock_into and move: the position in the resulting stack; an index past
    // the end appends.
    std::uint32_t index{dock_append_index};
    // dock_beside and dock_at_edge: the new item's share; set_ratio: the
    // split's first child's share. Always in (0, 1).
    double ratio{0.25};
    // float_subject and set_floating_frame: screen logical units.
    Rect frame{};
    std::string monitor_id{};
    // auto_hide: the flyout's extent in logical units.
    double extent{240.0};

    [[nodiscard]] static DockEdit dock_into(DockSubject subject, DockNodeId target,
                                            std::uint32_t index = dock_append_index);
    [[nodiscard]] static DockEdit dock_beside(DockSubject subject, DockNodeId target,
                                              DockSide side, double ratio = 0.5);
    // Against the main site's outer edge: a global target (DK decision 25).
    [[nodiscard]] static DockEdit dock_at_edge(DockSubject subject, DockSide side,
                                               double ratio = 0.25);
    [[nodiscard]] static DockEdit float_subject(DockSubject subject, Rect frame,
                                                std::string monitor_id);
    // Pins a root-level item out to edge (DK decision 28): a stack, or a
    // panel standing alone in its stack.
    [[nodiscard]] static DockEdit auto_hide(DockSubject subject, DockSide edge,
                                            double extent = 240.0);
    // Returns an auto-hidden stack to where it stood.
    [[nodiscard]] static DockEdit unpin(DockNodeId stack);
    [[nodiscard]] static DockEdit hide(DockPanelId panel);
    [[nodiscard]] static DockEdit show(DockPanelId panel);
    [[nodiscard]] static DockEdit close(DockPanelId panel);
    // Moves a panel to index within its own stack.
    [[nodiscard]] static DockEdit move(DockPanelId panel, std::uint32_t index);
    [[nodiscard]] static DockEdit select(DockPanelId panel);
    [[nodiscard]] static DockEdit set_ratio(DockNodeId split, double ratio);
    [[nodiscard]] static DockEdit set_floating_frame(DockSiteId site, Rect frame,
                                                     std::string monitor_id);
};

// program: the application's own edits; user: edits from the user's
// gestures, which a workspace lock refuses (all but select, set_ratio and
// set_floating_frame).
enum class DockEditOrigin : std::uint8_t { program, user };

// Primitive edits applied together: validated first, then applied all or
// nothing, publishing one change (DOCKING_STUDY.md section 5.3).
struct GUI_FORMS_API DockTransaction final {
    DockEditOrigin origin{DockEditOrigin::program};
    std::vector<DockEdit> edits{};
};

enum class DockRefusalLayer : std::uint8_t { none, model, panel, group, site };

enum class DockRefusalReason : std::uint8_t {
    none,
    // Model: identities, structure and limits.
    unknown_panel,
    unknown_node,
    unknown_site,
    not_a_stack,
    not_a_split,
    not_auto_hidden,
    target_is_subject,
    invalid_ratio,
    invalid_frame,
    invalid_extent,
    floating_site_limit,
    identities_exhausted,
    not_root_level,
    not_in_main_site,
    already_placed,
    not_placed,
    invariant_violated,
    // Panel layer.
    placement_not_allowed,
    tabbing_not_allowed,
    kind_mismatch,
    // Group layer.
    group_not_admitted,
    // Site layer.
    site_refuses_kind,
    site_refuses_group,
    user_docking_locked,
};

struct GUI_FORMS_API DockRefusal final {
    DockRefusalReason reason{DockRefusalReason::none};
    DockRefusalLayer layer{DockRefusalLayer::none};
    // The refused edit's position in the transaction.
    std::size_t edit_index{};
    // The panel the refusal concerns, when there is one.
    DockPanelId panel{};
};

struct GUI_FORMS_API DockTransactionResult final {
    bool applied{false};
    DockRefusal refusal{};
    // The layout's generation after the transaction.
    std::uint64_t generation{};
    // The elements whose kept unknown fields (DockLayoutState::unknown_fields)
    // the transaction dropped because it removed the element (a node pruned,
    // a panel hidden, a memory replaced by none); empty almost always.
    std::vector<DockUnknownOwner> dropped_unknown_fields{};

    [[nodiscard]] explicit operator bool() const noexcept { return applied; }
};

// Published once per applied transaction, after the model is committed.
struct GUI_FORMS_API DockLayoutChange final {
    std::uint64_t generation{};
    DockEditOrigin origin{DockEditOrigin::program};
    std::size_t edit_count{};
    // True for the change published by a restore.
    bool restored{false};
    // True when every edit was set_floating_frame: a floating window moved
    // or resized and no site's tree changed (phase D4), so realizing
    // controls need not rebuild.
    bool frames_only{false};
};

// One node's rectangle from DockLayout::arrange.
struct GUI_FORMS_API DockArrangedNode final {
    DockNodeId node{};
    Rect bounds{};
    // False when this node is a split too small for both children's minimums.
    bool minimums_met{true};
};

// A panel in a stack, as the realizing controls need it: lazy creation
// builds only panels that are selected in a docked or floating stack, and
// the rest on first show (DK decision 34).
struct GUI_FORMS_API DockVisiblePanel final {
    DockPanelId panel{};
    DockNodeId stack{};
    DockPlacementKind placement{DockPlacementKind::docked};
    bool selected{false};
};

struct GUI_FORMS_API DockPanelLocation final {
    DockPlacementKind placement{DockPlacementKind::absent};
    // The stack holding the panel, when it is docked, floating or
    // auto-hidden, with its index there.
    DockNodeId stack{};
    std::uint32_t index{};
    // The floating site, when it is floating.
    DockSiteId site{};
};

struct GUI_FORMS_API DockInvariantResult final {
    bool valid{true};
    // The first broken rule, in words, when not valid.
    std::string problem{};

    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

} // namespace gui_forms
