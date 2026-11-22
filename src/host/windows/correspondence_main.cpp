#include "correspondence.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <memory>
#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Correspondence";
    options.initial_size = {1120.0, 720.0};
    options.minimum_size = {600.0, 420.0};
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::correspondence::make_correspondence();
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_windows(std::move(window), std::move(options)));
}
