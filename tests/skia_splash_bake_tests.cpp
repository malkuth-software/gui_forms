// The Splash baker over the CPU Skia raster (APP_WINDOW_RULES_DESIGN Part S,
// phase 2): a snapshot baked on the worker comes back with the pixels the
// window's own painter would draw, premultiplied BGRA8.

#include "gui_forms/gui_forms.hpp"
#include "../src/core/display/snapshot/display_snapshot.hpp"
#include "../src/core/display/snapshot/splash_baker.hpp"
#include "../src/render/skia/raster/skia_splash_bake.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::detail;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A red rectangle over the left half of a 10 x 4 transparent field.
std::shared_ptr<const DisplaySnapshot> red_half(double scale) {
    DisplayCommand fill{};
    fill.operation = DisplayOperation::fill_rect;
    fill.rect = Rect{0.0, 0.0, 5.0, 4.0};
    fill.color = Color{255, 0, 0, 255};
    std::vector<DisplayCommand> commands;
    commands.push_back(fill);
    DisplayChunk chunk(1U, PaintPlane::overlay, Rect{0.0, 0.0, 10.0, 4.0}, std::move(commands));
    DisplaySnapshotResult made = make_display_snapshot(chunk, Size{10.0, 4.0}, DeviceScale{scale});
    require(made.snapshot != nullptr, "the snapshot is made");
    return made.snapshot;
}

// The BGRA8 pixel at (x, y) of `raster`.
const std::uint8_t* pixel_at(const PartRaster& raster, std::uint32_t x, std::uint32_t y) {
    const std::size_t row_bytes = static_cast<std::size_t>(raster.extent().width) * 4U;
    const std::uint8_t* const pixel =
        raster.pixels().data() + static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(x) * 4U;
    return pixel;
}

void test_direct_bake(double scale) {
    render::SkiaSplashBakeRasterFactory factory(nullptr);
    std::unique_ptr<SplashBakeRaster> raster = factory.make();
    require(raster != nullptr, "the Skia raster is made");
    const std::shared_ptr<const DisplaySnapshot> snapshot = red_half(scale);
    std::shared_ptr<PartRaster> target = PartRaster::create((*snapshot).device_extent(), (*snapshot).scale());
    require(target != nullptr, "the target is made");
    require((*raster).bake(*snapshot, (*target).view()), "the Skia raster bakes the snapshot");
    const std::uint32_t width = (*target).extent().width;
    const std::uint8_t* const red = pixel_at(*target, 1U, 1U);
    require(red[0] == 0U && red[1] == 0U && red[2] == 255U && red[3] == 255U, "the filled half is opaque red");
    const std::uint8_t* const clear = pixel_at(*target, width - 1U, 1U);
    require(clear[3] == 0U, "the rest stays transparent");

    // The reused raster starts each bake clear.
    const std::shared_ptr<const DisplaySnapshot> again = red_half(scale);
    std::shared_ptr<PartRaster> second = PartRaster::create((*again).device_extent(), (*again).scale());
    require((*raster).bake(*again, (*second).view()), "a second bake on the reused raster");
    require(pixel_at(*second, width - 1U, 1U)[3] == 0U, "a reused raster does not carry the last bake");
}

void test_worker_bake() {
    render::SkiaSplashBakeRun run(nullptr);
    const SplashBakerSlot slot = current_splash_baker();
    require(slot.baker != nullptr, "the run installs its baker");
    const SplashBakeTicket ticket = (*slot.baker).submit(red_half(2.0));
    SplashBakeOutcome outcome{};
    for (int attempt = 0; attempt < 2000000; ++attempt) {
        outcome = (*slot.baker).take(ticket);
        if (outcome.state != SplashBakeState::pending) break;
        std::this_thread::yield();
    }
    require(outcome.state == SplashBakeState::finished && outcome.raster != nullptr, "the worker bakes it");
    require((*outcome.raster).extent().width == 20U && (*outcome.raster).extent().height == 8U,
            "the raster is at the snapshot's scale");
    require(pixel_at(*outcome.raster, 2U, 2U)[2] == 255U, "the worker's pixels are the Skia painter's");
}

} // namespace

int main() {
    try {
        test_direct_bake(1.0);
        test_direct_bake(2.0);
        test_worker_bake();
        require(current_splash_baker().baker == nullptr, "the run uninstalls its baker");
    } catch (const std::exception& error) {
        std::cerr << "skia_splash_bake_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "skia_splash_bake_tests: passed\n";
    return EXIT_SUCCESS;
}
