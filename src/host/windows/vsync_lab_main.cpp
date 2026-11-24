#include "gui_forms/platform/windows_host.hpp"
#include "program_options.hpp"
#include "vsync_lab.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms VSync + Waterfall Lab";
    options.initial_size = {1280.0, 780.0};
    options.minimum_size = {1080.0, 680.0};
    options.print_metrics_on_close = true;
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(gui_forms::vsync_lab::make_vsync_lab(),
                                        std::move(options));
}
