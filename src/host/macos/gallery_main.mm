#include "macos_host.hpp"
#include "gallery.hpp"
#include "program_options.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    const std::optional<gui_forms::demo::ProgramOptions> program =
        gui_forms::demo::parse_program_options(argc, argv);
    if (!program) return 2;
    const std::optional<gui_forms::ThemeBase> base = gui_forms::gallery::gallery_theme_base((*program).theme);
    if (!base) {
        std::fprintf(stderr, "unknown Gallery theme: %s\n", (*program).theme.c_str());
        return 2;
    }
    // The program's application before any window (APP_WINDOW_RULES_DESIGN
    // A.5): the Gallery window joins it and takes the theme it sets.
    const std::unique_ptr<gui_forms::Application> application =
        gui_forms::gallery::create_gallery_application(*base);
    gui_forms::host::MacHostOptions options;
    options.title = std::string(gui_forms::gallery::gallery_program_name);
    options.initial_size = gui_forms::gallery::gallery_default_size;
    options.minimum_size = gui_forms::gallery::gallery_minimum_size;
    gui_forms::demo::apply_program_options(*program, options);
    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    if (!(*program).page.empty() && !gui_forms::gallery::select_gallery_page(*window, (*program).page)) {
        std::fprintf(stderr, "unknown Gallery page: %s\n", (*program).page.c_str());
        return 2;
    }
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_macos(std::move(window), std::move(options)));
}
