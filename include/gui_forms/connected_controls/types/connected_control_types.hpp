#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gui_forms {

// Connection is authored explicitly. GUI.Forms never infers a group from
// neighboring rectangles because layout changes must not silently change a
// control's paint, hit target, focus identity, or semantics.
enum class ConnectedControlAxis : std::uint8_t {
    horizontal,
    vertical,
};

enum class ConnectedControlSegment : std::uint8_t {
    standalone,
    leading,
    middle,
    trailing,
};

struct GUI_FORMS_API ConnectedControlTopology final {
    ConnectedControlAxis axis{ConnectedControlAxis::horizontal};
    std::size_t index{};
    std::size_t count{};

    friend constexpr bool operator==(const ConnectedControlTopology& left,
                                     const ConnectedControlTopology& right) noexcept {
        return left.axis == right.axis && left.index == right.index &&
               left.count == right.count;
    }
};

struct GUI_FORMS_API ConnectedControlSeam final {
    Point from{};
    Point to{};
    Color color{};
    double width{};

    friend constexpr bool operator==(const ConnectedControlSeam& left,
                                     const ConnectedControlSeam& right) noexcept {
        return left.from == right.from && left.to == right.to &&
               left.color == right.color && left.width == right.width;
    }
};

// The resolved recipe owns no renderer object. `paint_bounds` is deliberately
// larger across joined edges; clipping it to `clip_bounds` removes interior
// corners and duplicate borders while retaining the material's outer radius.
struct GUI_FORMS_API ConnectedControlVisualGeometry final {
    ConnectedControlSegment segment{ConnectedControlSegment::standalone};
    Rect clip_bounds{};
    Rect paint_bounds{};
    std::optional<ConnectedControlSeam> leading_seam;
};

[[nodiscard]] GUI_FORMS_API bool valid_connected_control_topology(
    ConnectedControlTopology topology) noexcept;
[[nodiscard]] GUI_FORMS_API ConnectedControlSegment connected_control_segment(
    std::optional<ConnectedControlTopology> topology) noexcept;
[[nodiscard]] GUI_FORMS_API ConnectedControlVisualGeometry
resolve_connected_control_visual_geometry(
    Rect bounds, const SurfaceMaterial& material,
    std::optional<ConnectedControlTopology> topology) noexcept;

} // namespace gui_forms
