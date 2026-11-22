#include "typography_scale_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Typography + Scale Lab";
    options.initial_size = {1320.0, 840.0};
    options.minimum_size = {1040.0, 720.0};
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(
        gui_forms::typography_scale_lab::make_typography_scale_lab(),
        std::move(options));
}
