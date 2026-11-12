#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

// The drop targets of a drag in one window (stage DK phase D3; owner
// decisions DK-24 to DK-26, DK-41): renderer-free, so the geometry and the
// target tables are tested without controls.
//
// Position selects the target (D2, as settled): over a pane, its edge bands
// split it on that side and its centre tabs the dragged subject into it; a
// cross of five tiles over the hovered pane (and only that pane) shows the
// choices, and a tile under the pointer wins over the band it lies in. The
// main site's outer edges are global targets, offered throughout a drag
// (D3-1): a band along each edge and a tile at each edge's middle. There is
// no drop straight into auto-hide (D2a-1).

// A pane's five choices, in the cross's reading order.
enum class DockDropZone : std::uint8_t { top, left, center, right, bottom };
inline constexpr std::size_t dock_drop_zone_count = 5;

// The geometry, in logical units at text scale 1 (the look study's quiet
// tiles, T2-4; its zone threshold and global bands).
struct GUI_FORMS_API DockDropMetrics final {
    // A pane's zone is its nearest edge while the pointer lies within this
    // fraction of the pane from it, else the centre.
    static constexpr double side_band = 0.28;
    // The cross: tiles of this side and gap, shrunk (not below
    // `minimum_tile`) to fit a small pane with `cross_margin` to spare.
    static constexpr double tile = 24.0;
    static constexpr double tile_gap = 7.0;
    static constexpr double minimum_tile = 12.0;
    static constexpr double cross_margin = 10.0;
    // The window edges: a tile of this side inset by `edge_margin` from each
    // edge's middle, and a band of `edge_band` along each edge.
    static constexpr double edge_tile = 18.0;
    static constexpr double edge_margin = 5.0;
    static constexpr double edge_band = 8.0;
    // The share a dropped subject takes: half the pane beside it, a quarter
    // of the window along an edge.
    static constexpr double side_share = 0.5;
    static constexpr double edge_share = 0.25;
};

// A pane the pointer can hover: a stack of the main site as it is laid out
// during the drag (the dragged subject's place closed up), or the empty
// document area. `bounds` in the site's coordinates. A document stack also
// names its document area (`area`, `area_bounds`): a side the stack itself
// refuses (a tool panel beside a document) docks beside the whole area,
// as Visual Studio's outer guides do.
struct GUI_FORMS_API DockDropPane final {
    DockNodeId node{};
    Rect bounds{};
    DockNodeId area{};
    Rect area_bounds{};
};

enum class DockDropTargetKind : std::uint8_t { pane, edge };

// Whether a site offers the window's outer edges as global targets: the main
// site does (D3-1); a floating site (phase D4) offers its panes only.
enum class DockDropEdges : std::uint8_t { global, none };

// One accepted target: what a drop there does (`edit`, one DockLayout edit
// validated against the restrictions when the drag started) and the area
// the subject would take (`highlight`, in the site's coordinates). For an
// edge target, `zone` is the edge (never center) and `pane` is none.
struct GUI_FORMS_API DockDropTarget final {
    DockDropTargetKind kind{DockDropTargetKind::pane};
    DockNodeId pane{};
    DockDropZone zone{DockDropZone::center};
    Rect highlight{};
    DockEdit edit{};
};

// What the pointer is over: a window edge, or a pane's zone, or nothing in
// particular inside the site (`over_site` without a pane), or outside it.
struct GUI_FORMS_API DockDropHit final {
    bool over_site{false};
    bool edge{false};
    // The pane's index in the panes given, while over a pane.
    std::optional<std::size_t> pane{};
    DockDropZone zone{DockDropZone::center};
};

// The cross over a pane, in DockDropZone order. Every tile lies inside the
// pane; a pane too small for the smallest cross gets the tiles anyway,
// clipped by the caller.
[[nodiscard]] GUI_FORMS_API std::array<Rect, dock_drop_zone_count> dock_cross_tiles(Rect pane, double scale) noexcept;
// The edge tiles of the site, in DockDropZone order with the centre's slot
// left empty.
[[nodiscard]] GUI_FORMS_API std::array<Rect, dock_drop_zone_count> dock_edge_tiles(Rect site, double scale) noexcept;
// The area a subject dropped on `zone` of `pane` would take; an edge's when
// `edge` (a quarter of the site along that edge).
[[nodiscard]] GUI_FORMS_API Rect dock_drop_highlight(Rect bounds, DockDropZone zone, bool edge) noexcept;
// The zone of `pane` a point selects from its position alone.
[[nodiscard]] GUI_FORMS_API DockDropZone dock_zone_at(Rect pane, Point point) noexcept;

// What a point hovers: the site's edge tiles and bands first (unless
// `edges` is none), then the pane containing it (a cross tile under the
// point, else its zone).
[[nodiscard]] GUI_FORMS_API DockDropHit hit_dock_drop(Point point, Rect site, std::span<const DockDropPane> panes,
                                        double scale, DockDropEdges edges = DockDropEdges::global) noexcept;

// The edit a target makes for `subject`: dock_into a stack or the empty
// document area for the centre, dock_beside a pane for a side, dock_at_edge
// for an edge.
[[nodiscard]] GUI_FORMS_API DockEdit dock_drop_edit(const DockSubject& subject, DockDropTargetKind kind, DockNodeId pane,
                                      DockDropZone zone);

// Fills `out` (cleared first) with every target of a drag of `subject` over
// `panes` and the site's edges that `layout` accepts as a user edit
// (DockLayout::check, so the three restriction layers and the user lock
// decide): each pane's zones in DockDropZone order, panes in the order
// given (the keyboard's Tab order), then, unless `edges` is none, the edges
// top, left, right, bottom. The centre of `own_stack` (where a dragged panel came from) is
// left out: dropping a panel back into its own stack changes nothing. A side
// a document stack refuses is tried beside its document area (DockDropPane).
GUI_FORMS_API void plan_dock_drop_targets(const DockLayout& layout, const DockSubject& subject, DockNodeId own_stack,
                            Rect site, std::span<const DockDropPane> panes, std::vector<DockDropTarget>& out,
                            DockDropEdges edges = DockDropEdges::global);

// The planned target a hit selects, as an index into `targets`, or none (a
// refused zone, a gap inside the site, or outside it).
[[nodiscard]] GUI_FORMS_API std::optional<std::size_t> find_dock_drop_target(std::span<const DockDropTarget> targets,
                                                               std::span<const DockDropPane> panes,
                                                               const DockDropHit& hit) noexcept;

} // namespace gui_forms
