#include "gui_forms/docking/dock_drop_targets/dock_drop_targets.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {

constexpr std::array<DockDropZone, dock_drop_zone_count> cross_order{
    DockDropZone::top, DockDropZone::left, DockDropZone::center, DockDropZone::right, DockDropZone::bottom};
constexpr std::array<DockDropZone, 4> edge_order{DockDropZone::top, DockDropZone::left, DockDropZone::right,
                                                 DockDropZone::bottom};

[[nodiscard]] std::size_t zone_index(DockDropZone zone) noexcept {
    const std::size_t index = static_cast<std::size_t>(zone);
    return index;
}

[[nodiscard]] DockSide side_of(DockDropZone zone) noexcept {
    switch (zone) {
    case DockDropZone::top: return DockSide::top;
    case DockDropZone::left: return DockSide::left;
    case DockDropZone::right: return DockSide::right;
    case DockDropZone::bottom: return DockSide::bottom;
    case DockDropZone::center: return DockSide::left;
    }
    return DockSide::left;
}

// A tile with a one-unit slack round it, so its rim still counts.
[[nodiscard]] bool within_tile(Rect tile, Point point) noexcept {
    const Rect slack{tile.x - 1.0, tile.y - 1.0, tile.width + 2.0, tile.height + 2.0};
    const bool inside = !tile.empty() && slack.contains(point);
    return inside;
}

} // namespace

std::array<Rect, dock_drop_zone_count> dock_cross_tiles(Rect pane, double scale) noexcept {
    double tile = DockDropMetrics::tile * scale;
    double gap = DockDropMetrics::tile_gap * scale;
    const double footprint = 3.0 * tile + 2.0 * gap + DockDropMetrics::cross_margin * scale;
    const double fit = std::min(pane.width, pane.height) / footprint;
    if (fit < 1.0) {
        tile = std::max(DockDropMetrics::minimum_tile * scale, std::floor(tile * fit));
        gap = std::max(1.0, std::floor(gap * fit));
    }
    const double half = tile * 0.5;
    const double step = tile + gap;
    const double centre_x = std::floor(pane.x + pane.width * 0.5);
    const double centre_y = std::floor(pane.y + pane.height * 0.5);
    std::array<Rect, dock_drop_zone_count> tiles{};
    tiles[zone_index(DockDropZone::center)] = {centre_x - half, centre_y - half, tile, tile};
    tiles[zone_index(DockDropZone::top)] = {centre_x - half, centre_y - half - step, tile, tile};
    tiles[zone_index(DockDropZone::bottom)] = {centre_x - half, centre_y - half + step, tile, tile};
    tiles[zone_index(DockDropZone::left)] = {centre_x - half - step, centre_y - half, tile, tile};
    tiles[zone_index(DockDropZone::right)] = {centre_x - half + step, centre_y - half, tile, tile};
    return tiles;
}

std::array<Rect, dock_drop_zone_count> dock_edge_tiles(Rect site, double scale) noexcept {
    const double tile = DockDropMetrics::edge_tile * scale;
    const double margin = DockDropMetrics::edge_margin * scale;
    const double middle_x = std::floor(site.x + (site.width - tile) * 0.5);
    const double middle_y = std::floor(site.y + (site.height - tile) * 0.5);
    std::array<Rect, dock_drop_zone_count> tiles{};
    tiles[zone_index(DockDropZone::top)] = {middle_x, site.y + margin, tile, tile};
    tiles[zone_index(DockDropZone::bottom)] = {middle_x, site.y + site.height - margin - tile, tile, tile};
    tiles[zone_index(DockDropZone::left)] = {site.x + margin, middle_y, tile, tile};
    tiles[zone_index(DockDropZone::right)] = {site.x + site.width - margin - tile, middle_y, tile, tile};
    return tiles;
}

Rect dock_drop_highlight(Rect bounds, DockDropZone zone, bool edge) noexcept {
    const double share = edge ? DockDropMetrics::edge_share : DockDropMetrics::side_share;
    const double across_width = std::floor(bounds.width * share);
    const double across_height = std::floor(bounds.height * share);
    Rect area = bounds;
    switch (zone) {
    case DockDropZone::top:
        area.height = across_height;
        return area;
    case DockDropZone::bottom:
        area.y = bounds.y + bounds.height - across_height;
        area.height = across_height;
        return area;
    case DockDropZone::left:
        area.width = across_width;
        return area;
    case DockDropZone::right:
        area.x = bounds.x + bounds.width - across_width;
        area.width = across_width;
        return area;
    case DockDropZone::center:
        // A drop into the stack highlights all of it.
        return bounds;
    }
    // A zone outside the enumeration highlights the whole target.
    return bounds;
}

DockDropZone dock_zone_at(Rect pane, Point point) noexcept {
    if (pane.width <= 0.0 || pane.height <= 0.0) return DockDropZone::center;
    const double across = (point.x - pane.x) / pane.width;
    const double down = (point.y - pane.y) / pane.height;
    // The nearest edge, as a fraction of the pane; ties go to the first in
    // left, right, top, bottom order.
    DockDropZone nearest = DockDropZone::left;
    double distance = across;
    if (1.0 - across < distance) {
        nearest = DockDropZone::right;
        distance = 1.0 - across;
    }
    if (down < distance) {
        nearest = DockDropZone::top;
        distance = down;
    }
    if (1.0 - down < distance) {
        nearest = DockDropZone::bottom;
        distance = 1.0 - down;
    }
    if (distance > DockDropMetrics::side_band) return DockDropZone::center;
    return nearest;
}

DockDropHit hit_dock_drop(Point point, Rect site, std::span<const DockDropPane> panes, double scale,
                          DockDropEdges edges) noexcept {
    DockDropHit hit{};
    if (!site.contains(point)) return hit;
    hit.over_site = true;
    if (edges == DockDropEdges::global) {
        const std::array<Rect, dock_drop_zone_count> edge_tiles = dock_edge_tiles(site, scale);
        for (const DockDropZone zone : edge_order) {
            if (!within_tile(edge_tiles[zone_index(zone)], point)) continue;
            hit.edge = true;
            hit.zone = zone;
            return hit;
        }
        // The bands along the edges; a corner belongs to the left or right
        // band.
        const double band = DockDropMetrics::edge_band * scale;
        std::optional<DockDropZone> banded{};
        if (point.y >= site.y + site.height - band) banded = DockDropZone::bottom;
        if (point.y < site.y + band) banded = DockDropZone::top;
        if (point.x >= site.x + site.width - band) banded = DockDropZone::right;
        if (point.x < site.x + band) banded = DockDropZone::left;
        if (banded) {
            hit.edge = true;
            hit.zone = *banded;
            return hit;
        }
    }
    for (std::size_t index = 0; index < panes.size(); ++index) {
        const Rect bounds = panes[index].bounds;
        if (!bounds.contains(point)) continue;
        hit.pane.set(index);
        hit.zone = dock_zone_at(bounds, point);
        const std::array<Rect, dock_drop_zone_count> tiles = dock_cross_tiles(bounds, scale);
        for (const DockDropZone zone : cross_order) {
            if (within_tile(tiles[zone_index(zone)], point)) hit.zone = zone;
        }
        return hit;
    }
    return hit;
}

DockEdit dock_drop_edit(const DockSubject& subject, DockDropTargetKind kind, DockNodeId pane, DockDropZone zone) {
    if (kind == DockDropTargetKind::edge) {
        const DockEdit edit = DockEdit::dock_at_edge(subject, side_of(zone), DockDropMetrics::edge_share);
        return edit;
    }
    if (zone == DockDropZone::center) {
        const DockEdit edit = DockEdit::dock_into(subject, pane);
        return edit;
    }
    const DockEdit edit = DockEdit::dock_beside(subject, pane, side_of(zone), DockDropMetrics::side_share);
    return edit;
}

void plan_dock_drop_targets(const DockLayout& layout, const DockSubject& subject, DockNodeId own_stack,
                            Rect site, std::span<const DockDropPane> panes, std::vector<DockDropTarget>& out,
                            DockDropEdges edges) {
    out.clear();
    DockTransaction trial{};
    trial.origin = DockEditOrigin::user;
    trial.edits.resize(1U);
    for (const DockDropPane& pane : panes) {
        for (const DockDropZone zone : cross_order) {
            if (zone == DockDropZone::center && pane.node == own_stack) continue;
            trial.edits[0] = dock_drop_edit(subject, DockDropTargetKind::pane, pane.node, zone);
            Rect area = pane.bounds;
            bool accepted = layout.check(trial).applied;
            const bool beside_area = zone != DockDropZone::center && !(pane.area == DockNodeId{});
            if (!accepted && beside_area) {
                trial.edits[0] = dock_drop_edit(subject, DockDropTargetKind::pane, pane.area, zone);
                accepted = layout.check(trial).applied;
                area = pane.area_bounds;
            }
            if (!accepted) continue;
            DockDropTarget target{};
            target.kind = DockDropTargetKind::pane;
            target.pane = pane.node;
            target.zone = zone;
            target.highlight = dock_drop_highlight(area, zone, false);
            target.edit = trial.edits[0];
            out.push_back(target);
        }
    }
    if (edges == DockDropEdges::none) return;
    for (const DockDropZone zone : edge_order) {
        trial.edits[0] = dock_drop_edit(subject, DockDropTargetKind::edge, DockNodeId{}, zone);
        if (!layout.check(trial).applied) continue;
        DockDropTarget target{};
        target.kind = DockDropTargetKind::edge;
        target.zone = zone;
        target.highlight = dock_drop_highlight(site, zone, true);
        target.edit = trial.edits[0];
        out.push_back(target);
    }
}

stxlib::Optional<std::size_t> find_dock_drop_target(std::span<const DockDropTarget> targets,
                                                 std::span<const DockDropPane> panes,
                                                 const DockDropHit& hit) noexcept {
    if (!hit.over_site) return stxlib::Optional<std::size_t>::none();
    if (!hit.edge && hit.pane.absent()) return stxlib::Optional<std::size_t>::none();
    // An edge hit looks only for the zone's edge target; a pane hit only for
    // the zone's target on that pane.
    if (hit.edge) {
        for (std::size_t index = 0; index < targets.size(); ++index) {
            const DockDropTarget& target = targets[index];
            if (target.zone == hit.zone && target.kind == DockDropTargetKind::edge) return stxlib::Optional<std::size_t>::of(index);
        }
        return stxlib::Optional<std::size_t>::none();
    }
    if ((*hit.pane.value()) >= panes.size()) return stxlib::Optional<std::size_t>::none();
    const DockNodeId pane = panes[(*hit.pane.value())].node;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const DockDropTarget& target = targets[index];
        if (target.zone == hit.zone && target.kind == DockDropTargetKind::pane && target.pane == pane) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

} // namespace gui_forms
