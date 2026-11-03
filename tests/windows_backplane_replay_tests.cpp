// Stage T5, phase C6's renderer gate on Windows: the backplane replay scene
// (backplane_replay_scene.hpp) through the Windows host's raster, with the
// bundled fonts loaded as the host loads them. The glass over the replayed
// text must hash as the table says, which Skia checks too
// (backplane_replay_identity_tests). GUI_FORMS_BACKPLANE_REPLAY_PRINT=1
// prints the observed hashes instead of checking them.
//
// It also records the replay's cost: a repeated backplane damage replays
// the whole backplane plane each time; the run prints the work (device
// pixels, replays) and the measured time per replay. The time is a record,
// not a gate.
// The scene (and with it every GUI.Forms header) comes before windows.h,
// whose `near` and `far` macros would otherwise rewrite enumerators.
#include "backplane_replay_scene.hpp"
#include "../src/host/windows/application/windows_host.cpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

using namespace gui_forms;
namespace scene = gui_forms::backplane_replay_scene;

namespace {

// Replays timed for the cost record.
constexpr unsigned cost_replays = 64U;

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: gui_forms_windows_backplane_replay_tests <fonts directory>\n";
        return 2;
    }
    const std::shared_ptr<gui_forms::detail::BundledFontSet> fonts = host::load_bundled_fonts(std::filesystem::path(argv[1]));
    if (!check(fonts != nullptr, "the bundled fonts load")) return 1;
    const char* const print_value = std::getenv("GUI_FORMS_BACKPLANE_REPLAY_PRINT");
    const bool printing = print_value != nullptr && std::strcmp(print_value, "1") == 0;
    bool passed = true;
    for (std::size_t index = 0U; index < scene::scales.size(); ++index) {
        const double scale = scene::scales[index];
        host::WindowsRaster renderer;
        renderer.set_bundled_fonts(fonts);
        const std::unique_ptr<scene::SceneWindow> window = scene::make_scene(renderer.painter(), scale);
        const scene::SceneReading reading = scene::read_scene(*window, scale);
        std::printf("backplane-replay dib %.1f 0x%016llxULL replays=%llu skipped=%llu\n", scale,
                    static_cast<unsigned long long>(reading.hash), static_cast<unsigned long long>(reading.replays),
                    static_cast<unsigned long long>(reading.skipped_commands));
        passed = check(reading.glass_found, "the glass is composited") && passed;
        passed = check(reading.replays == 1U, "the backplane is replayed once") && passed;
        passed = check(reading.skipped_commands == 0U, "the replay skips no command") && passed;
        passed = check(reading.metrics_replay, "the metrics report the replay") && passed;
        if (!printing) passed = check(reading.hash == scene::expected[index], "the glass hash matches the table") && passed;

        // The cost: the same window, its backplane damaged and replayed again.
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (unsigned replay = 0U; replay < cost_replays; ++replay) scene::replay_again(*window);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const scene::SceneReading after = scene::read_scene(*window, scale);
        passed = check(after.replays == 1U + cost_replays, "every backplane damage replays the backplane") && passed;
        passed = check(after.hash == reading.hash, "a replay into the reused framebuffer gives the same glass") &&
                 passed;
        const long long nanoseconds =
            static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        const unsigned long long device_pixels = static_cast<unsigned long long>(scene::window_size.width * scale) *
                                                 static_cast<unsigned long long>(scene::window_size.height * scale);
        std::printf("backplane-replay-cost dib %.1f window_pixels=%llu repaints=%u total_ns=%lld per_repaint_ns=%lld\n",
                    scale, device_pixels, cost_replays, nanoseconds, nanoseconds / static_cast<long long>(cost_replays));
    }
    if (!passed) return 1;
    std::cout << "gui_forms_windows_backplane_replay_tests passed\n";
    return 0;
}
