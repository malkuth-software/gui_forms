// The touch start demonstrator on this host (owner, 2026-11-21;
// demo/touch_start/touch_start.hpp): a panning surface of live tiles,
// with --headless --capture and --check.
#include "gui_forms/platform/linux_host.hpp"
#include "touch_start.hpp"
#include "touch_start_program.hpp"
#include "program_options.hpp"

#include <clocale>
#include <memory>
#include <optional>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    // std-rules: allow(locale-change) the program, not the library, chooses its locale: the environment's character type, for X input methods and compose
    std::setlocale(LC_CTYPE, "");
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::LinuxHostOptions options;
    options.title = std::string(gui_forms::touch_start::program_name);
    options.initial_size = gui_forms::touch_start::window_size;
    options.minimum_size = gui_forms::touch_start::minimum_window_size;
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::touch_start::make_touch_start();
    if (!gui_forms::touch_start::apply_touch_start_staging(*program, *window)) return 2;
    if ((*program).headless) return gui_forms::touch_start::run_headless_touch_start(*program, *window);
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_linux(std::move(window), std::move(options)));
}
