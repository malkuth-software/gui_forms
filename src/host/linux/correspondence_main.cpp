#include "correspondence.hpp"
#include "gui_forms/platform/linux_host.hpp"
#include "program_options.hpp"

#include <clocale>
#include <memory>
#include <optional>
#include <utility>

int main(int argc, char** argv) {
    // std-rules: allow(locale-change) the program, not the library, chooses its locale: the environment's character type, for X input methods and compose
    std::setlocale(LC_CTYPE, "");
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::LinuxHostOptions options;
    options.title = "GUI.Forms Correspondence";
    options.initial_size = {1120.0, 720.0};
    options.minimum_size = {600.0, 420.0};
    gui_forms::demo::apply_program_options(*program, options);
    // --automation is accepted for a uniform CI command line; the X11 host has
    // no WM_COPYDATA-style automation channel, so the flag has no effect here.
    std::unique_ptr<gui_forms::Window> window = gui_forms::correspondence::make_correspondence();
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_linux(std::move(window), std::move(options)));
}
