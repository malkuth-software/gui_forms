#include "visual_inspector_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Visual Inspector Lab";
    options.initial_size = {1160.0, 700.0};
    options.minimum_size = {980.0, 620.0};
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(
        gui_forms::visual_inspector_lab::make_visual_inspector_lab(),
        std::move(options));
}
