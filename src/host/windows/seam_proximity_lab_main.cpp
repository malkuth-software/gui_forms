#include "seam_proximity_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Physical Seam + Proximity Lab";
    options.initial_size = {1360.0, 760.0};
    options.minimum_size = {1200.0, 720.0};
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(
        gui_forms::seam_proximity_lab::make_seam_proximity_lab(),
        std::move(options));
}
