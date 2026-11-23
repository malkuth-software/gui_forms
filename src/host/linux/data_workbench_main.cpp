// The data-workbench demonstrator on this host (owner, 2026-11-22;
// demo/data_workbench/data_workbench.hpp): fifty thousand orders on a
// virtual DataGridView with a find panel, a column chooser and layouts.
#include "gui_forms/platform/linux_host.hpp"
#include "data_workbench.hpp"
#include "data_workbench_program.hpp"
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
    options.title = std::string(gui_forms::data_workbench::program_name);
    options.initial_size = gui_forms::data_workbench::window_size;
    options.minimum_size = gui_forms::data_workbench::window_size;
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::data_workbench::make_data_workbench();
    if (!gui_forms::data_workbench::apply_data_workbench_staging(*program)) return 2;
    if ((*program).headless) return gui_forms::data_workbench::run_headless_data_workbench(*program, *window);
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_linux(std::move(window), std::move(options)));
}
