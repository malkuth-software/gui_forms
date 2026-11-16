#include "gui_forms/connected_controls.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace gui_forms {
namespace {

bool valid_axis(const ConnectedControlAxis axis) noexcept {
    return axis == ConnectedControlAxis::horizontal ||
           axis == ConnectedControlAxis::vertical;
}

std::optional<MaterialBorder> leading_border(
    const SurfaceMaterial& material,
    const ConnectedControlAxis axis) noexcept {
    if (material.border) return material.border;
    return axis == ConnectedControlAxis::horizontal
        ? material.border_edges.left : material.border_edges.top;
}

void validate_group(
    const std::span<const std::shared_ptr<ButtonBase>> controls,
    const std::optional<ConnectedControlAxis> axis) {
    if (controls.size() < 2U || controls.size() > 256U) {
        throw std::invalid_argument(
            "connected button group requires between 2 and 256 members");
    }
    if (axis && !valid_axis(*axis)) {
        throw std::invalid_argument("connected button group axis is invalid");
    }

    std::unordered_set<const ButtonBase*> distinct;
    Control::Ptr parent;
    Window* window = nullptr;
    for (const std::shared_ptr<ButtonBase>& control : controls) {
        if (!control || !(*control).is_alive()) {
            throw std::invalid_argument(
                "connected button group requires live non-null members");
        }
        if (!distinct.insert(control.get()).second) {
            throw std::invalid_argument(
                "connected button group contains a duplicate member");
        }
        const Control::Ptr candidate_parent = (*control).parent();
        if (!candidate_parent) {
            throw std::invalid_argument(
                "connected button group members require one retained parent");
        }
        if (!parent) {
            parent = candidate_parent;
            window = (*control).attached_window();
        } else if (candidate_parent != parent ||
                   (*control).attached_window() != window) {
            throw std::invalid_argument(
                "connected button group members must share one retained parent");
        }
        if (axis && (*control).connection_topology() &&
            (*(*control).connection_topology()).axis != *axis) {
            throw std::invalid_argument(
                "connected button group cannot mix connection axes");
        }
    }
}

} // namespace

bool valid_connected_control_topology(
    const ConnectedControlTopology topology) noexcept {
    return valid_axis(topology.axis) && topology.count >= 2U &&
           topology.count <= 256U && topology.index < topology.count;
}

ConnectedControlSegment connected_control_segment(
    const std::optional<ConnectedControlTopology> topology) noexcept {
    if (!topology || !valid_connected_control_topology(*topology)) {
        return ConnectedControlSegment::standalone;
    }
    if ((*topology).index == 0U) return ConnectedControlSegment::leading;
    if ((*topology).index + 1U == (*topology).count) {
        return ConnectedControlSegment::trailing;
    }
    return ConnectedControlSegment::middle;
}

ConnectedControlVisualGeometry resolve_connected_control_visual_geometry(
    const Rect bounds, const SurfaceMaterial& material,
    const std::optional<ConnectedControlTopology> topology) noexcept {
    ConnectedControlVisualGeometry result;
    result.segment = connected_control_segment(topology);
    result.clip_bounds = bounds;
    result.paint_bounds = bounds;
    if (bounds.empty() || result.segment == ConnectedControlSegment::standalone) {
        return result;
    }

    const double extension = std::max(1.0, material.corner_radius);
    const bool has_leading = result.segment != ConnectedControlSegment::leading;
    const bool has_trailing = result.segment != ConnectedControlSegment::trailing;
    if ((*topology).axis == ConnectedControlAxis::horizontal) {
        if (has_leading) {
            result.paint_bounds.x -= extension;
            result.paint_bounds.width += extension;
        }
        if (has_trailing) result.paint_bounds.width += extension;
    } else {
        if (has_leading) {
            result.paint_bounds.y -= extension;
            result.paint_bounds.height += extension;
        }
        if (has_trailing) result.paint_bounds.height += extension;
    }

    if (has_leading) {
        const std::optional<MaterialBorder> border =
            leading_border(material, (*topology).axis);
        if (border && std::isfinite((*border).width) && (*border).width > 0.0) {
            ConnectedControlSeam seam;
            seam.color = (*border).color;
            seam.width = (*border).width;
            if ((*topology).axis == ConnectedControlAxis::horizontal) {
                const double x = bounds.x + seam.width * 0.5;
                seam.from = {x, bounds.y};
                seam.to = {x, bounds.bottom()};
            } else {
                const double y = bounds.y + seam.width * 0.5;
                seam.from = {bounds.x, y};
                seam.to = {bounds.right(), y};
            }
            result.leading_seam = seam;
        }
    }
    return result;
}

void connect_button_group(
    const std::span<const std::shared_ptr<ButtonBase>> controls,
    const ConnectedControlAxis axis) {
    validate_group(controls, axis);
    Window* window = (*controls.front()).attached_window();
    std::optional<UpdateScope> update;
    if (window != nullptr) update.emplace((*window).begin_update());
    for (std::size_t index = 0U; index < controls.size(); ++index) {
        (*controls[index]).set_connection_topology(
            ConnectedControlTopology{axis, index, controls.size()});
    }
}

void disconnect_button_group(
    const std::span<const std::shared_ptr<ButtonBase>> controls) {
    validate_group(controls, std::nullopt);
    Window* window = (*controls.front()).attached_window();
    std::optional<UpdateScope> update;
    if (window != nullptr) update.emplace((*window).begin_update());
    for (const std::shared_ptr<ButtonBase>& control : controls) {
        (*control).set_connection_topology(std::nullopt);
    }
}

void paint_connected_surface_material(
    Painter& painter, const Rect bounds, const SurfaceMaterial& material,
    const std::optional<ConnectedControlTopology> topology) {
    const ConnectedControlVisualGeometry geometry =
        resolve_connected_control_visual_geometry(bounds, material, topology);
    if (geometry.segment == ConnectedControlSegment::standalone) {
        paint_surface_material(painter, bounds, material);
        return;
    }
    painter.save();
    painter.clip_rect(geometry.clip_bounds);
    paint_surface_material(painter, geometry.paint_bounds, material);
    painter.restore();
    if (geometry.leading_seam) {
        painter.draw_line((*geometry.leading_seam).from,
                          (*geometry.leading_seam).to,
                          (*geometry.leading_seam).color,
                          (*geometry.leading_seam).width);
    }
}

namespace {

// `reach` with the sides joined to a neighbour cleared: a joined edge
// draws nothing past itself.
[[nodiscard]] Insets joined_outsets(Insets reach, const std::optional<ConnectedControlTopology> topology) noexcept {
    Insets result = reach;
    const ConnectedControlSegment segment = connected_control_segment(topology);
    if (segment == ConnectedControlSegment::standalone) return result;
    const bool joined_leading = segment != ConnectedControlSegment::leading;
    const bool joined_trailing = segment != ConnectedControlSegment::trailing;
    if ((*topology).axis == ConnectedControlAxis::horizontal) {
        if (joined_leading) result.left = 0.0;
        if (joined_trailing) result.right = 0.0;
    } else {
        if (joined_leading) result.top = 0.0;
        if (joined_trailing) result.bottom = 0.0;
    }
    return result;
}

} // namespace

Insets connected_surface_visual_outsets(
    const SurfaceMaterial& material,
    const std::optional<ConnectedControlTopology> topology) noexcept {
    const Insets result = joined_outsets(surface_material_visual_outsets(material), topology);
    return result;
}

Insets connected_surface_visual_outsets(const SurfaceMaterial& material,
                                        const std::optional<ConnectedControlTopology> topology,
                                        Size box) noexcept {
    const Insets result = joined_outsets(surface_material_visual_outsets(material, box), topology);
    return result;
}

} // namespace gui_forms
