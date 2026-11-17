#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/connected_controls/types/connected_control_types.hpp"
#include "gui_forms/controls/button_base.hpp"

#include <memory>
#include <optional>
#include <span>

#include <stxlib/optional.hpp>

namespace gui_forms {

// Applies an explicit ordered connection to separate retained controls. All
// validation happens before the first mutation. The members must be distinct,
// live ButtonBase controls under one retained parent; existing connections on
// the other axis are rejected instead of being silently rewritten.
GUI_FORMS_API void connect_button_group(
    std::span<const std::shared_ptr<ButtonBase>> controls,
    ConnectedControlAxis axis);
GUI_FORMS_API void disconnect_button_group(
    std::span<const std::shared_ptr<ButtonBase>> controls);

// Renderer-neutral material composition used by ButtonBase and available to
// custom controls which need the same explicit connected physical grammar.
GUI_FORMS_API void paint_connected_surface_material(
    Painter& painter, Rect bounds, const SurfaceMaterial& material,
    stxlib::Optional<ConnectedControlTopology> topology);
[[nodiscard]] GUI_FORMS_API Insets connected_surface_visual_outsets(
    const SurfaceMaterial& material,
    stxlib::Optional<ConnectedControlTopology> topology) noexcept;
// As above for a material drawn over a box of `box`: its reach includes the
// material's own transform (surface_material_visual_outsets with a size).
[[nodiscard]] GUI_FORMS_API Insets connected_surface_visual_outsets(
    const SurfaceMaterial& material,
    stxlib::Optional<ConnectedControlTopology> topology, Size box) noexcept;

// The standard forms, kept for one stage (D15 Q3); a plain topology is
// refused: pass stxlib::Optional<ConnectedControlTopology>::of(topology).
[[deprecated("pass stxlib::Optional<ConnectedControlTopology>")]] inline void paint_connected_surface_material(
    Painter& painter, Rect bounds, const SurfaceMaterial& material,
    std::optional<ConnectedControlTopology> topology) {
    paint_connected_surface_material(painter, bounds, material, connected_topology_from_standard(topology));
}
void paint_connected_surface_material(Painter& painter, Rect bounds, const SurfaceMaterial& material,
                                      ConnectedControlTopology topology) = delete;
[[deprecated("pass stxlib::Optional<ConnectedControlTopology>")]] [[nodiscard]] inline Insets
connected_surface_visual_outsets(const SurfaceMaterial& material,
                                 std::optional<ConnectedControlTopology> topology) noexcept {
    const Insets result = connected_surface_visual_outsets(material, connected_topology_from_standard(topology));
    return result;
}
Insets connected_surface_visual_outsets(const SurfaceMaterial& material, ConnectedControlTopology topology) = delete;
[[deprecated("pass stxlib::Optional<ConnectedControlTopology>")]] [[nodiscard]] inline Insets
connected_surface_visual_outsets(const SurfaceMaterial& material, std::optional<ConnectedControlTopology> topology,
                                 Size box) noexcept {
    const Insets result =
        connected_surface_visual_outsets(material, connected_topology_from_standard(topology), box);
    return result;
}
Insets connected_surface_visual_outsets(const SurfaceMaterial& material, ConnectedControlTopology topology,
                                        Size box) = delete;

} // namespace gui_forms
