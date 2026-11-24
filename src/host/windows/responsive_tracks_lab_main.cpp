#include "responsive_tracks_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Responsive Tracks Lab";
    options.initial_size = {1450.0, 894.0};
    options.minimum_size = {150.0, 194.0};
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab(),
        std::move(options));
}
