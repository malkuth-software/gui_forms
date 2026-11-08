#include "shared_state_form.hpp"   // generated from forms/shared_state.form

int main() {
    reference::SharedModel model{};           // declared before the window it binds
    reference::SharedWindow window(model);    // no application: a test window
    (*window.volume_first).value = 60.0;      // state change; no input-only scroll notification
    (*window.run).perform_click();
    if ((*window.volume_second).value.read() != 60.0 || model.volume.get() != 60.0 || model.runs != 1) return 1;
    model.run.set_enabled(false);             // the bound button follows the command
    if ((*window.run).perform_click()) return 1;
    return 0;
}
