// A painting canvas through the CPU Skia raster (benchmarks/
// canvas_pan_benchmark.cpp draws the same scene):
//
// - occluded replay (replay_display_chunk_occluded) draws the bytes a full
//   replay draws: the scene under an opaque live surface is painted twice,
//   once with the surface opaque (its cover hides what lies beneath) and
//   once with the same pixels marked not opaque (nothing is hidden), at
//   device scales 1 and 2, sheet positions on and off whole pixels, inside
//   the canvas and past its edges, and under a clipped group;
// - a pattern drawn through the kept device tile is within one level of
//   the direct draw, opaque or translucent, at opacity 0.75 and 1 (an
//   opaque tile at full opacity is copied), at scales 1, 1.5 and 2.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/live_surface.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms::render {
struct SkiaCanvasTestAccess final {
    static void sample_directly(SkiaRaster& raster, bool direct) { raster.direct_patterns_ = direct; }
    static std::uint64_t covers_promised(const SkiaRaster& raster) { return raster.covers_promised_; }
};
} // namespace gui_forms::render

namespace {

using namespace gui_forms;
using gui_forms::render::SkiaRaster;

void require(const bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_canvas_occlusion_raster_tests: " << message << '\n';
    std::exit(1);
}

constexpr Size window_size{320.0, 240.0};
constexpr Rect canvas_bounds{0.0, 20.0, 320.0, 200.0};
constexpr std::uint32_t felt_pixels = 16U;

// The felt: opaque, or with a translucent stripe when `translucent`.
std::vector<std::byte> felt_bytes(const bool translucent) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(felt_pixels) * felt_pixels * 4U);
    for (std::uint32_t y = 0U; y < felt_pixels; ++y) {
        for (std::uint32_t x = 0U; x < felt_pixels; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * felt_pixels + x) * 4U;
            const unsigned alpha = translucent && y < 4U ? 128U : 255U;
            const unsigned grain = (x * 37U + y * 101U) % 64U;
            bytes[at] = static_cast<std::byte>((150U + grain) * alpha / 255U);
            bytes[at + 1U] = static_cast<std::byte>((120U + grain) * alpha / 255U);
            bytes[at + 2U] = static_cast<std::byte>((90U + grain) * alpha / 255U);
            bytes[at + 3U] = static_cast<std::byte>(alpha);
        }
    }
    return bytes;
}

class SheetCanvas final : public Control {
public:
    explicit SheetCanvas(StableId id) : Control(std::move(id)) {}

    ImageId felt{};
    Rect sheet{};
    std::shared_ptr<LiveSurface> surface;
    // Draws the surface inside a save with a clip, as an application
    // limiting it to the visible part of the sheet does.
    bool clipped{};

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds = client_rectangle();
        painter.fill_image_pattern(felt, {static_cast<double>(felt_pixels), static_cast<double>(felt_pixels)}, bounds,
                                   {16.0, 16.0});
        const GradientStop light[] = {{0.0, Color::rgba(255, 255, 255, 22)}, {1.0, Color::rgba(60, 77, 95, 15)}};
        painter.fill_linear_gradient(bounds, {0.0, 0.0}, {0.0, bounds.height}, light);
        painter.draw_box_shadow(sheet, 0.0, {1.5, 2.5}, 6.0, 0.0, Color::rgba(42, 53, 67, 75));
        painter.stroke_rect({sheet.x - 0.5, sheet.y - 0.5, sheet.width + 1.0, sheet.height + 1.0},
                            Color::rgba(93, 111, 130, 155), 1.0);
        if (clipped) {
            painter.save();
            painter.clip_rect(Rect::intersection(sheet, bounds));
            painter.draw_live_surface(surface, sheet);
            painter.restore();
        } else {
            painter.draw_live_surface(surface, sheet);
        }
        painter.fill_rect({4.0, 4.0, 12.0, 12.0}, Color::rgba(200, 30, 30, 160));
    }
};

// A surface of `width` x `height` opaque pixels in a gradient, marked
// opaque or not.
std::shared_ptr<LiveSurface> make_surface(std::uint32_t width, std::uint32_t height, bool opaque) {
    LiveSurfaceDescription description{};
    description.width = width;
    description.height = height;
    description.pixel_format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
    description.opaque = opaque;
    std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
    require(static_cast<bool>(surface), "surface not created");
    LiveSurfaceWriteLease write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "surface write refused");
    for (std::uint32_t y = 0U; y < height; ++y) {
        std::byte* row = write.pixels().data() + static_cast<std::size_t>(y) * write.row_bytes();
        for (std::uint32_t x = 0U; x < width; ++x) {
            row[x * 4U] = static_cast<std::byte>(x * 255U / width);
            row[x * 4U + 1U] = static_cast<std::byte>(y * 255U / height);
            row[x * 4U + 2U] = std::byte{160};
            row[x * 4U + 3U] = std::byte{255};
        }
    }
    static_cast<void>(write.publish());
    return surface;
}

struct Frame final {
    std::vector<std::byte> pixels;
    std::uint64_t covers_promised{};
};

// Paints the whole window once at `scale` and returns its bytes.
Frame paint_scene(double scale, Rect sheet, bool opaque, bool clipped) {
    const Control::Ptr root = make_control<Control>(StableId("occlusion.root"));
    (*root).set_requested_bounds({0.0, 0.0, window_size.width, window_size.height});
    const std::shared_ptr<SheetCanvas> canvas = make_control<SheetCanvas>(StableId("occlusion.canvas"));
    (*canvas).set_requested_bounds(canvas_bounds);
    (*root).add_child(canvas);
    Window window(root, window_size);
    window.set_scale(scale);
    const std::vector<std::byte> felt = felt_bytes(false);
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(felt_pixels, felt_pixels, felt_pixels * 4U, felt);
    require(static_cast<bool>(loaded), "felt image refused");
    (*canvas).felt = loaded.image;
    (*canvas).sheet = sheet;
    (*canvas).clipped = clipped;
    // Device-sized when the sheet lies on whole device pixels, so the
    // raster copies it; otherwise it is resampled.
    const std::uint32_t width = static_cast<std::uint32_t>(sheet.width * scale);
    const std::uint32_t height = static_cast<std::uint32_t>(sheet.height * scale);
    (*canvas).surface = make_surface(width, height, opaque);
    window.perform_layout();
    SkiaRaster raster;
    require(raster.resize(window_size, scale), "raster resize failed");
    require(raster.synchronize_images(window.image_resources()), "image synchronization failed");
    DamageRegion full;
    full.add({0.0, 0.0, window_size.width, window_size.height});
    raster.begin_frame(full);
    static_cast<void>(window.paint(raster, full.rectangles()));
    raster.end_frame();
    Frame frame;
    frame.covers_promised = render::SkiaCanvasTestAccess::covers_promised(raster);
    const std::byte* bytes = static_cast<const std::byte*>(raster.pixels());
    frame.pixels.assign(bytes, bytes + raster.byte_size());
    return frame;
}

void test_occluded_replay_draws_the_full_replay_bytes() {
    const std::array<double, 2> scales{1.0, 2.0};
    const std::array<Rect, 5> sheets{{
        {20.0, 16.0, 280.0, 160.0},    // inside the canvas, whole pixels
        {20.25, 16.5, 280.0, 160.0},   // off whole pixels
        {-40.0, -30.0, 400.0, 300.0},  // past every edge: covers the canvas
        {100.0, -50.0, 60.0, 120.0},   // across the top edge
        {300.0, 150.0, 80.0, 80.0},    // across the bottom right corner
    }};
    for (const double scale : scales) {
        for (std::size_t index = 0U; index < sheets.size(); ++index) {
            for (const bool clipped : {false, true}) {
                const Frame hidden = paint_scene(scale, sheets[index], true, clipped);
                const Frame shown = paint_scene(scale, sheets[index], false, clipped);
                const std::string where = "scale " + std::to_string(scale) + ", sheet " + std::to_string(index) +
                                          (clipped ? ", clipped" : "");
                require(hidden.pixels == shown.pixels,
                        "occluded replay changed the canvas's bytes (" + where + ")");
                require(hidden.covers_promised > 0U && shown.covers_promised == 0U,
                        "only an opaque sheet may hide what lies beneath it (" + where + ")");
            }
        }
    }
}

// The felt drawn over a canvas at `scale` and `origin` through the kept
// device tile or sampled directly.
std::vector<std::byte> paint_pattern(double scale, Point origin, bool translucent, double opacity, bool direct) {
    const Control::Ptr root = make_control<Control>(StableId("pattern.root"));
    Window window(root, window_size);
    window.set_scale(scale);
    const std::vector<std::byte> felt = felt_bytes(translucent);
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(felt_pixels, felt_pixels, felt_pixels * 4U, felt);
    require(static_cast<bool>(loaded), "felt image refused");
    SkiaRaster raster;
    require(raster.resize(window_size, scale), "raster resize failed");
    require(raster.synchronize_images(window.image_resources()), "image synchronization failed");
    render::SkiaCanvasTestAccess::sample_directly(raster, direct);
    DamageRegion full;
    full.add({0.0, 0.0, window_size.width, window_size.height});
    raster.begin_frame(full);
    raster.fill_rect({0.0, 0.0, window_size.width, window_size.height}, Color::rgba(30, 60, 90));
    // Twice, so the second draw uses the tile the first one kept.
    for (int pass = 0; pass < 2; ++pass) {
        raster.fill_image_pattern(loaded.image, {static_cast<double>(felt_pixels), static_cast<double>(felt_pixels)},
                                  {origin.x, origin.y, 200.5, 150.0}, {24.0, 24.0}, ImagePatternWrap::tile, opacity);
    }
    raster.end_frame();
    const std::byte* bytes = static_cast<const std::byte*>(raster.pixels());
    return std::vector<std::byte>(bytes, bytes + raster.byte_size());
}

// The largest byte difference between two frames of one size.
int largest_difference(const std::vector<std::byte>& first, const std::vector<std::byte>& second) {
    require(first.size() == second.size(), "pattern frames differ in size");
    int largest = 0;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        const int difference = std::abs(std::to_integer<int>(first[index]) - std::to_integer<int>(second[index]));
        largest = std::max(largest, difference);
    }
    return largest;
}

void test_device_tile_matches_the_direct_pattern() {
    const std::array<double, 3> scales{1.0, 2.0, 1.5};
    const std::array<Point, 2> origins{{{10.0, 6.0}, {-3.0, 14.0}}};
    const std::array<bool, 2> translucencies{false, true};
    const std::array<double, 2> opacities{0.75, 1.0};
    for (const double scale : scales) {
        for (const Point origin : origins) {
            for (const bool translucent : translucencies) {
                for (const double opacity : opacities) {
                    const std::vector<std::byte> kept = paint_pattern(scale, origin, translucent, opacity, false);
                    const std::vector<std::byte> direct = paint_pattern(scale, origin, translucent, opacity, true);
                    require(largest_difference(kept, direct) <= 1,
                            "a kept pattern tile differs from the direct draw by more than one level (scale " +
                                std::to_string(scale) + ")");
                }
            }
        }
    }
}

} // namespace

int main() {
    test_occluded_replay_draws_the_full_replay_bytes();
    test_device_tile_matches_the_direct_pattern();
    std::cout << "gui_forms_canvas_occlusion_raster_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
