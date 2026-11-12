#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/live_surface/frame/live_surface_frame.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

namespace gui_forms::render {
struct SkiaLiveSurfaceTestAccess final {
    static void reject_next_image(SkiaRaster& raster) {
        raster.fail_next_live_image_ = true;
    }
};
} // namespace gui_forms::render

namespace {
using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::shared_ptr<LiveSurface> make_surface(const bool opaque, const std::byte alpha, const LiveSurfacePixelFormat format) {
    std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 8U, .height = 8U, .pixel_format = format, .opaque = opaque});
    require(static_cast<bool>(surface), "surface created");
    LiveSurfaceWriteLease write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "write lease acquired");
    const std::span<std::byte> pixels = write.pixels();
    for (std::size_t index = 0; index < pixels.size(); index += 4U) {
        pixels[index] = std::byte{0};
        pixels[index + 1U] = std::byte{0};
        pixels[index + 2U] = std::byte{0};
        pixels[index + 3U] = alpha;
    }
    // Blue at full coverage: byte 0 of each BGRA pixel.
    for (std::size_t index = 0U; index < pixels.size(); index += 4U) pixels[index] = alpha;
    require(write.publish() != 0U, "frame published");
    return surface;
}

// The raster's pixel at (x, y), premultiplied BGRA8 in memory, as red,
// green, blue and alpha.
[[nodiscard]] std::array<unsigned, 4> pixel(const render::SkiaRaster& raster, const std::size_t x, const std::size_t y) {
    const std::byte* const bytes = static_cast<const std::byte*>(raster.pixels());
    require(bytes != nullptr, "raster readable");
    const std::size_t offset = y * raster.row_bytes() + x * 4U;
    const std::array<unsigned, 4> result{
        std::to_integer<unsigned>(bytes[offset + 2U]), std::to_integer<unsigned>(bytes[offset + 1U]),
        std::to_integer<unsigned>(bytes[offset]), std::to_integer<unsigned>(bytes[offset + 3U])};
    return result;
}

void blend_cases(const LiveSurfacePixelFormat format) {
    render::SkiaRaster raster{};
    require(raster.resize({8.0, 8.0}, 1.0), "raster allocated");
    DamageRegion damage{};
    damage.add({0.0, 0.0, 8.0, 8.0});
    const std::shared_ptr<LiveSurface> opaque = make_surface(true, std::byte{255}, format);
    const std::shared_ptr<LiveSurface> translucent = make_surface(false, std::byte{128}, format);
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.draw_live_surface(opaque, {0.0, 0.0, 8.0, 8.0}, 1.0);
    raster.end_frame();
    const std::array<unsigned, 4> blue{0U, 0U, 255U, 255U};
    for (std::size_t y = 0; y < 8U; ++y) {
        for (std::size_t x = 0; x < 8U; ++x) require(pixel(raster, x, y) == blue, "opaque pixels exactly replace red");
    }
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.draw_live_surface(translucent, {0.0, 0.0, 8.0, 8.0}, 1.0);
    raster.end_frame();
    const std::array<unsigned, 4> mixed = pixel(raster, 4U, 4U);
    require(mixed[0] == 127U && mixed[2] == 128U && mixed[3] == 255U, "nonopaque surface still source-over blends");
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.draw_live_surface(opaque, {0.0, 0.0, 8.0, 8.0}, 0.5);
    raster.end_frame();
    const std::array<unsigned, 4> faded = pixel(raster, 4U, 4U);
    // Skia RGBA and BGRA source-over kernels differ by one byte in rounding.
    require(faded[0] >= 126U && faded[0] <= 128U && faded[2] >= 127U && faded[2] <= 128U && faded[3] == 255U, "opacity below one still blends opaque producer");
}

void image_failure(const LiveSurfacePixelFormat format) {
    render::SkiaRaster raster{};
    const std::shared_ptr<LiveSurface> surface = make_surface(true, std::byte{255}, format);
    const LiveSurfaceFrame frame = (*surface).acquire_latest();
    DamageRegion damage{};
    damage.add({0.0, 0.0, 8.0, 8.0});
#if defined(GUI_FORMS_PREPARED_TEXT)
    require(raster.begin_prepared_frame({8.0, 8.0}, 1.0, damage) == PreparedTextStatus::success, "initial prepared frame admitted");
#else
    require(raster.resize({8.0, 8.0}, 1.0), "failure raster allocated");
    raster.begin_frame(damage);
#endif
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.end_frame();
#if defined(GUI_FORMS_PREPARED_TEXT)
    const PaintReceipt previous{1U, 1U};
    require(raster.commit_prepared_frame(previous) == PreparedTextStatus::success, "previous complete frame published");
    require(raster.begin_prepared_frame({8.0, 8.0}, 1.0, damage) == PreparedTextStatus::success, "full live candidate admitted");
    require(raster.draw_live_surface_frame(frame, {0.0, 0.0, 4.0, 8.0}, 1.0), "first live fragment succeeds");
#else
    raster.begin_frame(damage);
#endif
    render::SkiaLiveSurfaceTestAccess::reject_next_image(raster);
    const bool drawn = raster.draw_live_surface_frame(frame, {0.0, 0.0, 8.0, 8.0}, 1.0);
    require(!drawn, "image failure reported to host");
    raster.end_frame();
#if defined(GUI_FORMS_PREPARED_TEXT)
    raster.abort_prepared_frame();
    require(raster.prepared_front_receipt() == previous, "failed live composition retains previous receipt");
#endif
    const std::array<unsigned, 4> red{255U, 0U, 0U, 255U};
    require(pixel(raster, 1U, 1U) == red && pixel(raster, 7U, 7U) == red, "failure does not publish cleared or partial pixels");
}

[[nodiscard]] std::shared_ptr<LiveSurface> make_pattern(const LiveSurfacePixelFormat format) {
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 2U, .height = 2U, .pixel_format = format, .opaque = true});
    require(static_cast<bool>(surface), "pattern allocated");
    LiveSurfaceWriteLease write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "pattern write acquired");
    // Red, blue, (17, 63, 129) and (211, 151, 31), opaque, as BGRA bytes.
    const std::array<std::byte, 16> bgra{
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{129}, std::byte{63}, std::byte{17}, std::byte{255},
        std::byte{31}, std::byte{151}, std::byte{211}, std::byte{255}};
    const std::span<std::byte> bytes = write.pixels();
    std::copy(bgra.begin(), bgra.end(), bytes.begin());
    require(write.publish() != 0U, "pattern published");
    return surface;
}

void geometry_and_lease_cases(const LiveSurfacePixelFormat format) {
    const std::shared_ptr<LiveSurface> surface = make_pattern(format);
    const LiveSurfaceFrame frame = (*surface).acquire_latest();
    require((*surface).reconfigure({.width = 2U, .height = 2U, .pixel_format = format}), "reconfigure before old lease draw");
    render::SkiaRaster raster{};
    require(raster.resize({4.0, 3.0}, 2.0), "device-scaled raster allocated");
    DamageRegion damage{};
    damage.add({0.0, 0.0, 4.0, 3.0});
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 4.0, 3.0}, Color::rgba(0, 255, 0));
    raster.translate({0.5, 0.5});
    require(raster.draw_live_surface_frame(frame, {0.5, 0.0, 1.0, 1.0}, 1.0), "old lease copied at device 1:1");
    raster.end_frame();
    const std::array<std::array<unsigned, 4>, 4> expected{{
        {255U, 0U, 0U, 255U}, {0U, 0U, 255U, 255U},
        {17U, 63U, 129U, 255U}, {211U, 151U, 31U, 255U}}};
    const std::array<unsigned, 4> green{0U, 255U, 0U, 255U};
    for (std::size_t y = 0U; y < 6U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            const bool inside = x >= 2U && x < 4U && y >= 1U && y < 3U;
            if (inside) require(pixel(raster, x, y) == expected[(y - 1U) * 2U + x - 2U], "all channels exact after transform and reconfigure");
            else require(pixel(raster, x, y) == green, "copy stays inside destination");
        }
    }
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 4.0, 3.0}, Color::rgba(0, 255, 0));
    raster.clip_rect({0.5, 0.5, 0.5, 0.5});
    require(raster.draw_live_surface_frame(frame, {0.0, 0.0, 1.0, 1.0}, 1.0), "partial rectangular copy");
    raster.end_frame();
    require(pixel(raster, 1U, 1U) == expected[3] && pixel(raster, 0U, 0U) == green, "clip offsets source and preserves outside");

    DamageRegion disjoint{};
    disjoint.add({0.0, 0.0, 0.5, 0.5});
    disjoint.add({0.5, 0.5, 0.5, 0.5});
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 4.0, 3.0}, Color::rgba(0, 255, 0));
    raster.end_frame();
    raster.begin_frame(disjoint);
    require(raster.draw_live_surface_frame(frame, {0.0, 0.0, 1.0, 1.0}, 1.0), "complex clip draw");
    raster.end_frame();
    require(pixel(raster, 0U, 0U) == expected[0] && pixel(raster, 1U, 1U) == expected[3], "complex clip draws admitted pixels");
    require(pixel(raster, 1U, 0U) == green && pixel(raster, 0U, 1U) == green, "complex clip holes preserved");
}

void filtering_and_rounded_clip(const LiveSurfacePixelFormat format) {
    const std::shared_ptr<LiveSurface> pattern = make_pattern(format);
    render::SkiaRaster raster{};
    require(raster.resize({8.0, 8.0}, 1.0), "filter raster allocated");
    DamageRegion damage{};
    damage.add({0.0, 0.0, 8.0, 8.0});
    raster.begin_frame(damage);
    raster.draw_live_surface(pattern, {0.0, 0.0, 4.0, 4.0}, 1.0);
    raster.end_frame();
    const std::array<unsigned, 4> scaled = pixel(raster, 1U, 0U);
    require(scaled[0] > 150U && scaled[0] < 220U && scaled[2] > 30U && scaled[2] < 100U, "scaled draw linearly mixes red and blue");
    raster.begin_frame(damage);
    raster.draw_live_surface(pattern, {0.5, 0.0, 2.0, 2.0}, 1.0);
    raster.end_frame();
    const std::array<unsigned, 4> fractional = pixel(raster, 1U, 0U);
    require(fractional[0] >= 127U && fractional[0] <= 128U && fractional[2] >= 127U && fractional[2] <= 128U, "fractional translation still filters");
    const std::shared_ptr<LiveSurface> solid = make_surface(true, std::byte{255}, format);
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.clip_rounded_rect({0.0, 0.0, 8.0, 8.0}, 4.0);
    raster.draw_live_surface(solid, {0.0, 0.0, 8.0, 8.0}, 1.0);
    raster.end_frame();
    const std::array<unsigned, 4> red{255U, 0U, 0U, 255U};
    const std::array<unsigned, 4> blue{0U, 0U, 255U, 255U};
    require(pixel(raster, 0U, 0U) == red && pixel(raster, 4U, 4U) == blue, "rounded clip preserved");
    const std::array<unsigned, 4> edge = pixel(raster, 2U, 0U);
    require(edge[0] > 0U && edge[2] > 0U, "antialiased clip edge blends coverage");
    // A per-corner rounded clip (EP-6): square top-left and bottom-right
    // corners, the other two rounded by half the side.
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(255, 0, 0));
    raster.save();
    PaintCornerRadii radii{};
    radii.top_right = 4.0;
    radii.bottom_left = 4.0;
    raster.clip_rounded_rect_corners({0.0, 0.0, 8.0, 8.0}, radii);
    raster.fill_rect({0.0, 0.0, 8.0, 8.0}, Color::rgba(0, 0, 255));
    raster.restore();
    raster.end_frame();
    require(pixel(raster, 0U, 0U) == blue && pixel(raster, 7U, 7U) == blue,
            "a per-corner clip keeps its square corners");
    require(pixel(raster, 7U, 0U) == red && pixel(raster, 0U, 7U) == red,
            "a per-corner clip rounds its rounded corners");
    require(pixel(raster, 4U, 4U) == blue, "a per-corner clip keeps its interior");
}

class SolidControl final : public Control {
public:
    SolidControl(StableId id, const Color color) : Control(std::move(id)), color_(color) {}
    void on_paint(Painter& painter, Rect) override {
        painter.fill_rect(client_rectangle(), color_);
    }
private:
    Color color_{};
};

void retained_overlay(const LiveSurfacePixelFormat format) {
    const std::shared_ptr<SolidControl> root = make_control<SolidControl>(StableId("root"), Color::rgba(255, 0, 0));
    const std::shared_ptr<Control> live = make_control<Control>(StableId("live"));
    const std::shared_ptr<SolidControl> overlay = make_control<SolidControl>(StableId("overlay"), Color::rgba(0, 255, 0));
    (*live).set_requested_bounds({0.0, 0.0, 8.0, 8.0});
    (*overlay).set_requested_bounds({2.0, 2.0, 4.0, 4.0});
    (*overlay).set_paint_plane(PaintPlane::overlay);
    (*root).add_child(live);
    (*root).add_child(overlay);
    Window window(root, {8.0, 8.0});
    window.perform_layout();
    const std::shared_ptr<LiveSurface> surface = make_surface(true, std::byte{255}, format);
    require(window.queue_live_surface_presentation(live, surface), "live presentation registered");
    render::SkiaRaster raster{};
    require(raster.resize({8.0, 8.0}, 1.0), "overlay raster allocated");
    DamageRegion damage{};
    damage.add({0.0, 0.0, 8.0, 8.0});
    raster.begin_frame(damage);
    require(window.paint(raster).has_value(), "retained overlay painted");
    const std::vector<LiveSurfacePresentation> updates = window.take_live_surface_presentations();
    require(updates.size() == 4U, "overlay subtracted from live clips");
    for (const LiveSurfacePresentation& update : updates) {
        raster.save();
        raster.clip_rect(update.clip);
        raster.draw_live_surface(update.surface, update.destination, 1.0);
        raster.restore();
    }
    raster.end_frame();
    const std::array<unsigned, 4> green{0U, 255U, 0U, 255U};
    const std::array<unsigned, 4> blue{0U, 0U, 255U, 255U};
    for (std::size_t y = 0; y < 8U; ++y) {
        for (std::size_t x = 0; x < 8U; ++x) {
            const bool covered = x >= 2U && x < 6U && y >= 2U && y < 6U;
            require(pixel(raster, x, y) == (covered ? green : blue), "overlay pixels remain retained; uncovered pixels advance");
        }
    }
}
} // namespace

int main() {
    constexpr LiveSurfacePixelFormat format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
    geometry_and_lease_cases(format);
    filtering_and_rounded_clip(format);
    blend_cases(format);
    image_failure(format);
    retained_overlay(format);
    return 0;
}
