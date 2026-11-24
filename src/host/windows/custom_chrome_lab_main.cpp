#include "custom_chrome_lab.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <optional>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    gui_forms::host::WindowsHostOptions options;
    options.title = "File Manager — GUI.Forms Custom Chrome Lab";
    options.initial_size = {1120.0, 580.0};
    options.minimum_size = {760.0, 430.0};
    // The retained lab is cross-platform. Win32 currently presents it below a
    // standard native title bar; Win32 full-client caption composition remains
    // explicitly unavailable rather than replacing caption controls with paint.
    gui_forms::demo::apply_program_options(*program, options);
    return gui_forms::host::run_windows(
        gui_forms::custom_chrome_lab::make_custom_chrome_lab(),
        std::move(options));
}
