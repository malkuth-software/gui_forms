// The compositor lab on this host (stage T5, phase C8; demo/compositor_lab.hpp).
// --backdrop names the request; each grant is printed as a JSON line.
#include "compositor_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    const std::optional<gui_forms::WindowBackdropRequest> request =
        gui_forms::compositor_lab::parse_backdrop_request((*program).backdrop);
    if (!request.has_value()) {
        std::fprintf(stderr, "--backdrop takes one to three of opaque, transparent, window_material and "
                             "transient_material, comma-separated, opaque only last\n");
        return 2;
    }
    gui_forms::host::WindowsHostOptions options;
    options.title = "GUI.Forms Compositor Lab";
    options.initial_size = gui_forms::compositor_lab::lab_size;
    options.minimum_size = gui_forms::compositor_lab::lab_size;
    gui_forms::demo::apply_program_options(*program, options);
    options.automation_enabled = (*program).automation;
    std::unique_ptr<gui_forms::Window> window = gui_forms::compositor_lab::make_compositor_lab(*request, true);
    gui_forms::compositor_lab::record_when_ready(options, *window);
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(smoke, gui_forms::host::run_windows(std::move(window), std::move(options)));
}
