#include "collection_state_form.hpp"   // generated from forms/collection_state.form

#include <gui_forms/collection_controls.hpp>

namespace gf = gui_forms;

class Actions final : public gf::Component {
public:
    void command(const gf::CommandItem& item) { last_command = item.id; }
    void choice(const int index) { last_choice = index; }
    void section(const int index, const bool expanded) {
        last_section = index;
        section_open = expanded;
    }
    int last_command{-1};
    int last_choice{-1};
    int last_section{-1};
    bool section_open{};
};

int main() {
    gf::Value<int> selection(0);
    gf::Value<bool> expanded(false);
    reference::CollectionsWindow window{};   // binds nothing, so it takes no model
    Actions actions{};                       // declared after the window, so it ends first
    const gf::Control::Ptr help = gf::make_control<gf::Control>(gf::StableId("help.body"));
    (*window.commands).set_items({{.id = 1, .label = "Back"}, {.id = 2, .label = "Help"}});
    (*window.choices).set_items({{.id = 10, .label = "Classic"}, {.id = 20, .label = "Modern"}});
    (*window.choices).bind(selection);
    (*window.sections).set_items({{.id = 1, .label = "Help", .content = help, .expanded = &expanded}});
    gf::on((*window.commands).invoked(), actions, &Actions::command);
    gf::on((*window.choices).changed(), actions, &Actions::choice);
    gf::on((*window.sections).toggled(), actions, &Actions::section);
    (*window.commands).item(1).perform_click();
    selection.set(1);
    expanded.set(true);
    if (actions.last_command != 2 || actions.last_choice != 1 ||
        actions.last_section != 0 || !actions.section_open) return 1;
    // Replacing data preserves the three group subscriptions above.
    (*window.commands).set_items({{.id = 3, .label = "Close"}});
    (*window.commands).item(0).perform_click();
    if (actions.last_command != 3) return 1;
    return 0;
}
