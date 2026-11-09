#include "shared_state_form.hpp"   // generated from forms/shared_state.form

#include <gui_forms/application.hpp>

namespace gf = gui_forms;

int main() {
    // The program's application first: the window joins it.
    gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
    if (!created) return 1;
    gf::Application& application = *created.application;
    reference::SharedModel model{};                    // declared before the window it binds
    reference::SharedWindow window(application, model);
    if (application.window_count() != 1U) return 1;
    (*window.volume_first).value = 60.0;      // state change; no input-only scroll notification
    (*window.run).perform_click();
    if ((*window.volume_second).value.read() != 60.0 || model.volume.get() != 60.0 || model.runs != 1) return 1;
    model.run.set_enabled(false);             // the bound button follows the command
    if ((*window.run).perform_click()) return 1;
    return 0;
}
