#include "owned_events_form.hpp"   // generated from forms/owned_events.form

#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>

namespace gui_forms::examples {

// Actions owns the subscription. The window continues to own the button;
// this connection adds no visual ownership or scheduler work.
class Actions final : public Component {
public:
    explicit Actions(Button& run) {
        on(run.clicked(), *this, &Actions::on_run);
    }

    [[nodiscard]] unsigned runs() const noexcept { return runs_; }

private:
    void on_run() { ++runs_; }

    unsigned runs_{};
};

} // namespace gui_forms::examples

int main() {
    reference::ExampleWindow window{};   // binds nothing, so it takes no model
    gui_forms::examples::Actions actions(*window.example_run);
    (*window.example_run).perform_click();
    if (actions.runs() != 1U) {
        return 1;
    }
    actions.dispose();
    (*window.example_run).perform_click();
    if (actions.runs() != 1U) {
        return 1;
    }
    return 0;
}
