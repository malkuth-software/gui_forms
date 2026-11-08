// Form documents generated as derived classes (stage B,
// APP_WINDOW_RULES_DESIGN Part B step 2). Gate: gui_forms_add_form MODEL
// generates, at build time, a Window subclass from a form root and a
// subclass of the root's type from a control root; each makes the
// document's objects as its members, sets its values, binds the model's
// sources and, for a window, names its accept and cancel buttons; the
// window joins the current application or the one it is given; a
// released class ends its bindings; a control that binds nothing takes no
// model.

#include "derived_panel_form.hpp"
#include "plain_panel_form.hpp"
#include "sample_dialog_window.hpp"

#include "gui_forms/gui_forms.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The window: its members, values, bindings and buttons; released, its
// controls end their bindings.
void test_window() {
    form_generated::SampleModel model{};
    std::weak_ptr<gui_forms::CheckBox> released{};
    {
        form_generated::SampleDialogWindow window(model);
        require(window.root() == window.sample_root, "window: the document's root control is the window's root");
        require(window.client_size() == gui_forms::Size{480.0, 320.0}, "window: the document's client size");
        require(form_generated::SampleDialogWindow::window_title == "R\303\251glages", "window: the title");
        require(window.accept_button() == window.sample_ok && window.cancel_button() == window.sample_cancel,
                "window: the accept and cancel buttons");
        require(window.application() == nullptr, "window: no current application, none joined");
        const std::span<const gui_forms::Control::Ptr> children = (*window.sample_root).children();
        require(children.size() == 7U && children[0] == window.sample_general &&
                    children[6] == window.sample_level,
                "window: the controls, in document order");
        require(window.sample_save != nullptr, "window: the component, a member");
        const gui_forms::BindingValue padding{gui_forms::Insets{.left = 8.0, .top = 4.0, .right = 8.0, .bottom = 4.0}};
        require((*window.sample_root).property_value("padding") == std::optional<gui_forms::BindingValue>(padding),
                "window: a value set");

        gui_forms::CheckBox& check = *window.sample_general_auto_save;
        require(check.checked(), "window: the check box takes its Value's state");
        check.set_checked(false);
        require(!model.auto_save_.get(), "window: two way, the check box's change reaches the model");
        model.level_.set(40.0);
        require((*window.sample_level).value() == 40.0, "window: one way, the model's change reaches the control");
        static_cast<void>((*window.sample_ok).perform_click());
        static_cast<void>((*window.sample_cancel).perform_click());
        require(model.accept_calls == 1 && model.dismiss_calls == 1, "window: the buttons run the model's commands");
        released = window.sample_general_auto_save;
    }
    require(released.expired(), "window: its controls are released with it");
    model.auto_save_.set(true);
    require(model.auto_save_.get(), "window: the source changes on alone once its control is gone");
}

// The application overload joins the application it names.
void test_window_application() {
    gui_forms::ApplicationCreateResult created = gui_forms::Application::create(gui_forms::ApplicationConfig{});
    require(static_cast<bool>(created), "application: made");
    if (!created) return;
    gui_forms::Application& application = *created.application;
    form_generated::SampleModel model{};
    {
        form_generated::SampleDialogWindow window(application, model);
        require(window.application() == &application, "application: the window joins the one it names");
        form_generated::SampleDialogWindow implicit(model);
        require(implicit.application() == &application, "application: the other joins the current one");
    }
    const gui_forms::ApplicationResult closed = application.close();
    require(closed.accepted(), "application: closed once the windows are gone");
}

// The control: a UserControl made with make(model), its children made once
// it has an owner.
void test_control() {
    form_generated::SampleModel model{};
    const std::shared_ptr<form_generated::DerivedPanel> panel = form_generated::DerivedPanel::make(model);
    require((*panel).stable_id().value() == "panel.root", "control: the document's stable ID");
    const gui_forms::BindingValue padding{gui_forms::Insets{.left = 2.0, .top = 2.0, .right = 2.0, .bottom = 2.0}};
    require((*panel).property_value("padding") == std::optional<gui_forms::BindingValue>(padding),
            "control: its own value set");
    const std::span<const gui_forms::Control::Ptr> children = (*panel).children();
    require(children.size() == 2U && children[0] == (*panel).panel_flag && children[1] == (*panel).panel_go,
            "control: its children, members, in document order");
    if (children.size() != 2U) return;
    require((*(*panel).panel_flag).checked(), "control: the check box takes the model's state");
    model.auto_save_.set(false);
    require(!(*(*panel).panel_flag).checked(), "control: the model's change reaches the check box");
    gui_forms::Window window(panel, gui_forms::Size{200.0, 100.0});
    static_cast<void>((*(*panel).panel_go).perform_click());
    require(model.accept_calls == 1, "control: the button runs the model's command");

    const std::shared_ptr<form_generated::PlainPanel> plain = form_generated::PlainPanel::make();
    require((*plain).children().size() == 1U && (*plain).children()[0] == (*plain).plain_caption,
            "control: binding nothing, it takes no model");
}

} // namespace

int main() {
    test_window();
    test_window_application();
    test_control();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "form derived class tests passed\n";
    return 0;
}
