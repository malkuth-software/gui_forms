#pragma once

#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The framework's layout format version written by write_xml. Older
// versions are upgraded step by step on restore (version 1 spelled stacks
// as <stack kind="tool|document">); newer ones are read best-effort.
inline constexpr std::uint32_t dock_layout_format_version = 2;

// The XML limits layouts are written and read with: XmlLimits' defaults
// with a depth of 256, since every split nests one level.
[[nodiscard]] XmlLimits dock_layout_xml_limits() noexcept;

// A monitor as restore needs it; convert from HostMonitor (id, work_area,
// primary). Frames are in screen logical units.
struct DockMonitor final {
    std::string id{};
    Rect work_area{};
    bool primary{false};
};

// How much of a restored window must lie inside a work area so its caption
// can be reached: the caption's full height and this much of its width
// (DOCKING_STUDY.md section 5.8, step 4).
struct DockCaptionReach final {
    double caption_height{32.0};
    double visible_width{64.0};
};

enum class DockMonitorMatch : std::uint8_t {
    // No monitors were given; the frame is unchanged.
    none,
    same_id,
    most_overlap,
    primary,
};

struct DockMonitorPlacement final {
    Rect frame{};
    std::string monitor_id{};
    DockMonitorMatch match{DockMonitorMatch::none};
    bool clamped{false};
};

// The monitor fallback of DK decision 34: the monitor with the saved ID if it
// is connected, else the one whose work area overlaps the frame most, else
// the primary (else the first). The frame keeps its size and is moved, if
// needed, so the caption is reachable inside that monitor's work area.
[[nodiscard]] DockMonitorPlacement place_on_monitor(Rect frame, std::string_view monitor_id,
                                                    std::span<const DockMonitor> monitors,
                                                    const DockCaptionReach& reach = {});

// Renames a panel everywhere in a state: stacks and its record. For an
// application's upgrade hook (renamed panels). Returns false, changing
// nothing, when from is not in the state or to already is.
bool rename_dock_panel(DockLayoutState& state, const DockPanelId& from, const DockPanelId& to);

enum class DockUpgradeDecision : std::uint8_t { accept, reject };

// Called during restore when the saved application layout version differs
// from DockRestoreOptions::application_version, with the parsed state before
// panel identities are resolved. It may rename panels (rename_dock_panel),
// move or drop them, or reject the layout (the layout then keeps what it
// held). It must not retain the state. Runs on the caller's thread, inside
// restore_xml; it must not call into the DockLayout being restored.
struct DockLayoutUpgradeHook final {
    DockUpgradeDecision (*upgrade)(void* context, std::uint32_t saved_version,
                                   DockLayoutState& state){nullptr};
    void* context{nullptr};
};

struct DockRestoreOptions final {
    // The application's current layout version.
    std::uint32_t application_version{0};
    DockLayoutUpgradeHook upgrade{};
    // The monitors now connected; empty skips the monitor fallback.
    std::span<const DockMonitor> monitors{};
    DockCaptionReach reach{};
    XmlLimits limits{dock_layout_xml_limits()};
};

enum class DockRestoreDiagnosticKind : std::uint8_t {
    unreadable_xml,
    not_a_layout,
    unsupported_format_version,
    older_format_migrated,
    newer_format_read,
    unknown_element,
    unknown_attribute,
    invalid_value,
    duplicate_panel,
    unknown_panel_kept_as_placeholder,
    panel_moved_to_placeholder,
    floating_site_limit,
    application_version_upgraded,
    application_rejected,
    monitor_fallback,
    frame_clamped,
    structure_invalid,
    // Unknown fields kept from the file whose element the restore itself
    // removed (an unregistered panel made a placeholder, a stack left
    // empty); they are not written back.
    unknown_fields_dropped,
};

struct DockRestoreDiagnostic final {
    DockRestoreDiagnosticKind kind{DockRestoreDiagnosticKind::unreadable_xml};
    // What was found and where, in words (an element path, a panel ID).
    std::string detail{};
};

enum class DockRestoreStatus : std::uint8_t {
    // The saved layout is now the layout (possibly with diagnostics).
    restored,
    // The record was unreadable, invalid or rejected; the layout keeps what
    // it held, which at startup is the program's default layout.
    kept_current,
};

struct DockRestoreResult final {
    DockRestoreStatus status{DockRestoreStatus::kept_current};
    std::vector<DockRestoreDiagnostic> diagnostics{};
};

struct DockLayoutWriteResult final {
    XmlError error{XmlError::none};
    std::string text{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == XmlError::none; }
};

// The renderer-free docking layout model (DK decisions 3, 18 to 23, 28, 31 to
// 34): one main site whose tree always holds exactly one document area,
// floating sites, auto-hidden stacks on the main site's edges, and a record
// (placeholder) for every panel it has held. Every change goes through a
// transaction of primitive edits, checked against the three restriction
// layers (panel, dock group, site) and applied all or nothing. The model
// never creates panels: it reports which are visible and selected.
//
// Execution: one thread, the owner's; nothing is synchronized.
class DockLayout final {
public:
    // A main site holding only an empty document area.
    DockLayout();
    DockLayout(const DockLayout&) = delete;
    DockLayout& operator=(const DockLayout&) = delete;

    // Registers a panel's descriptor; registering does not place it (show
    // does). Returns false, changing nothing, when the ID is empty or
    // already registered.
    bool register_panel(const DockPanelDescriptor& descriptor);
    // Replaces a registered panel's descriptor (its restrictions, defaults
    // and close button); where it stands is unchanged, and later edits obey
    // the new restrictions. False for an unregistered identity.
    bool update_panel(const DockPanelDescriptor& descriptor);
    [[nodiscard]] const DockPanelDescriptor* find_descriptor(const DockPanelId& panel) const noexcept;
    // Replaces the group descriptor with the same ID, or adds it.
    void set_group(const DockGroupDescriptor& group);
    void set_main_site_restrictions(const DockSiteRestrictions& restrictions);
    void set_floating_site_restrictions(const DockSiteRestrictions& restrictions);
    // While locked, user-origin edits that move panels are refused.
    void set_user_docking_locked(bool locked) noexcept;
    // Recorded for saving; not a layout change and publishes nothing.
    void set_main_window_frame(const DockWindowFrame& frame);
    void set_application_version(std::uint32_t version) noexcept;

    // Validates the whole transaction on a copy and, when every edit is
    // accepted, commits it, increments the generation and publishes one
    // change. A refused transaction changes nothing and publishes nothing.
    // An empty transaction is accepted and publishes nothing.
    DockTransactionResult apply(const DockTransaction& transaction);
    // The same validation without committing: whether apply would accept.
    [[nodiscard]] DockTransactionResult check(const DockTransaction& transaction) const;

    [[nodiscard]] const DockLayoutState& state() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] const DockNode* find_node(DockNodeId node) const noexcept;
    [[nodiscard]] const DockFloatingSite* find_floating_site(DockSiteId site) const noexcept;
    [[nodiscard]] DockPanelLocation locate(const DockPanelId& panel) const;
    // The document area of the main site.
    [[nodiscard]] DockNodeId document_area() const noexcept;
    // The root-level item whose pin governs a panel (DK decision 28): the
    // panel's stack when it is a tool stack of the main site outside the
    // document area, or auto-hidden; none otherwise.
    [[nodiscard]] std::optional<DockNodeId> pin_owner(const DockPanelId& panel) const;
    // Fills out (cleared first) with every item that carries a pin: the main
    // site's tool stacks in tree order, then the auto-hidden stacks.
    void collect_pinnable_items(std::vector<DockNodeId>& out) const;
    // Fills out (cleared first) with every panel in a stack, in tree order:
    // the main site, then floating sites, then auto-hidden stacks.
    void collect_visible_panels(std::vector<DockVisiblePanel>& out) const;
    [[nodiscard]] DockInvariantResult check_invariants() const;

    // The smallest size of a node's subtree, from its panels' descriptor
    // minimums: a split adds its children along its axis plus seam, and
    // takes the larger across it. An unregistered panel counts as zero.
    [[nodiscard]] Size minimum_size(DockNodeId node, double seam) const;
    // Lays out a subtree inside bounds with solve_dock_split, filling out
    // (cleared first) with every node's rectangle in tree order. A seam of
    // the given thickness separates each split's children.
    void arrange(DockNodeId root, Rect bounds, double seam,
                 std::vector<DockArrangedNode>& out) const;

    // Writes the layout in the current format (dock_layout_format_version);
    // the same layout always gives the same bytes.
    [[nodiscard]] DockLayoutWriteResult write_xml() const;
    // Reads and validates a whole saved layout before changing anything,
    // then replaces the layout with it as one transaction (one generation,
    // one change). Registered descriptors and restrictions are kept.
    // Unknown attributes and elements are kept and written back by
    // write_xml (DockLayoutState::unknown_fields; DK decision 48).
    DockRestoreResult restore_xml(std::string_view xml, const DockRestoreOptions& options);

    [[nodiscard]] Event<const DockLayoutChange&>& changed() noexcept;

private:
    [[nodiscard]] DockTransactionResult run(const DockTransaction& transaction,
                                            DockLayoutState& scratch) const;
    [[nodiscard]] std::optional<DockPanelId> side_refusal(const DockLayoutState& after) const;
    void commit(DockLayoutState&& replacement, DockEditOrigin origin, std::size_t edit_count,
                bool restored, bool frames_only);

    DockLayoutState state_{};
    std::vector<DockPanelDescriptor> descriptors_{};
    std::vector<DockGroupDescriptor> groups_{};
    DockSiteRestrictions main_site_restrictions_{};
    DockSiteRestrictions floating_site_restrictions_{};
    bool user_docking_locked_{false};
    std::uint64_t generation_{0};
    Event<const DockLayoutChange&> changed_{};
};

} // namespace gui_forms
