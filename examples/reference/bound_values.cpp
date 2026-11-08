#include "bound_values_form.hpp"   // generated from forms/bound_values.form

#include <gui_forms/event.hpp>

namespace gf = gui_forms;

class Settings final : public gf::Component {
public:
    void fixed() { ++calls; }
    void toggle(const int index) { selected = index; }
    void choose(const int row, const int choice, gf::ButtonBase&) {
        selected = row * 10 + choice;
    }
    int calls{};
    int selected{};
};

int main() {
    reference::SettingsWindow window{};   // binds nothing, so it takes no model
    Settings settings{};                  // declared after the window, so it ends first
    gf::on((*window.back).clicked(), settings, &Settings::fixed);
    gf::on((*window.option).clicked(), settings, &Settings::toggle, 3);
    gf::on((*window.tile).clicked(), settings, &Settings::choose, 2, 4);
    (*window.back).perform_click();
    (*window.option).perform_click();
    if (settings.calls != 1 || settings.selected != 3) return 1;
    (*window.tile).perform_click();
    if (settings.selected != 24) return 1;
    return 0;
}
