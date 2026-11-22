// The process-control demonstrator on this host (owner, 2026-11-22;
// demo/process_control/process_control.hpp): the dialog drawn from the
// library's light maps and finishes, with --headless --capture and --check.
#include "gui_forms/platform/linux_host.hpp"
#include "process_control.hpp"
#include "process_control_program.hpp"
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
    options.title = std::string(gui_forms::process_control::program_name);
    options.initial_size = gui_forms::process_control::window_size;
    options.minimum_size = gui_forms::process_control::window_size;
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::process_control::make_process_control();
    if (!gui_forms::process_control::apply_process_control_staging(*program, *window)) return 2;
    if ((*program).headless) return gui_forms::process_control::run_headless_process_control(*program, *window);
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_linux(std::move(window), std::move(options)));
}
