// The Gallery run as an application (Application::run), so that its menus,
// drop-downs and pickers open in warm Popup windows as a program's do: the
// single-window Gallery runner has no application run and shows them as
// overlays. A development probe for tools/menu_probe.py, which drives it
// through the automation channel on a private desktop and captures the
// owner with its Popups composed (capture-composite).
//
//   GUI_FORMS_WINDOWS_AUTOMATION=1 GUI_FORMS_WINDOWS_SCALE=2 \
//       gui_forms_menu_probe_windows [page]
//
// `page` is a Gallery page id, commands-and-menus by default.
#include "gallery.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    const std::unique_ptr<gui_forms::Application> application = gui_forms::gallery::create_gallery_application();
    const std::string page = argc > 1 ? std::string(argv[1]) : std::string("commands-and-menus");
    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    if (!gui_forms::gallery::select_gallery_page(*window, page)) {
        std::fprintf(stderr, "unknown Gallery page: %s\n", page.c_str());
        return 2;
    }
    gui_forms::ApplicationWindowOptions options;
    options.title = "GUI.Forms menu probe";
    options.initial_size = gui_forms::gallery::gallery_default_size;
    options.minimum_size = gui_forms::gallery::gallery_minimum_size;
    const gui_forms::ApplicationResult result = (*application).run(std::move(window), std::move(options));
    const int code = result.error == gui_forms::ApplicationError::none ? result.native_exit_code : 1;
    return code;
}
