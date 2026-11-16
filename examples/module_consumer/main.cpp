// A consumer of `import gui_forms;` (cmake/Module.cmake): a window of
// controls laid out, with nothing of GUI.Forms included textually. The
// standard library comes from its own headers, before the import; the
// module brings only GUI.Forms' exported names, and no macro.
//
//   gui_forms_module_consumer    (exit status 0 when the button joins the panel)

#include <iostream>
#include <memory>
#include <string>

import gui_forms;

int main() {
    const std::shared_ptr<gui_forms::Panel> root =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId(std::string("module-consumer")));
    const std::shared_ptr<gui_forms::Button> button =
        gui_forms::make_control<gui_forms::Button>(gui_forms::StableId(std::string("module-consumer.button")));
    (*button).set_requested_bounds({8.0, 8.0, 96.0, 28.0});
    (*root).add_child(button);
    gui_forms::Window window(root, {320.0, 200.0});
    window.perform_layout();
    if ((*button).parent() == nullptr) {
        std::cerr << "FAIL: the button did not join the panel\n";
        return 1;
    }
    return 0;
}
