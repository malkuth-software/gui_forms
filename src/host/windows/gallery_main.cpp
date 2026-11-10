#include "gallery.hpp"
#include "windows_host.hpp"
#include "program_options.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
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
    std::string sheet;
    if (!(*program).theme_sheet.empty()) {
        std::ifstream file((*program).theme_sheet, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "cannot read theme sheet: %s\n", (*program).theme_sheet.c_str());
            return 2;
        }
        std::ostringstream text;
        text << file.rdbuf();
        sheet = text.str();
    }
    std::unique_ptr<gui_forms::Application> application;
    try {
        application = gui_forms::gallery::create_gallery_application(*base, sheet, (*program).theme_sheet);
    } catch (const std::runtime_error& refused) {
        std::fprintf(stderr, "theme sheet refused: %s\n", refused.what());
        return 2;
    }
    gui_forms::host::WindowsHostOptions options;
    options.title = std::string(gui_forms::gallery::gallery_program_name);
    options.initial_size = gui_forms::gallery::gallery_default_size;
    options.minimum_size = gui_forms::gallery::gallery_minimum_size;
    gui_forms::demo::apply_program_options(*program, options);
    options.automation_enabled = (*program).automation;
    options.scale_override = (*program).scale_hint;
    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    if (!(*program).page.empty() && !gui_forms::gallery::select_gallery_page(*window, (*program).page)) {
        std::fprintf(stderr, "unknown Gallery page: %s\n", (*program).page.c_str());
        return 2;
    }
    if (!(*program).reveal.empty() && !gui_forms::gallery::reveal_gallery_control(*window, (*program).reveal)) {
        std::fprintf(stderr, "cannot reveal Gallery control: %s\n", (*program).reveal.c_str());
        return 2;
    }
    if (!(*program).open_menu.empty()) {
        const std::string& request = (*program).open_menu;
        const std::size_t slash = request.find('/');
        const std::string control = request.substr(0U, slash);
        const std::string item = slash == std::string::npos ? std::string() : request.substr(slash + 1U);
        if (!gui_forms::gallery::open_gallery_menu(*window, control, item)) {
            std::fprintf(stderr, "cannot open Gallery menu: %s\n", request.c_str());
            return 2;
        }
    }
    if (!(*program).scheme.empty()) {
        gui_forms::PresentationOverrides overrides{};
        overrides.colour_scheme =
            (*program).scheme == "dark" ? gui_forms::ColourScheme::dark : gui_forms::ColourScheme::light;
        (*window).set_presentation_overrides(overrides);
    }
    const std::shared_ptr<gui_forms::demo::SmokeTest> smoke =
        gui_forms::demo::arm_smoke_test(*program, options, *window);
    return gui_forms::demo::smoke_test_result(
        smoke, gui_forms::host::run_windows(std::move(window), std::move(options)));
}
