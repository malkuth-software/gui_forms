// Window::snapshot_control on the Windows host's CPU raster (the Skia raster, the
// raster the host presents): a snapshot at scale 1 matches, pixel for pixel,
// the same control painted by the window itself; at scale 0.5 it is half
// the size.

#include "gui_forms/window.hpp"
#include "gui_forms/basic_controls.hpp"
#include "../src/host/windows/application/windows_host.cpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_windows_control_snapshot_tests: " << message << '\n';
    std::exit(1);
}

void matches_direct_paint() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> card = make_control<Panel>(StableId("card"));
    (*card).set_background(Color::rgba(200, 40, 40));
    (*card).set_requested_bounds({20.0, 10.0, 100.0, 60.0});
    std::shared_ptr<Panel> inner = make_control<Panel>(StableId("card.inner"));
    (*inner).set_background(Color::rgba(30, 90, 200));
    (*inner).set_requested_bounds({10.0, 12.0, 50.0, 30.0});
    (*card).add_child(inner);
    (*root).add_child(card);
    const Size size{200.0, 120.0};
    // Declared before the window that borrows it, so it outlives the window.
    host::WindowsRaster host_raster;
    require(host_raster.resize(size, 1.0), "the raster sizes");
    Window window(root, size);
    window.perform_layout();
    window.set_framebuffer_painter(&host_raster.painter());

    // The window's own paint, into a framebuffer of the same raster.
    const std::unique_ptr<PaintFramebuffer> direct = window.create_framebuffer(size, 1.0);
    require(direct != nullptr && (*direct).begin(window.image_resources(), {0.0, 0.0, size.width, size.height}),
            "a direct framebuffer begins");
    require(window.paint((*direct).painter()).has_value(), "the window paints");
    (*direct).end();

    const std::optional<ControlSnapshot> snapshot = window.snapshot_control(card, 1.0);
    require(snapshot.has_value() && (*snapshot).pixel_width == 100U && (*snapshot).pixel_height == 60U,
            "the host raster snapshots the card");
    const std::optional<ImageResourceView> view = window.image_resources().find((*snapshot).image);
    require(view.has_value(), "the picture is registered");
    const std::span<const std::byte> picture = (*view).encoded;
    const std::span<std::byte> painted = (*direct).pixels();
    const std::size_t direct_row = (*direct).row_bytes();
    std::size_t differences = 0U;
    for (std::size_t y = 0; y < 60U; ++y) {
        for (std::size_t x = 0; x < 100U * 4U; ++x) {
            const std::byte from_snapshot = picture[y * (*view).row_bytes + x];
            const std::byte from_window = painted[(y + 10U) * direct_row + 20U * 4U + x];
            if (from_snapshot != from_window) ++differences;
        }
    }
    std::cout << "snapshot vs direct paint: " << differences << " differing bytes\n";
    require(differences == 0U, "the snapshot matches the window's own paint pixel for pixel");

    const std::optional<ControlSnapshot> half = window.snapshot_control(card, 0.5);
    require(half.has_value() && (*half).pixel_width == 50U && (*half).pixel_height == 30U,
            "at scale 0.5 the picture is half the size");
    require(window.release_control_snapshot((*snapshot).image) && window.release_control_snapshot((*half).image),
            "the pictures are released");
    window.set_framebuffer_painter(nullptr);
}

// Window::capture_window on the host raster: at the window's scale the
// capture is the window's own full paint, backplane included, pixel for
// pixel, and it encodes as a PNG.
void capture_matches_presented_frame() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> card = make_control<Panel>(StableId("card"));
    (*card).set_background(Color::rgba(200, 40, 40));
    (*card).set_requested_bounds({20.0, 10.0, 100.0, 60.0});
    std::shared_ptr<Button> button = make_control<Button>(StableId("card.button"), "Act");
    (*button).set_requested_bounds({8.0, 8.0, 60.0, 24.0});
    (*card).add_child(button);
    (*root).add_child(card);
    const Size size{200.0, 120.0};
    host::WindowsRaster host_raster;
    require(host_raster.resize(size, 1.0), "the raster sizes");
    Window window(root, size);
    window.perform_layout();
    window.set_framebuffer_painter(&host_raster.painter());

    const std::unique_ptr<PaintFramebuffer> direct = window.create_framebuffer(size, 1.0);
    require(direct != nullptr && (*direct).begin(window.image_resources(), {0.0, 0.0, size.width, size.height}),
            "a direct framebuffer begins");
    require(window.paint((*direct).painter()).has_value(), "the window paints");
    (*direct).end();

    WindowCaptureOptions options{};
    options.scale = 1.0;
    const std::optional<WindowCapture> capture = window.capture_window(options);
    require(capture.has_value() && (*capture).pixel_width == 200U && (*capture).pixel_height == 120U,
            "the host raster captures the window");
    const std::span<std::byte> painted = (*direct).pixels();
    const std::size_t direct_row = (*direct).row_bytes();
    std::size_t differences = 0U;
    for (std::size_t y = 0; y < 120U; ++y) {
        for (std::size_t x = 0; x < 200U * 4U; ++x) {
            if ((*capture).bgra[y * 200U * 4U + x] != painted[y * direct_row + x]) ++differences;
        }
    }
    std::cout << "capture vs presented frame: " << differences << " differing bytes\n";
    require(differences == 0U, "the capture matches the window's own paint pixel for pixel");
    // The corner lies outside the card: the backplane, opaque.
    require((*capture).bgra[3] == std::byte{255}, "the backplane is drawn, opaque");
    std::vector<std::byte> png{};
    require(encode_window_capture_png(*capture, png) && png.size() > 8U, "the capture encodes as a PNG");
    window.set_framebuffer_painter(nullptr);
}

} // namespace

int main() {
    matches_direct_paint();
    capture_matches_presented_frame();
    std::cout << "Windows control snapshots matched the direct paint\n";
    return EXIT_SUCCESS;
}
