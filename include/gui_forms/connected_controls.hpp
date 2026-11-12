#pragma once

#include "gui_forms/connected_controls/types/connected_control_types.hpp"
#include "gui_forms/controls/button_base.hpp"

#include <memory>
#include <span>

namespace gui_forms {

// Applies an explicit ordered connection to separate retained controls. All
// validation happens before the first mutation. The members must be distinct,
// live ButtonBase controls under one retained parent; existing connections on
// the other axis are rejected instead of being silently rewritten.
void connect_button_group(
    std::span<const std::shared_ptr<ButtonBase>> controls,
    ConnectedControlAxis axis);
void disconnect_button_group(
    std::span<const std::shared_ptr<ButtonBase>> controls);

// Renderer-neutral material composition used by ButtonBase and available to
// custom controls which need the same explicit connected physical grammar.
void paint_connected_surface_material(
    Painter& painter, Rect bounds, const SurfaceMaterial& material,
    std::optional<ConnectedControlTopology> topology);
[[nodiscard]] Insets connected_surface_visual_outsets(
    const SurfaceMaterial& material,
    std::optional<ConnectedControlTopology> topology) noexcept;

} // namespace gui_forms
