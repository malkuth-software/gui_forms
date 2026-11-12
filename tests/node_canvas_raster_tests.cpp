// NodeCanvas through the CPU renderer (OffscreenRaster with the bundled
// faces): the grid draws lines across the well, a node's face, a link and a
// port marker each paint a colour other than the well's, and an empty view
// paints only the well and grid. Every colour comes from the theme's parts,
// so the checks compare pixels with each other, never with literals.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/controls/node_graph.hpp"
#include "gui_forms/scene/scene_view/scene_view.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_node_canvas_raster_tests: " << message << '\n';
    std::exit(1);
}

std::unique_ptr<OffscreenRaster> made_raster() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made = OffscreenRaster::create(options);
    require(made.succeeded(), "the bundled faces make a raster");
    std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    require(raster != nullptr, "the raster is taken");
    return raster;
}

// The capture's pixel at (x, y), as B, G, R, A bytes packed in that order
// from the low byte.
std::uint32_t pixel_at(const WindowCapture& capture, std::uint32_t x, std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * capture.pixel_width + x) * 4U;
    std::uint32_t value = 0U;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const std::uint32_t byte = static_cast<std::uint32_t>(capture.bgra[at + channel]);
        value |= byte << (8U * channel);
    }
    return value;
}

// The distinct colours along row `y` from `left` to `right` (exclusive).
std::size_t colours_along(const WindowCapture& capture, std::uint32_t y, std::uint32_t left, std::uint32_t right) {
    std::set<std::uint32_t> colours{};
    for (std::uint32_t x = left; x < right; ++x) colours.insert(pixel_at(capture, x, y));
    return colours.size();
}

PortRecord port_of(const char* name, PortDirection direction, PortSide side) {
    PortRecord port{};
    port.name = name;
    port.direction = direction;
    port.side = side;
    port.offset = 0.5;
    port.socket_type = 1U;
    return port;
}

// Noise (30, 50) and Mix (200, 50), 96 by 64, linked along y = 82 from
// Noise.out (126, 82) to Mix.in (200, 82). The graph outlives the canvas.
void node_canvas_pixels() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    NodeGraph graph{};
    NodeSpec noise{};
    noise.key = "noise";
    noise.kind = "noise";
    noise.title = "Noise";
    noise.bounds = WorldRect{30.0, 50.0, 96.0, 64.0};
    noise.ports.push_back(port_of("out", PortDirection::output, PortSide::right));
    NodeSpec mix{};
    mix.key = "mix";
    mix.kind = "mix";
    mix.title = "Mix";
    mix.bounds = WorldRect{200.0, 50.0, 96.0, 64.0};
    mix.ports.push_back(port_of("in", PortDirection::input, PortSide::left));
    const NodeId a = graph.add_node(noise).node;
    const NodeId b = graph.add_node(mix).node;
    require(graph.connect(PortRef{a, PortIndex{0U}}, PortRef{b, PortIndex{0U}}).status == GraphStatus::ok,
            "the link is made");

    const std::shared_ptr<NodeCanvas> canvas = make_control<NodeCanvas>(StableId("canvas"));
    (*canvas).set_graph(&graph);
    Window window(canvas, Size{400.0, 300.0});
    window.set_active(true);
    window.perform_layout();
    OffscreenAttachment attached(*raster, window);
    const OffscreenCaptureOptions whole{};
    const std::optional<WindowCapture> picture = attached.capture_window(whole);
    require(picture.has_value() && (*picture).pixel_width == 400U && (*picture).pixel_height == 300U,
            "the canvas is captured at scale 1");
    const WindowCapture& capture = *picture;

    // (165, 138) is open well: off every node, link and grid multiple.
    const std::uint32_t well = pixel_at(capture, 165U, 138U);
    require((well >> 24U) == 255U, "the well is opaque");
    require(colours_along(capture, 270U, 0U, 400U) >= 2U, "the grid draws lines across the well");
    require(pixel_at(capture, 78U, 100U) != well, "a node's face differs from the well");
    require(pixel_at(capture, 165U, 82U) != well, "the link is drawn between the nodes");
    require(pixel_at(capture, 126U, 82U) != well, "the output's port marker is drawn");
    require((*canvas).statistics().frames_painted >= 1U && (*canvas).statistics().nodes_painted >= 2U,
            "the capture painted the canvas");

    // Selection changes the node's drawing.
    const std::uint32_t plain_face = pixel_at(capture, 78U, 100U);
    (*canvas).select_only(a);
    const std::optional<WindowCapture> selected = attached.capture_window(whole);
    require(selected.has_value(), "the selected canvas is captured");
    const bool edge_changed = pixel_at(*selected, 28U, 82U) != pixel_at(capture, 28U, 82U) ||
                              pixel_at(*selected, 78U, 100U) != plain_face;
    require(edge_changed, "a selected node is drawn differently");

    // Looking away: only the well and the grid remain.
    // 4992 is a multiple of the grid spacing, so the lines stay where they
    // were and (78, 100), off every line, shows the well.
    require((*canvas).set_view(SceneViewTransform{WorldPoint{4992.0, 4992.0}, 1.0}), "look away");
    const std::optional<WindowCapture> empty = attached.capture_window(whole);
    require(empty.has_value() && (*canvas).visible_nodes().empty(), "an empty view is captured");
    require(pixel_at(*empty, 78U, 100U) == well, "where the node was, an empty view shows the well");
}

} // namespace

int main() {
    node_canvas_pixels();
    std::cout << "gui_forms_node_canvas_raster_tests passed\n";
    return 0;
}
