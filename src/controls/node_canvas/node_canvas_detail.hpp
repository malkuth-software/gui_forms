#pragma once

// NodeCanvas's private vocabulary shared by its three sources (the model
// and caches, the input, the painting): the canvas anatomy's part table,
// port and lamp geometry and the callback scope. Nothing here is public.

#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/scene/scene_view/scene_view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gui_forms::node_canvas_detail {

// The theme's canvas anatomy (node_canvas_anatomy, canvas_look.hpp) by
// part name: the index of each part in this file's own table, which
// publish_part_geometry maps onto the anatomy's order by name.
inline constexpr std::size_t canvas_part_region = 0U;
inline constexpr std::size_t canvas_part_grid_minor = 1U;
inline constexpr std::size_t canvas_part_grid_major = 2U;
inline constexpr std::size_t canvas_part_node = 3U;
inline constexpr std::size_t canvas_part_node_header = 4U;
inline constexpr std::size_t canvas_part_node_selection = 5U;
inline constexpr std::size_t canvas_part_port = 6U;
inline constexpr std::size_t canvas_part_link = 7U;
inline constexpr std::size_t canvas_part_dashed_link = 8U;
inline constexpr std::size_t canvas_part_lamp = 9U;
inline constexpr std::size_t canvas_part_marquee = 10U;
inline constexpr std::size_t canvas_part_port_label = 11U;
inline constexpr std::size_t canvas_part_focus = 12U;
inline constexpr std::size_t canvas_part_knife = 13U;
inline constexpr std::size_t canvas_part_link_preview = 14U;
inline constexpr std::size_t canvas_part_count = 15U;
inline constexpr std::array<std::string_view, canvas_part_count> canvas_part_names{
    "region",  "grid_minor", "grid_major", "node",  "node_header", "node_selection", "port",         "link",
    "dashed_link", "lamp",   "marquee",    "port_label", "focus",  "knife",          "link_preview",
};

// The table index of the anatomy part named `name`, or none for a part
// this canvas does not publish.
[[nodiscard]] inline std::optional<std::size_t> canvas_part_index(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < canvas_part_count; ++index) {
        if (canvas_part_names[index] == name) return index;
    }
    return std::nullopt;
}

// Geometry in world units (zoomed with the scene) unless named view.
inline constexpr double node_corner_radius = 6.0;
inline constexpr double port_marker_size = 9.0;
inline constexpr double smallest_port_marker = 3.0;
inline constexpr double smallest_lamp = 2.0;
inline constexpr double lamp_size = 6.0;
inline constexpr double lamp_inset = 12.0;
inline constexpr double caption_text_size = 12.0;
inline constexpr double port_label_size = 10.0;
inline constexpr double port_label_gap = 10.0;
inline constexpr double port_label_width = 80.0;
// View units between a node's body and the inside of its selection ring,
// as paint_canvas_node_selection strokes it (zoom-independent).
inline constexpr double selection_ring_gap = 2.0;
// View units: the magnet target's square around its port, the captured
// target's drop ring, the link strokes and the frame margins.
inline constexpr double target_side = 12.0;
inline constexpr double drop_ring = 18.0;
inline constexpr double link_width_minimum = 1.0;
inline constexpr double link_width_maximum = 3.0;
inline constexpr double link_width_world = 2.0;
inline constexpr double selected_link_halo = 4.0;
inline constexpr double frame_all_margin = 40.0;
inline constexpr double frame_selection_margin = 80.0;
inline constexpr double reveal_margin = 24.0;
inline constexpr double key_pan_step = 40.0;
inline constexpr double wheel_pixels_per_line = 40.0;
inline constexpr double wheel_step = 1.15;
inline constexpr double fine_wheel_step = 1.02;
inline constexpr double key_zoom_step = 1.25;
// The move gesture's snap bounds reach this many grid cells from the world
// origin in every direction (a multiple of the grid, far past any graph).
inline constexpr double snap_bounds_cells = 65536.0;
// The keyboard link and link_to_nearest try at most this many candidates.
inline constexpr std::size_t keyboard_link_tries = 16U;

// The side a port sits on: its own, or for an automatic port the side its
// direction faces (outputs right, inputs left), as the link router's soft
// style does.
[[nodiscard]] inline PortSide effective_side(const PortRecord& port) noexcept {
    if (port.side != PortSide::automatic) return port.side;
    const PortSide side = port.direction == PortDirection::output ? PortSide::right : PortSide::left;
    return side;
}

[[nodiscard]] inline SceneEdge edge_of(PortSide side) noexcept {
    switch (side) {
    case PortSide::left: return SceneEdge::left;
    case PortSide::right: return SceneEdge::right;
    case PortSide::top: return SceneEdge::top;
    case PortSide::bottom: return SceneEdge::bottom;
    default: break;
    }
    // Automatic is resolved before this; anything else leaves right.
    return SceneEdge::right;
}

[[nodiscard]] inline WorldPoint port_world(const NodeRecord& node, const PortRecord& port) noexcept {
    const WorldPoint anchor = port_anchor(node, effective_side(port), port.offset);
    return anchor;
}

// A port's disc radius at `zoom`, view units (never under half the
// smallest marker, so a port stays findable when zoomed out).
[[nodiscard]] inline double port_radius(double zoom) noexcept {
    const double size = std::max(smallest_port_marker, port_marker_size * zoom);
    return size / 2.0;
}

[[nodiscard]] inline double lamp_radius(double zoom) noexcept {
    const double size = std::max(smallest_lamp, lamp_size * zoom);
    return size / 2.0;
}

// An output's lamp: inside the node, lamp_inset world units in from the
// port along its edge's inward normal, in view units.
[[nodiscard]] inline Point lamp_centre(const NodeRecord& node, const PortRecord& port, double zoom,
                                       const SceneViewTransform& view) noexcept {
    const Point anchor = view.to_view(port_world(node, port));
    const double inset = lamp_inset * zoom;
    switch (effective_side(port)) {
    case PortSide::left: return Point{anchor.x + inset, anchor.y};
    case PortSide::right: return Point{anchor.x - inset, anchor.y};
    case PortSide::top: return Point{anchor.x, anchor.y + inset};
    case PortSide::bottom: return Point{anchor.x, anchor.y - inset};
    default: break;
    }
    // Automatic is resolved by effective_side; anything else stays put.
    return anchor;
}

// The flowing links' order: by slot, then generation.
[[nodiscard]] inline bool link_before(LinkId left, LinkId right) noexcept {
    if (left.index != right.index) return left.index < right.index;
    return left.generation < right.generation;
}

[[nodiscard]] inline PortRef port_ref(std::uint32_t slot, const NodeRecord& node, std::size_t port) noexcept {
    const PortRef ref{NodeId{slot, node.generation}, PortIndex{static_cast<std::uint16_t>(port)}};
    return ref;
}

} // namespace gui_forms::node_canvas_detail

namespace gui_forms {

class NodeCanvas::CallbackScope final {
public:
    explicit CallbackScope(const NodeCanvas& canvas) noexcept : canvas_(canvas), previous_(canvas.in_callback_) {
        canvas_.in_callback_ = true;
    }
    ~CallbackScope() { canvas_.in_callback_ = previous_; }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

private:
    const NodeCanvas& canvas_;
    bool previous_;
};

} // namespace gui_forms
