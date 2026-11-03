// Stage T5, phase C6's renderer gate through the Skia raster: the backplane
// replay scene (backplane_replay_scene.hpp), with Carlito registered as the
// content face, against the table the Windows host's raster also checks
// (windows_backplane_replay_tests). GUI_FORMS_BACKPLANE_REPLAY_PRINT=1
// prints the observed hashes instead of checking them.
#include "gui_forms/gui_forms.hpp"
#include "skia_raster.hpp"
#include "backplane_replay_scene.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

using namespace gui_forms;
namespace scene = gui_forms::backplane_replay_scene;

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    const char* const print_value = std::getenv("GUI_FORMS_BACKPLANE_REPLAY_PRINT");
    const bool printing = print_value != nullptr && std::strcmp(print_value, "1") == 0;
    bool passed = true;
    for (std::size_t index = 0U; index < scene::scales.size(); ++index) {
        const double scale = scene::scales[index];
        render::SkiaRaster renderer{};
        if (!check(renderer.resize(scene::window_size, scale), "the raster is allocated")) return 1;
        if (!check(renderer.register_typeface_file(FontRole::content, 400, false, GUI_FORMS_TEST_CONTENT_FONT),
                   "the content face is registered")) {
            return 1;
        }
        const std::unique_ptr<scene::SceneWindow> window = scene::make_scene(renderer, scale);
        const scene::SceneReading reading = scene::read_scene(*window, scale);
        std::printf("backplane-replay skia %.1f 0x%016llxULL replays=%llu skipped=%llu\n", scale,
                    static_cast<unsigned long long>(reading.hash), static_cast<unsigned long long>(reading.replays),
                    static_cast<unsigned long long>(reading.skipped_commands));
        passed = check(reading.glass_found, "the glass is composited") && passed;
        passed = check(reading.replays == 1U, "the backplane is replayed once") && passed;
        passed = check(reading.skipped_commands == 0U, "the replay skips no command") && passed;
        passed = check(reading.metrics_replay, "the metrics report the replay") && passed;
        if (!printing) passed = check(reading.hash == scene::expected[index], "the glass hash matches the table") && passed;
    }
    if (!passed) return 1;
    std::cout << "gui_forms_backplane_replay_identity_tests passed\n";
    return 0;
}
