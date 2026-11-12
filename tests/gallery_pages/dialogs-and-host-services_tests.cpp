#include "support.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/font_dialog.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/task_dialog.hpp"
#include "gui_forms/controls/text_box.hpp"

namespace gallery_pages_tests {

void test_dialogs_and_host_services(gui_forms::Window& window,
                                    gui_forms::host::HeadlessHostServices& services,
                                    bool& cancel_save) {
    select_page(window, "dialogs-and-host-services");
    const std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.host.status"));
    const std::shared_ptr<gui_forms::TextBox> editor = std::dynamic_pointer_cast<TextBox>(
        window.find("gallery.host.clipboard.editor"));
    require(status && editor,
            "host-services board must expose its public result and clipboard controls");
    for (const char* id : {"gallery.host.message.0", "gallery.host.dialog.0",
                           "gallery.host.service.0"}) {
        const std::shared_ptr<gui_forms::Button> button =
            std::dynamic_pointer_cast<Button>(window.find(id));
        require(button != nullptr && (*button).command_connected(),
                "host-services buttons must bind page-owned Commands");
    }

    click(window, window.find("gallery.host.message.2"));
    require((*status).text.read().find("Accepted · MessageBox Yes") != std::string::npos,
            "message dialog must publish its typed accepted choice");
    click(window, window.find("gallery.host.dialog.1"));
    require((*status).text.read().find("2 paths · /tmp/alpha.txt") != std::string::npos,
            "multi-open must retain all accepted paths through the portable result");
    cancel_save = true;
    click(window, window.find("gallery.host.dialog.2"));
    require((*status).text.read().find("Cancelled · preserved: 2 paths · /tmp/alpha.txt") !=
                std::string::npos,
            "dialog cancellation must not overwrite the last accepted value");
    click(window, window.find("gallery.host.dialog.4"));
    require((*status).text.read().find("#315F89FF") != std::string::npos,
            "color dialog must publish an exact typed RGBA result");

    const std::string clipboard_probe((*editor).text.read());
    click(window, window.find("gallery.host.service.0"));
    (*editor).text = "locally replaced";
    click(window, window.find("gallery.host.service.1"));
    require((*editor).text.read() == clipboard_probe &&
                (*status).text.read().find("text restored") != std::string::npos,
            "clipboard write/read must round-trip UTF-8 through HostServices");
    click(window, window.find("gallery.host.service.2"));
    require((*status).text.read().find("headless.primary") != std::string::npos,
            "monitor inspection must identify the primary host monitor and scale");
    click(window, window.find("gallery.host.service.3"));
    require(services.sound_trace().find("sound=operation_complete") !=
                std::string::npos,
            "gallery must exercise the complete operation sound cue");

    const HostServicesSnapshot snapshot = services.snapshot();
    require(snapshot.dialog_requests == 4U &&
                snapshot.dialog_completions == 4U &&
                snapshot.dialog_cancellations == 1U &&
                snapshot.maximum_modal_depth == 1U && snapshot.modal_depth == 0U &&
                snapshot.clipboard_writes == 1U && snapshot.clipboard_reads == 1U &&
                snapshot.monitor_queries == 1U && snapshot.sound_playbacks == 1U,
            "host-services board must leave exact modal and service accounting");

    const std::shared_ptr<gui_forms::Label> sample = std::dynamic_pointer_cast<Label>(
        window.find("gallery.host.font.sample"));
    click(window, window.find("gallery.host.font.choose"));
    const std::shared_ptr<gui_forms::FontDialog> dialog = std::dynamic_pointer_cast<FontDialog>(
        window.find("gallery.host.font.dialog"));
    require(sample && dialog && (*dialog).is_open() && window.focus_scope_depth() == 1U,
            "the font button opens the in-window dialog");
    (*(*dialog).face_list()).select_index(2U);
    require((*(*dialog).ok_button()).perform_click() && !(*dialog).is_open() &&
                (*sample).resolved_font().role == FontRole::monospace && window.focus_scope_depth() == 0U,
            "OK restyles the sample and closes the dialog");

    // Open file goes through the OpenFileDialog component.
    click(window, window.find("gallery.host.dialog.0"));
    require((*status).text.read().find("1 path · /tmp/alpha.txt") != std::string::npos &&
                services.snapshot().dialog_requests == 5U &&
                services.dialog_trace().find("dialog=open_file") != std::string::npos,
            "the OpenFileDialog specimen publishes its one accepted path");

    // Save file goes through the SaveFileDialog component.
    cancel_save = false;
    click(window, window.find("gallery.host.dialog.2"));
    require((*status).text.read().find("1 path · /tmp/gui-forms-evidence.txt") != std::string::npos &&
                services.snapshot().dialog_requests == 6U,
            "the SaveFileDialog specimen publishes its accepted path");

    // Choose folder goes through the FolderBrowserDialog component.
    click(window, window.find("gallery.host.dialog.3"));
    require((*status).text.read().find("1 path · /tmp/evidence") != std::string::npos &&
                services.snapshot().dialog_requests == 7U,
            "the FolderBrowserDialog specimen publishes its chosen folder");
    // The button sits below the fold of the test window, so it is clicked
    // as a keyboard user activates it.
    const std::shared_ptr<gui_forms::Button> exception_button = std::dynamic_pointer_cast<Button>(
        window.find("gallery.host.exception-dialog.show"));
    require(exception_button && (*exception_button).perform_click(), "the exception button is clicked");
    const std::shared_ptr<gui_forms::TaskDialog> exception_dialog = std::dynamic_pointer_cast<TaskDialog>(
        window.find("gallery.host.exception-dialog.dialog"));
    require(exception_dialog && (*exception_dialog).shown() &&
                (*exception_dialog).page().text == "The evidence index is out of date." &&
                (*status).text.read().find("ThreadExceptionDialog shown") != std::string::npos,
            "the exception button reports the caught exception in a ThreadExceptionDialog");
    require((*exception_dialog).click("quit") && !(*exception_dialog).shown() &&
                (*status).text.read().find("closed with Quit") != std::string::npos,
            "Quit closes the exception dialog and the Gallery keeps running");
    require((*exception_button).perform_click(), "the exception button is clicked again");
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::escape}));
    require(!(*exception_dialog).shown() && (*status).text.read().find("closed with Continue") != std::string::npos,
            "Escape continues past the exception");
}

} // namespace gallery_pages_tests
