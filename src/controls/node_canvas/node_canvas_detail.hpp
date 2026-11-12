#pragma once

// NodeCanvas's private vocabulary shared by its three sources (the model
// and caches, the input, the painting): part names, the frame's resolved
// inks, port geometry and the callback scope. Nothing here is public.

#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/scene/scene_view/scene_view.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms::node_canvas_detail {

// The anatomy's part names (node_canvas.cpp declares them): the names a
// theme's `node-canvas::part(...)` rules match through each part's key.
inline constexpr std::string_view region_part = "region";
inline constexpr std::string_view well_part = "well";
inline constexpr std::string_view grid_minor_part = "grid_minor";
inline constexpr std::string_view grid_major_part = "grid_major";
inline constexpr std::string_view connector_part = "connector";
inline constexpr std::string_view marquee_part = "marquee";
inline constexpr std::string_view knife_part = "knife";
inline constexpr std::string_view drop_part = "drop";
inline constexpr std::string_view focus_part = "focus";
inline constexpr std::string_view node_part = "node";
inline constexpr std::string_view node_face_part = "node_face";
inline constexpr std::string_view node_selection_part = "node_selection";
inline constexpr std::string_view node_caption_part = "node_caption";
inline constexpr std::string_view port_part = "port";
inline constexpr std::string_view port_label_part = "port_label";
inline constexpr std::string_view lamp_part = "lamp";
inline constexpr std::string_view node_focus_part = "node_focus";

// Geometry in world units (zoomed with the scene) unless named view.
inline constexpr double node_corner_radius = 6.0;
inline constexpr double port_marker_size = 9.0;
inline constexpr double smallest_port_marker = 3.0;
inline constexpr double lamp_size = 6.0;
inline constexpr double lamp_inset = 12.0;
inline constexpr double caption_text_size = 12.0;
inline constexpr double port_label_size = 10.0;
inline constexpr double port_label_gap = 10.0;
inline constexpr double port_label_width = 80.0;
inline constexpr double selection_outset = 3.0;
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

// The grid's levels (select_grid_level's band).
inline constexpr GridLevelBand grid_band{12.0, 48.0};

// The shapes of the port buckets: filled shapes first (a bucket's shape
// and its accent colour both name the socket type).
inline constexpr std::array<MarkerShape, NodeCanvas::port_buckets> port_shapes{
    MarkerShape::circle,        MarkerShape::square,        MarkerShape::diamond,     MarkerShape::triangle_up,
    MarkerShape::triangle_right, MarkerShape::triangle_down, MarkerShape::triangle_left, MarkerShape::plus,
};

[[nodiscard]] inline std::size_t port_bucket(std::uint16_t socket_type) noexcept {
    const std::size_t bucket = static_cast<std::size_t>(socket_type) % NodeCanvas::port_buckets;
    return bucket;
}

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

[[nodiscard]] inline PortRef port_ref(std::uint32_t slot, const NodeRecord& node, std::size_t port) noexcept {
    const PortRef ref{NodeId{slot, node.generation}, PortIndex{static_cast<std::uint16_t>(port)}};
    return ref;
}

} // namespace gui_forms::node_canvas_detail

namespace gui_forms {

// The colours one frame resolves from the theme's parts (each once per
// frame, never per node).
struct NodeCanvas::FrameInks final {
    Color grid_minor{};
    Color grid_major{};
    Color node_fill{};
    Color node_selected_fill{};
    Color selection{};
    Color port_edge{};
    Color knife{};
    Color preview{};
    std::array<Color, port_buckets> sockets{};
};

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
