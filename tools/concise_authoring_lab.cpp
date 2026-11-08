#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/window.hpp"
#include "skia_raster.hpp"
#ifndef GUI_FORMS_AUTHORING_BASELINE
#include "gui_forms/commands.hpp"
#include "gui_forms/value.hpp"
#endif

#include <chrono>
#include <fstream>
#include <iostream>

namespace gf = gui_forms;

namespace {
class Fixture final {
public:
    Fixture() {
        root = gf::make_control<gf::Panel>(gf::StableId("root"));
        button = gf::make_control<gf::Button>(gf::StableId("run"), "Run");
        check = gf::make_control<gf::CheckBox>(gf::StableId("check"), "Enabled option");
        slider = gf::make_control<gf::TrackBar>(gf::StableId("volume"));
        (*button).set_bounds({20.0, 20.0, 160.0, 36.0});
        (*check).set_bounds({20.0, 80.0, 200.0, 36.0});
        (*slider).set_bounds({20.0, 140.0, 600.0, 36.0});
        (*root).add_child(button);
        (*root).add_child(check);
        (*root).add_child(slider);
#ifdef GUI_FORMS_AUTHORING_BASELINE
        (*check).checked = true;
        (*slider).value = 40.0;
#else
        command.set_checked(true);
        (*check).bind(command);
        (*slider).bind(volume);
#endif
        window = std::make_unique<gf::Window>(root, gf::Size{1060.0, 618.0});
    }
    std::shared_ptr<gf::Panel> root{};
    std::shared_ptr<gf::Button> button{};
    std::shared_ptr<gf::CheckBox> check{};
    std::shared_ptr<gf::TrackBar> slider{};
    std::unique_ptr<gf::Window> window{};
#ifndef GUI_FORMS_AUTHORING_BASELINE
    gf::Command command{};
    gf::Value<double> volume{40.0};
#endif
};

void paint(Fixture& fixture, gf::render::SkiaRaster& raster) {
    (*fixture.root).invalidate(gf::Dirty::paint);
    gf::DamageRegion damage{};
    damage.add({0.0, 0.0, 1060.0, 618.0});
    raster.begin_frame(damage);
    const std::optional<gf::PaintReceipt> receipt =
        (*fixture.window).paint(raster, {0.0, 0.0, 1060.0, 618.0});
    raster.end_frame();
    if (!receipt || !(*fixture.window).notify_presented(*receipt)) {
        throw std::runtime_error("paint did not produce a valid receipt");
    }
}

std::int64_t elapsed(const std::chrono::steady_clock::time_point start) {
    const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
    const std::chrono::nanoseconds duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);
    const std::int64_t nanoseconds = duration.count();
    return nanoseconds;
}
} // namespace

int main(const int argc, const char* const* argv) {
    if (argc < 2) return 2;
    Fixture fixture{};
    gf::render::SkiaRaster raster{};
    if (!raster.register_typeface_file(gf::FontRole::control, 400U, false, argv[1]) ||
        !raster.resize({1060.0, 618.0}, 1.0)) return 3;
    for (int index = 0; index < 1000; ++index) paint(fixture, raster);
    std::cout << "kind,trial,iterations,total_ns,ns_per_operation\n";
    for (int trial = 0; trial < 7; ++trial) {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (int index = 0; index < 1000; ++index) paint(fixture, raster);
        const std::int64_t paint_time = elapsed(start);
        std::cout << "retained_skia_paint," << trial << ",1000," << paint_time << ','
                  << static_cast<double>(paint_time) / 1000.0 << '\n';
        (*fixture.window).reset_activity_metrics();
        start = std::chrono::steady_clock::now();
        for (int index = 0; index < 1000000; ++index) {
            const gf::FramePollResult poll = (*fixture.window).poll_frame_schedule(gf::FrameTime{});
            if (poll.next_wake || poll.deadlines_fired != 0U || poll.ui_timer_ticks != 0U) return 4;
        }
        const std::int64_t idle_time = elapsed(start);
        std::cout << "idle_poll," << trial << ",1000000," << idle_time << ','
                  << static_cast<double>(idle_time) / 1000000.0 << '\n';
        const gf::MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
        if (metrics.paint_passes != 0U || metrics.scheduler_wakes != 0U || metrics.mutations != 0U) return 5;
    }
    const std::byte* const pixels = static_cast<const std::byte*>(raster.pixels());
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (std::size_t index = 0U; index < raster.byte_size(); ++index) {
        hash ^= std::to_integer<std::uint8_t>(pixels[index]);
        hash *= UINT64_C(1099511628211);
    }
    std::cout << "pixel_hash," << hash << '\n';
    if (argc > 2) {
        // The raster's own capture: the shared owned encoder, unpremultiplied.
        const std::vector<std::byte> png = raster.encode_png();
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!output) return 6;
    }
    return 0;
}
