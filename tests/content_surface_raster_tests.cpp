// ContentSurface in pixels (cp32 content census return): the consumer's frame
// is a part drawn as given, and the stale veil and status caption are parts
// the theme styles. In the default theme the stale frame darkens under the
// veil, still visible, and the caption is written in white; in forced high
// contrast the live frame is still drawn as given (content), and the stale
// veil covers it in the plate with the caption in the high-contrast text
// colour.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL " << message << '\n';
}

// The region's size, and the frame's grey (every channel, opaque).
constexpr double region_width = 160.0;
constexpr double region_height = 80.0;
constexpr std::uint8_t frame_grey = 0x80U;
// The capture's margin around the control, in pixels at scale 1.
constexpr double capture_margin = 4.0;

struct Pixel final {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t alpha{};
};

Pixel pixel_at(const WindowCapture& picture, std::uint32_t x, std::uint32_t y) {
    Pixel result{};
    if (x >= picture.pixel_width || y >= picture.pixel_height) return result;
    const std::size_t at = (static_cast<std::size_t>(y) * picture.pixel_width + x) * 4U;
    result.blue = static_cast<std::uint8_t>(picture.bgra[at]);
    result.green = static_cast<std::uint8_t>(picture.bgra[at + 1U]);
    result.red = static_cast<std::uint8_t>(picture.bgra[at + 2U]);
    result.alpha = static_cast<std::uint8_t>(picture.bgra[at + 3U]);
    return result;
}

bool same_pixel(Pixel left, Pixel right) {
    const bool same = left.blue == right.blue && left.green == right.green && left.red == right.red;
    return same;
}

// A pixel in the region's lower right quarter, away from the caption.
Pixel quiet_pixel(const WindowCapture& picture) {
    const std::uint32_t x = static_cast<std::uint32_t>(capture_margin + region_width * 0.75);
    const std::uint32_t y = static_cast<std::uint32_t>(capture_margin + region_height * 0.75);
    const Pixel result = pixel_at(picture, x, y);
    return result;
}

// Whether any pixel in the caption's band (the region's top 30 units, inset
// 8) differs from `ground` by more than `threshold` in some channel, and in
// the white test whether any is near white.
struct CaptionInk final {
    bool differs{};
    bool near_white{};
};

CaptionInk caption_ink(const WindowCapture& picture, Pixel ground, int threshold) {
    CaptionInk found{};
    const std::uint32_t left = static_cast<std::uint32_t>(capture_margin + 8.0);
    const std::uint32_t right = static_cast<std::uint32_t>(capture_margin + region_width - 8.0);
    const std::uint32_t top = static_cast<std::uint32_t>(capture_margin + 8.0);
    const std::uint32_t bottom = static_cast<std::uint32_t>(capture_margin + 30.0);
    for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
            const Pixel sample = pixel_at(picture, x, y);
            const int blue = std::abs(static_cast<int>(sample.blue) - static_cast<int>(ground.blue));
            const int green = std::abs(static_cast<int>(sample.green) - static_cast<int>(ground.green));
            const int red = std::abs(static_cast<int>(sample.red) - static_cast<int>(ground.red));
            if (std::max(blue, std::max(green, red)) > threshold) found.differs = true;
            if (sample.blue > 220U && sample.green > 220U && sample.red > 220U) found.near_white = true;
        }
    }
    return found;
}

void set_high_contrast(Window& window, bool forced) {
    PresentationSettings settings = window.presentation_base();
    settings.high_contrast = forced;
    window.set_presentation_settings(settings);
}

struct Scene final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ContentSurface> surface;
    std::unique_ptr<Window> window;
    std::vector<std::byte> pixels;
    ContentFrameSink sink;
};

void make_scene(Scene& scene) {
    scene.root = make_control<Panel>(StableId("content.raster.root"));
    scene.surface = make_control<ContentSurface>(StableId("content.raster.surface"));
    (*scene.surface).set_requested_bounds({10.0, 10.0, region_width, region_height});
    (*scene.root).add_child(scene.surface);
    scene.window = std::make_unique<Window>(scene.root, Size{200.0, 120.0});
    (*scene.window).perform_layout();
    scene.sink = (*scene.surface).open_content(true);
    const std::size_t size = static_cast<std::size_t>(region_width * region_height * 4.0);
    scene.pixels.assign(size, static_cast<std::byte>(frame_grey));
    ContentFrame frame{};
    frame.width = static_cast<std::uint32_t>(region_width);
    frame.height = static_cast<std::uint32_t>(region_height);
    frame.row_bytes = static_cast<std::uint64_t>(region_width) * 4U;
    frame.opaque = true;
    frame.pixels = std::span<const std::byte>(scene.pixels.data(), scene.pixels.size());
    require(scene.sink.publish(frame) == ContentPublishResult::published, "the frame is published");
    for (int turn = 0; turn < 8; ++turn) static_cast<void>((*scene.window).drain_posted_work());
}

std::optional<WindowCapture> capture(OffscreenAttachment& attached, const Scene& scene) {
    (*scene.window).perform_layout();
    OffscreenCaptureOptions options{};
    options.scale = 1.0;
    options.margin = capture_margin;
    std::optional<WindowCapture> picture = attached.capture_control(scene.surface, options);
    return picture;
}

void make_stale(Scene& scene) {
    ContentSurface& surface = *scene.surface;
    surface.set_stale_interval(std::chrono::milliseconds(200));
    const FrameTime now = FrameClock::now();
    surface.check_watchdog(now);
    surface.check_watchdog(now + std::chrono::seconds(2));
}

void veil_and_caption_in_pixels() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made = OffscreenRaster::create(options);
    require(made.succeeded(), "the bundled faces make a raster");
    if (!made.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    Scene scene{};
    make_scene(scene);
    OffscreenAttachment attached(*raster, *scene.window);

    // Default theme, live: the frame as published.
    const std::optional<WindowCapture> live = capture(attached, scene);
    require(live.has_value(), "the live surface is captured");
    if (!live) return;
    const Pixel live_pixel = quiet_pixel(*live);
    require(live_pixel.red == frame_grey && live_pixel.green == frame_grey && live_pixel.blue == frame_grey,
            "a live frame is drawn exactly as given");
    require(!(*scene.surface).states().interrupted, "a live surface is not interrupted");

    // High contrast, live: the frame is content and stays as given.
    set_high_contrast(*scene.window, true);
    const std::optional<WindowCapture> live_contrast = capture(attached, scene);
    require(live_contrast.has_value() && same_pixel(quiet_pixel(*live_contrast), live_pixel),
            "in high contrast the consumer's frame is still drawn as given");
    set_high_contrast(*scene.window, false);

    // Default theme, stale: the frame darkens under the veil, still
    // visible, and the caption is white.
    make_stale(scene);
    require((*scene.surface).status() == ContentSurfaceStatus::stale && (*scene.surface).states().interrupted,
            "a stale surface is in the interrupted state");
    const ContentSurface::Parts parts = (*scene.surface).parts();
    require(parts.veil.present && parts.caption.present, "the veil and caption parts are published");
    const std::optional<WindowCapture> stale = capture(attached, scene);
    require(stale.has_value(), "the stale surface is captured");
    if (!stale) return;
    const Pixel veiled = quiet_pixel(*stale);
    require(veiled.red < live_pixel.red && veiled.red > 40U && veiled.red == veiled.green && veiled.green == veiled.blue,
            "the veil darkens the frame and leaves it visible (" + std::to_string(veiled.red) + ")");
    const CaptionInk ink = caption_ink(*stale, veiled, 60);
    require(ink.differs && ink.near_white, "the caption is written in white over the veil");

    // High contrast, stale: the plate covers the frame, the caption in the
    // high-contrast text colour.
    set_high_contrast(*scene.window, true);
    const std::optional<WindowCapture> stale_contrast = capture(attached, scene);
    require(stale_contrast.has_value(), "the stale surface is captured in high contrast");
    if (!stale_contrast) return;
    const Pixel plate = quiet_pixel(*stale_contrast);
    require(!same_pixel(plate, live_pixel) && !same_pixel(plate, veiled),
            "in high contrast the veil is the plate, not a blend over the frame");
    const CaptionInk contrast_ink = caption_ink(*stale_contrast, plate, 100);
    require(contrast_ink.differs, "the caption stands out from the plate in high contrast");
    set_high_contrast(*scene.window, false);
}

} // namespace

int main() {
    try {
        veil_and_caption_in_pixels();
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_content_surface_raster_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "gui_forms_content_surface_raster_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
