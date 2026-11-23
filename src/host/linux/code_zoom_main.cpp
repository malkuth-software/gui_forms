// The code-zoom demonstrator on this host (owner, 2026-11-22;
// demo/code_zoom/code_zoom.hpp): one of GUI.Forms' own sources zoomed from
// a minimap to a few characters, with --bench and --headless --capture.
#include "code_zoom.hpp"
#include "code_zoom_program.hpp"
#include "gui_forms/platform/linux_host.hpp"
#include "program_options.hpp"

#include <clocale>
#include <memory>
#include <optional>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    // std-rules: allow(locale-change) the program, not the library, chooses its locale: the environment's character type, for X input methods and compose
    std::setlocale(LC_CTYPE, "");
    gui_forms::code_zoom::CodeZoomArguments arguments = gui_forms::code_zoom::take_code_zoom_arguments(argc, argv);
    const std::optional<gui_forms::demo::ProgramOptions> program = gui_forms::demo::parse_program_options(
        static_cast<int>(arguments.passed.size()), arguments.passed.data());
    if (!program) return 2;
    gui_forms::host::LinuxHostOptions options;
    options.title = std::string(gui_forms::code_zoom::program_name);
    options.initial_size = gui_forms::code_zoom::window_size;
    options.minimum_size = gui_forms::code_zoom::minimum_window_size;
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::code_zoom::make_code_zoom();
    if (arguments.bench) return gui_forms::code_zoom::run_code_zoom_bench(*program, *window);
    if ((*program).headless) return gui_forms::code_zoom::run_headless_code_zoom(*program, *window);
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_linux(std::move(window), std::move(options)));
}
