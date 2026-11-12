// Window::snapshot_control, renderer-free (stage DK phase D3, owner decision
// D1: the stamp's picture): unsupported without a framebuffer painter; with
// one, the subtree is replayed into a framebuffer of the scaled size and its
// pixels registered as premultiplied BGRA (an RGBA raster swapped); refused
// for a bad scale, another window's control or a hidden one; released.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/fake_framebuffer.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_control_snapshot_tests: " << message << '\n';
    std::exit(1);
}

// Releases a snapshot from another thread, recording whether it was refused.
struct OtherThreadRelease final {
    Window* window{};
    ImageId image{};
    bool* refused{};
    void operator()() const {
        try {
            static_cast<void>((*window).release_control_snapshot(image));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void snapshots() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> card = make_control<Panel>(StableId("card"));
    (*card).set_background(Color::rgba(200, 40, 40));
    (*card).set_requested_bounds({20.0, 10.0, 101.0, 60.0});
    std::shared_ptr<Button> button = make_control<Button>(StableId("card.button"), "Act");
    (*button).set_requested_bounds({8.0, 8.0, 60.0, 24.0});
    (*card).add_child(button);
    (*root).add_child(card);
    // Declared before the window that borrows it, so it outlives the window.
    test_support::FakeRaster raster{};
    Window window(root, {200.0, 120.0});
    window.perform_layout();
    require(!window.snapshot_control(card, 1.0), "without a framebuffer painter there is no snapshot");

    window.set_framebuffer_painter(&raster);
    const std::uint64_t images = window.image_resources().snapshot().resource_count;
    const std::optional<ControlSnapshot> snapshot = window.snapshot_control(card, 0.5);
    require(snapshot.has_value() && raster.made == 1U, "a framebuffer painter gives a snapshot");
    require((*snapshot).pixel_width == 51U && (*snapshot).pixel_height == 30U &&
                (*snapshot).logical_size == Size{101.0, 60.0} && (*snapshot).scale == 0.5,
            "the picture covers the control at the scale asked, rounded up");
    require(raster.replayed.fills > 0U && raster.replayed.texts > 0U,
            "the subtree's painting (the card and its button) is replayed into the framebuffer");
    const std::optional<ImageResourceView> view = window.image_resources().find((*snapshot).image);
    require(view.has_value() && (*view).row_bytes == 51U * 4U && (*view).encoded.size() == 51U * 30U * 4U,
            "the pixels are registered as one image of the window");
    require((*view).encoded[0] == std::byte{1} && (*view).encoded[1] == std::byte{2} &&
                (*view).encoded[2] == std::byte{3} && (*view).encoded[3] == std::byte{4},
            "the framebuffer's BGRA pixels are stored as they are");
    require(window.image_resources().snapshot().resource_count == images + 1U, "one image was added");

    require(!window.snapshot_control(card, 0.0) && !window.snapshot_control(card, 9.0),
            "a scale outside (0, 8] is refused");
    std::shared_ptr<Panel> foreign = make_control<Panel>(StableId("foreign"));
    Window other(foreign, {50.0, 50.0});
    require(!window.snapshot_control(foreign, 1.0), "another window's control is refused");
    (*card).visible = false;
    window.perform_layout();
    require(!window.snapshot_control(card, 1.0), "a hidden control is refused");
    (*card).visible = true;
    window.perform_layout();

    // Finding V4: the release is UI-thread only and repaints as remove_image.
    bool refused = false;
    std::thread releaser(OtherThreadRelease{&window, (*snapshot).image, &refused});
    releaser.join();
    require(refused && window.image_resources().find((*snapshot).image).has_value(),
            "a release off the UI thread is refused and keeps the image");
    const std::uint64_t marks = window.metrics_snapshot().dirty_marks;
    require(window.release_control_snapshot((*snapshot).image) && !window.image_resources().find((*snapshot).image) &&
                !window.release_control_snapshot((*snapshot).image),
            "releasing removes the image once");
    require(window.metrics_snapshot().dirty_marks > marks,
            "releasing a snapshot retires the window's retained paint, as remove_image does");
    window.set_framebuffer_painter(nullptr);
}

// Window::capture_window (owner request 2026-11-09): the whole window with
// its backplane, at the window's scale or another, its overlays optional;
// refused without a raster, for a bad scale and from another thread.
struct OtherThreadCapture final {
    Window* window{};
    bool* refused{};
    void operator()() const {
        try {
            static_cast<void>((*window).capture_window());
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void window_captures() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Panel> card = make_control<Panel>(StableId("card"));
    (*card).set_background(Color::rgba(200, 40, 40));
    (*card).set_requested_bounds({20.0, 10.0, 100.0, 60.0});
    (*root).add_child(card);
    test_support::FakeRaster raster{};
    Window window(root, {201.0, 120.0});
    window.perform_layout();
    require(!window.capture_window(), "without a framebuffer painter there is no capture");

    window.set_framebuffer_painter(&raster);
    const std::uint64_t images = window.image_resources().snapshot().resource_count;
    const std::optional<WindowCapture> capture = window.capture_window();
    require(capture.has_value() && raster.made == 1U, "a framebuffer painter gives a capture");
    require((*capture).pixel_width == 201U && (*capture).pixel_height == 120U &&
                (*capture).logical_size == Size{201.0, 120.0} && (*capture).scale == window.scale() &&
                (*capture).bgra.size() == 201U * 120U * 4U,
            "the capture covers the whole window at its own scale");
    require((*capture).bgra[0] == std::byte{1} && (*capture).bgra[3] == std::byte{4},
            "the framebuffer's BGRA pixels are copied as they are");
    require(window.image_resources().snapshot().resource_count == images,
            "a capture registers no image: it owns its pixels");
    const std::size_t content_fills = raster.replayed.fills;
    const std::optional<ControlSnapshot> card_only = window.snapshot_control(card, 1.0);
    require(card_only.has_value(), "the card snapshots");
    const std::size_t card_fills = raster.replayed.fills - content_fills;
    require(content_fills > card_fills, "the capture draws more than the card: the backplane beneath it");
    require(window.release_control_snapshot((*card_only).image), "the snapshot is released");

    WindowCaptureOptions half{};
    half.scale = 0.5;
    const std::optional<WindowCapture> halved = window.capture_window(half);
    require(halved.has_value() && (*halved).pixel_width == 101U && (*halved).pixel_height == 60U &&
                (*halved).scale == 0.5,
            "at scale 0.5 the capture is half the size, rounded up");
    WindowCaptureOptions too_large{};
    too_large.scale = 9.0;
    WindowCaptureOptions negative{};
    negative.scale = -1.0;
    require(!window.capture_window(too_large) && !window.capture_window(negative),
            "a scale outside (0, 8] is refused");

    // An overlay is drawn over the content unless the options leave it out.
    std::shared_ptr<Panel> overlay = make_control<Panel>(StableId("overlay"));
    (*overlay).set_background(Color::rgba(10, 200, 10));
    (*overlay).set_requested_bounds({60.0, 40.0, 80.0, 40.0});
    OverlayToken token = window.open_overlay(root, overlay);
    window.perform_layout();
    const std::size_t before_with = raster.replayed.fills;
    require(window.capture_window().has_value(), "a capture with the overlay");
    const std::size_t with_overlay = raster.replayed.fills - before_with;
    WindowCaptureOptions without{};
    without.include_overlays = false;
    const std::size_t before_without = raster.replayed.fills;
    require(window.capture_window(without).has_value(), "a capture without the overlay");
    const std::size_t without_overlay = raster.replayed.fills - before_without;
    require(with_overlay > without_overlay, "the overlay's painting is in the capture only when asked for");
    token.disconnect();

    std::vector<std::byte> png{};
    require(!encode_window_capture_png(WindowCapture{}, png) && png.empty(), "an empty capture encodes nothing");
    require(encode_window_capture_png(*capture, png) && png.size() > 8U && png[1] == std::byte{'P'} &&
                png[2] == std::byte{'N'} && png[3] == std::byte{'G'},
            "a capture encodes as a PNG");

    bool refused = false;
    std::thread capturer(OtherThreadCapture{&window, &refused});
    capturer.join();
    require(refused, "a capture off the UI thread is refused");
    window.set_framebuffer_painter(nullptr);
}

} // namespace

int main() {
    snapshots();
    window_captures();
    std::cout << "Control snapshots: unsupported without a raster, the scaled replay, BGRA registration, refusals "
                 "and release passed\n";
    return EXIT_SUCCESS;
}
