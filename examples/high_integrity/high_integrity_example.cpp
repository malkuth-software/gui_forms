// The high-integrity configuration's example program (cmake/HighIntegrity.cmake):
// an application with its embedded theme, a window with a text box and a
// button, run on the native host. It is linked as a shipped program is
// (statically, Release, with dead stripping), so its symbols are exactly
// what an ordinary program reaches; gui_forms_high_integrity_surface checks
// them against manifests/high_integrity/absent_symbols.txt. Built only in
// the high-integrity configuration.

#include "high_integrity_example_theme.hpp"  // generated from theme/

#include <gui_forms/application.hpp>
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/controls/text_box.hpp>

#include <iostream>
#include <memory>
#include <utility>

namespace gf = gui_forms;

int main() {
    try {
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        config.theme.packages.push_back(high_integrity_example::high_integrity_example_theme());
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) return 1;
        gf::Application& application = *created.application;
        {
            const std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("example.root"));
            (*root).set_requested_bounds({0.0, 0.0, 360.0, 140.0});
            const std::shared_ptr<gf::TextBox> note =
                gf::make_control<gf::TextBox>(gf::StableId("example.note"), "Type here");
            (*note).set_requested_bounds({20.0, 20.0, 320.0, 28.0});
            (*root).add_child(note);
            const std::shared_ptr<gf::Button> close =
                gf::make_control<gf::Button>(gf::StableId("example.close"), "Close");
            (*close).set_requested_bounds({240.0, 90.0, 100.0, 30.0});
            (*root).add_child(close);
            std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(application, root, gf::Size{360.0, 140.0});
            gf::ApplicationWindowOptions options{};
            options.title = "High-integrity example";
            options.initial_size = {360.0, 140.0};
            const gf::ApplicationResult result = application.run(std::move(window), options);
            if (!result.accepted()) return 1;
        }
        const gf::ApplicationResult ended = gf::Application::shutdown();
        return ended.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
