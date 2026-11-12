#include "pages.hpp"
#include "gui_forms/components/color_dialog/color_dialog.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp"
#include "gui_forms/components/message_box/message_box.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"
#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"
#include "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp"
#include "gui_forms/controls/font_dialog.hpp"
#include "gui_forms/controls/task_dialog.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include "dialogs-and-host-services_form.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms::gallery::pages {

// Owns the host-services page: the controls
// demo/pages/dialogs-and-host-services.form describes; one Command per message dialog, path/color
// dialog and host service, each bound to its button; the typed dialog results
// and the last accepted value are page state, not shell state.
class HostServicesPage final : public gui_forms::Component {
public:
    HostServicesPage(const std::shared_ptr<Surface>& body);

private:
    struct MessageCase final {
        std::string_view label;
        HostMessageButtons buttons;
        HostMessageIcon icon;
        MessageBoxDefaultButton default_button;
    };
    static constexpr std::array<MessageCase, 6> message_cases{{
        {"OK", HostMessageButtons::ok,
         HostMessageIcon::information, MessageBoxDefaultButton::button1},
        {"OK/Cancel", HostMessageButtons::ok_cancel,
         HostMessageIcon::warning, MessageBoxDefaultButton::button2},
        {"Yes/No", HostMessageButtons::yes_no,
         HostMessageIcon::question, MessageBoxDefaultButton::button1},
        {"Retry/Cancel", HostMessageButtons::retry_cancel,
         HostMessageIcon::error, MessageBoxDefaultButton::button1},
        {"Abort/Retry/Ignore", HostMessageButtons::abort_retry_ignore,
         HostMessageIcon::error, MessageBoxDefaultButton::button2},
        {"Cancel/Try/Continue", HostMessageButtons::cancel_try_continue,
         HostMessageIcon::warning, MessageBoxDefaultButton::button3},
    }};
    static constexpr std::array<std::string_view, 5> path_labels{
        "Open file", "Open multiple", "Save file", "Choose folder", "Choose color"};
    static constexpr std::array<std::string_view, 5> service_labels{
        "Copy to host", "Paste from host", "Inspect monitors",
        "Completion cue", "Warning cue"};

    void mount_document(const std::shared_ptr<Surface>& body);
    void build_messages();
    void build_paths();
    void build_services();
    void build_font_dialog();
    void on_choose_font();
    void on_font_closed(const FontDialogResult& result);
    void on_font_applied(const FontSpec& font);
    void build_task_dialog();
    void on_show_task_dialog();
    void on_task_dialog_closed(const TaskDialogResult& result);
    void on_native_dialogs_changed(bool native);
    void on_show_exception_dialog();
    void on_exception_dialog_closed(DialogResult result);
    void build_notify_icon();
    void on_toggle_notify_icon();
    void on_notify();
    void on_notify_icon_click(const NotifyIconClick& click);
    void on_notify_icon_menu(const MenuItemInvocation& invocation);

    void on_message_dialog(int index, const CommandInvocation& invocation);
    void on_path_dialog(int index, const CommandInvocation& invocation);
    void show_open_file_dialog(bool multiple);
    void show_save_file_dialog();
    void show_color_dialog();
    void show_folder_browser_dialog();
    void on_service(int index);

    [[nodiscard]] HostServices* services() const;
    void show_dialog(const CommandInvocation& invocation,
                     HostDialogRequestPayload payload);
    void publish(const HostDialogResult& result);
    void publish_accepted(std::string acceptance);
    void publish_cancelled();
    void write_clipboard(HostServices& host);
    void read_clipboard(HostServices& host);
    void inspect_monitors(HostServices& host);
    void play_sound(HostServices& host, HostSoundCue cue);

    std::array<Command, 6> message_commands_{};
    std::array<Command, 5> dialog_commands_{};
    std::array<Command, 5> service_commands_{};
    // Open file, Open multiple, Save file, Choose folder and Choose color
    // show these components.
    std::shared_ptr<OpenFileDialog> open_file_dialog_;
    std::shared_ptr<SaveFileDialog> save_file_dialog_;
    std::shared_ptr<FolderBrowserDialog> folder_browser_dialog_;
    std::shared_ptr<ColorDialog> color_dialog_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<TextBox> clipboard_;
    Command choose_font_{};
    std::shared_ptr<Button> font_button_;
    std::shared_ptr<FontDialog> font_dialog_;
    std::shared_ptr<Label> font_sample_;
    Command task_dialog_command_{};
    std::shared_ptr<Button> task_dialog_button_;
    std::shared_ptr<TaskDialog> task_dialog_;
    // Opens TaskDialog and FontDialog as native popup windows (owner,
    // 2026-11-04: opt-in) instead of overlays.
    std::shared_ptr<CheckBox> native_dialogs_;
    Command exception_dialog_command_{};
    std::shared_ptr<Button> exception_dialog_button_;
    std::shared_ptr<ThreadExceptionDialog> exception_dialog_;
    Command notify_toggle_command_{};
    Command notify_message_command_{};
    std::shared_ptr<Command> notify_menu_command_;
    std::shared_ptr<Button> notify_toggle_button_;
    // Made on first use, when the page's window is known.
    std::shared_ptr<NotifyIcon> notify_icon_;
    std::string last_acceptance_{"No accepted dialog value yet"};
    std::uint64_t next_request_id_{1U};
    // Declared after the Commands its buttons bind, so it is released first.
    HostServicesDocument<HostServicesPage> document_{};
};

HostServicesPage::HostServicesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    build_messages();
    build_paths();
    build_services();
    build_task_dialog();
    build_notify_icon();
}

// The document's root is a declared boundary only: its children move into
// the shell's page body, where each is placed by its bounds in design
// coordinates. The copy of the child list keeps the iteration valid while
// the root shrinks.
void HostServicesPage::mount_document(const std::shared_ptr<Surface>& body) {
    Control& root = *document_.gallery_host_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    status_ = document_.gallery_host_status;
    clipboard_ = document_.gallery_host_clipboard_editor;
    font_button_ = document_.gallery_host_font_choose;
    task_dialog_button_ = document_.gallery_host_task_dialog_show;
    exception_dialog_button_ = document_.gallery_host_exception_dialog_show;
    native_dialogs_ = document_.gallery_host_task_dialog_native;
    notify_toggle_button_ = document_.gallery_host_notify_icon_toggle;
}

// A TaskDialog: an owned dialog built from GUI.Forms controls inside the
// window, the same on every host; beside it, the ThreadExceptionDialog preset
// over a TaskDialog, reporting an exception the specimen throws and catches.
void HostServicesPage::build_task_dialog() {
    task_dialog_command_.set_text("Show task dialog");
    task_dialog_command_.set_description("Show a TaskDialog over this window");
    (*task_dialog_button_).bind(task_dialog_command_);
    exception_dialog_command_.set_text("Show exception dialog");
    exception_dialog_command_.set_description("Report a caught exception in a ThreadExceptionDialog");
    (*exception_dialog_button_).bind(exception_dialog_command_);
    on((*native_dialogs_).checked_changed(), *this, &HostServicesPage::on_native_dialogs_changed);
    task_dialog_ = make_control<TaskDialog>(StableId("gallery.host.task-dialog.dialog"));
    TaskDialogPage page;
    page.caption = "Export evidence";
    page.heading = "Export the evidence bundle?";
    page.text = "The bundle holds the page captures and the control schema. It is written next to the build.";
    page.icon = TaskDialogIcon::information;
    page.buttons = {{.stable_id = "export", .text = "Export"}, {.standard = TaskDialogStandardButton::cancel}};
    page.default_button = "export";
    page.radio_buttons = {{.stable_id = "zip", .text = "One archive", .checked = true},
                          {.stable_id = "folder", .text = "A folder of files"}};
    page.verification_text = "Open the folder afterwards";
    page.expander = TaskDialogExpander{.text = "Captures: 25 pages. Schema: manifests/control-schema.json."};
    page.footnote = "Nothing leaves this machine.";
    page.footnote_icon = TaskDialogIcon::information;
    page.allow_cancel = true;
    (*task_dialog_).set_page(std::move(page));
    on(task_dialog_command_.invoked(), *this, &HostServicesPage::on_show_task_dialog);
    on((*task_dialog_).closed(), *this, &HostServicesPage::on_task_dialog_closed);
    ThreadExceptionReport report =
        describe_exception(std::make_exception_ptr(std::runtime_error("The evidence index is out of date.")));
    report.caption = "GUI.Forms Gallery";
    report.details = "Thrown and caught by this specimen. A C++ exception carries no stack trace, "
                     "so an application supplies its own details.";
    exception_dialog_ = std::make_shared<ThreadExceptionDialog>(
        StableId("gallery.host.exception-dialog.dialog"), std::move(report));
    on(exception_dialog_command_.invoked(), *this, &HostServicesPage::on_show_exception_dialog);
    on((*exception_dialog_).closed(), *this, &HostServicesPage::on_exception_dialog_closed);
}

void HostServicesPage::on_show_exception_dialog() {
    if (!(*exception_dialog_).show(exception_dialog_button_)) return;
    (*status_).text = "ThreadExceptionDialog shown · Continue, Quit or Escape closes it";
}

void HostServicesPage::on_exception_dialog_closed(DialogResult result) {
    // The Gallery keeps running on Quit; an application would close itself.
    const char* const outcome = result == DialogResult::abort ? "Quit · the application would close itself"
                                                              : "Continue · the error is ignored";
    (*status_).text = std::string("ThreadExceptionDialog closed with ") + outcome;
}

void HostServicesPage::on_show_task_dialog() {
    if (!(*task_dialog_).show(task_dialog_button_)) return;
    (*status_).text = "TaskDialog shown · the window behind it takes no input until it closes";
}

// The toggle takes effect at each dialog's next show().
void HostServicesPage::on_native_dialogs_changed(bool native) {
    const DialogSurface surface = native ? DialogSurface::native_popup : DialogSurface::overlay;
    (*task_dialog_).set_surface(surface);
    if (font_dialog_) (*font_dialog_).set_surface(surface);
    (*status_).text = native ? "Task and font dialogs open as native popup windows where the host has them"
                               : "Task and font dialogs open as overlays in this window";
}

void HostServicesPage::on_task_dialog_closed(const TaskDialogResult& result) {
    (*status_).text = "TaskDialog closed with " + result.button_id + " · " +
                        (result.radio_button_id.empty() ? std::string("no choice") : result.radio_button_id) +
                        (result.verification_checked ? " · open afterwards" : "");
}

// The FontDialog specimen, beside the service status: a button opens the
// in-window dialog over the page; OK and Apply restyle the sample label,
// Cancel leaves it.
void HostServicesPage::build_font_dialog() {
    choose_font_.set_text("Choose font...");
    choose_font_.set_description("Open the in-window font dialog");
    (*font_button_).bind(choose_font_);
    font_sample_ = label("gallery.host.font.sample", "Sample text in the chosen font", 12.0, 400,
                         "heading");
    (*document_.gallery_host_services).add_at(font_sample_, {650.0, 142.0, 246.0, 56.0});
    font_dialog_ = make_control<FontDialog>(StableId("gallery.host.font.dialog"));
    (*font_dialog_).font = (*font_sample_).resolved_font();
    on(choose_font_.invoked(), *this, &HostServicesPage::on_choose_font);
    on((*font_dialog_).closed(), *this, &HostServicesPage::on_font_closed);
    on((*font_dialog_).apply(), *this, &HostServicesPage::on_font_applied);
}

void HostServicesPage::on_choose_font() {
    static_cast<void>((*font_dialog_).show(font_button_));
}

void HostServicesPage::on_font_closed(const FontDialogResult& result) {
    if (result.result == DialogResult::ok) (*font_sample_).font = result.font;
}

void HostServicesPage::on_font_applied(const FontSpec& font) {
    (*font_sample_).font = font;
}

namespace {

// A round icon of `side` pixels in the page's accent colour (red 39, green
// 116, blue 184), opaque premultiplied BGRA8.
HostImage round_icon(std::uint32_t side) {
    HostImage image;
    image.width = side;
    image.height = side;
    image.row_bytes = static_cast<std::uint64_t>(side) * 4U;
    image.pixels.assign(static_cast<std::size_t>(image.row_bytes) * side, std::byte{0});
    const double centre = static_cast<double>(side) * 0.5;
    const double radius = centre - 1.0;
    for (std::uint32_t row = 0; row < side; ++row) {
        for (std::uint32_t column = 0; column < side; ++column) {
            const double dx = static_cast<double>(column) + 0.5 - centre;
            const double dy = static_cast<double>(row) + 0.5 - centre;
            if (dx * dx + dy * dy > radius * radius) continue;
            const std::size_t offset = static_cast<std::size_t>(row) * image.row_bytes + column * 4U;
            image.pixels[offset + 0U] = std::byte{184};
            image.pixels[offset + 1U] = std::byte{116};
            image.pixels[offset + 2U] = std::byte{39};
            image.pixels[offset + 3U] = std::byte{255};
        }
    }
    return image;
}

std::string service_result(const HostServiceStatus& status) {
    if (status.accepted()) return "accepted";
    return host_service_error_name(status.error);
}

// The DialogResult values a message box returns, as the page reports them.
std::string_view message_result_name(DialogResult result) {
    if (result == DialogResult::ok) return "OK";
    if (result == DialogResult::cancel) return "Cancel";
    if (result == DialogResult::yes) return "Yes";
    if (result == DialogResult::no) return "No";
    if (result == DialogResult::retry) return "Retry";
    if (result == DialogResult::abort) return "Abort";
    if (result == DialogResult::ignore) return "Ignore";
    if (result == DialogResult::try_again) return "Try Again";
    if (result == DialogResult::continue_) return "Continue";
    return "None";
}

} // namespace

// A NotifyIcon: the page's icon in the notification area, where the host
// has one, with a context menu and notification text.
void HostServicesPage::build_notify_icon() {
    notify_toggle_command_.set_text("Show tray icon");
    notify_message_command_.set_text("Notify");
    (*notify_toggle_button_).bind(notify_toggle_command_);
    (*document_.gallery_host_notify_icon_notify).bind(notify_message_command_);
    notify_menu_command_ = std::make_shared<Command>("gallery.host.notify-icon.hello", "Say hello");
    on(notify_toggle_command_.invoked(), *this, &HostServicesPage::on_toggle_notify_icon);
    on(notify_message_command_.invoked(), *this, &HostServicesPage::on_notify);
}

void HostServicesPage::on_toggle_notify_icon() {
    Window* const window = (*notify_toggle_button_).attached_window();
    if (window == nullptr) return;
    if (!notify_icon_) {
        notify_icon_ = std::make_shared<NotifyIcon>(*window);
        (*notify_icon_).icon = std::vector<HostImage>{round_icon(16U), round_icon(32U), round_icon(48U)};
        (*notify_icon_).text = "GUI.Forms Gallery";
        std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>("gallery.host.notify-icon.menu");
        (*menu).set_items({MenuItemSpec("gallery.host.notify-icon.menu.hello", MenuItemKind::command,
                                        notify_menu_command_)});
        (*notify_icon_).context_menu = menu;
        on((*notify_icon_).click(), *this, &HostServicesPage::on_notify_icon_click);
        on((*notify_icon_).menu_item_invoked(), *this, &HostServicesPage::on_notify_icon_menu);
    }
    const bool show = !(*notify_icon_).visible.read();
    (*notify_icon_).visible = show;
    const HostServiceStatus status = (*notify_icon_).status();
    notify_toggle_command_.set_text(show ? "Hide tray icon" : "Show tray icon");
    (*notify_toggle_button_).text = show ? "Hide tray icon" : "Show tray icon";
    (*status_).text = std::string(show ? "Tray icon shown: " : "Tray icon hidden: ") + service_result(status);
}

void HostServicesPage::on_notify() {
    if (!notify_icon_) {
        (*status_).text = "Show the tray icon first";
        return;
    }
    const HostServiceStatus status = (*notify_icon_).show_balloon_tip(
        "GUI.Forms", "Notification text from the Gallery", HostTrayNotificationIcon::information);
    (*status_).text = "Tray notification: " + service_result(status);
}

void HostServicesPage::on_notify_icon_click(const NotifyIconClick& click) {
    (*status_).text = click.button == PointerButton::primary ? "Tray icon clicked"
                                                               : "Tray icon context gesture";
}

void HostServicesPage::on_notify_icon_menu(const MenuItemInvocation& invocation) {
    (*status_).text = "Tray menu chose " + invocation.item_id;
}

void HostServicesPage::build_messages() {
    const std::array<std::shared_ptr<Button>, 6> buttons{
        document_.gallery_host_message_0, document_.gallery_host_message_1,
        document_.gallery_host_message_2, document_.gallery_host_message_3,
        document_.gallery_host_message_4, document_.gallery_host_message_5};
    for (std::size_t index = 0U; index < message_cases.size(); ++index) {
        Command& command = message_commands_[index];
        command.set_text(std::string(message_cases[index].label));
        command.set_description("Show a modal message dialog through HostServices");
        (*buttons[index]).bind(command);
        on(command.invoked(), *this, &HostServicesPage::on_message_dialog,
           static_cast<int>(index));
    }
}

void HostServicesPage::build_paths() {
    const std::array<std::shared_ptr<Button>, 5> buttons{
        document_.gallery_host_dialog_0, document_.gallery_host_dialog_1,
        document_.gallery_host_dialog_2, document_.gallery_host_dialog_3,
        document_.gallery_host_dialog_4};
    for (std::size_t index = 0U; index < path_labels.size(); ++index) {
        Command& command = dialog_commands_[index];
        command.set_text(std::string(path_labels[index]));
        command.set_description("Show a typed path or color dialog through HostServices");
        (*buttons[index]).bind(command);
        on(command.invoked(), *this, &HostServicesPage::on_path_dialog,
           static_cast<int>(index));
    }
    open_file_dialog_ = std::make_shared<OpenFileDialog>();
    (*open_file_dialog_).filter = "Text and logs|*.txt;*.log|PNG images|*.png";
    save_file_dialog_ = std::make_shared<SaveFileDialog>();
    (*save_file_dialog_).title = "Save GUI.Forms evidence";
    (*save_file_dialog_).filter = "Text evidence|*.txt";
    (*save_file_dialog_).default_ext = "txt";
    (*save_file_dialog_).file_name = "gui-forms-evidence";
    color_dialog_ = std::make_shared<ColorDialog>();
    (*color_dialog_).title = "Choose an accent color";
    (*color_dialog_).color = Color::rgba(0x27U, 0x74U, 0xB8U);
    folder_browser_dialog_ = std::make_shared<FolderBrowserDialog>();
    (*folder_browser_dialog_).description = "Choose an evidence directory";
}

void HostServicesPage::build_services() {
    const std::array<std::shared_ptr<Button>, 5> buttons{
        document_.gallery_host_service_0, document_.gallery_host_service_1,
        document_.gallery_host_service_2, document_.gallery_host_service_3,
        document_.gallery_host_service_4};
    for (std::size_t index = 0U; index < service_labels.size(); ++index) {
        Command& command = service_commands_[index];
        command.set_text(std::string(service_labels[index]));
        command.set_description("Call one portable HostServices operation");
        (*buttons[index]).bind(command);
        on(command.invoked(), *this, &HostServicesPage::on_service,
           static_cast<int>(index));
    }
    build_font_dialog();
}

HostServices* HostServicesPage::services() const {
    Window* const window = (*status_).attached_window();
    return window == nullptr ? nullptr : (*window).host_services();
}

// Each message case goes through show_message_box, the MessageBox.Show
// facade over the host's message dialog.
void HostServicesPage::on_message_dialog(const int index, const CommandInvocation&) {
    const MessageCase& value = message_cases[static_cast<std::size_t>(index)];
    Window* const window = (*status_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Host rejected request · unsupported";
        return;
    }
    MessageBoxRequest request;
    request.caption = "GUI.Forms message contract";
    request.text = "This modal result travels through show_message_box and the portable HostServices boundary.";
    request.buttons = value.buttons;
    request.icon = value.icon;
    request.default_button = value.default_button;
    const MessageBoxResult result = show_message_box(*window, request);
    if (!result.shown()) {
        (*status_).text = std::string("Host rejected request · ") + host_service_error_name(result.error());
        return;
    }
    publish_accepted(std::string("MessageBox ") + std::string(message_result_name(result.result())));
}

void HostServicesPage::on_path_dialog(const int index,
                                      const CommandInvocation&) {
    if (index <= 1) {
        show_open_file_dialog(index == 1);
    } else if (index == 2) {
        show_save_file_dialog();
    } else if (index == 3) {
        show_folder_browser_dialog();
    } else {
        show_color_dialog();
    }
}

void HostServicesPage::show_open_file_dialog(const bool multiple) {
    Window* const window = (*status_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Host rejected request · unsupported";
        return;
    }
    (*open_file_dialog_).title = multiple ? "Open fixture set" : "Open one fixture";
    (*open_file_dialog_).multiselect = multiple;
    const DialogResult result = (*open_file_dialog_).show_dialog(*window);
    if (result == DialogResult::none) {
        (*status_).text = std::string("Host rejected request · ") +
                            host_service_error_name((*open_file_dialog_).status().error);
        return;
    }
    if (result != DialogResult::ok) {
        publish_cancelled();
        return;
    }
    const std::span<const std::string> names = (*open_file_dialog_).file_names();
    publish_accepted(std::to_string(names.size()) + " path" + (names.size() == 1U ? "" : "s") + " · " +
                     names.front());
}

void HostServicesPage::show_save_file_dialog() {
    Window* const window = (*status_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Host rejected request · unsupported";
        return;
    }
    const DialogResult result = (*save_file_dialog_).show_dialog(*window);
    if (result == DialogResult::none) {
        (*status_).text = std::string("Host rejected request · ") +
                            host_service_error_name((*save_file_dialog_).status().error);
        return;
    }
    if (result != DialogResult::ok) {
        publish_cancelled();
        return;
    }
    publish_accepted("1 path · " + (*save_file_dialog_).file_name.read());
}

void HostServicesPage::show_color_dialog() {
    Window* const window = (*status_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Host rejected request · unsupported";
        return;
    }
    const DialogResult result = (*color_dialog_).show_dialog(*window);
    if (result == DialogResult::none) {
        (*status_).text = std::string("Host rejected request · ") +
                            host_service_error_name((*color_dialog_).status().error);
        return;
    }
    if (result != DialogResult::ok) {
        publish_cancelled();
        return;
    }
    // The colour as the host contract spells it, 0xRRGGBBAA.
    const Color chosen = (*color_dialog_).color.read();
    const std::uint32_t rgba = (static_cast<std::uint32_t>(chosen.red) << 24U) |
                               (static_cast<std::uint32_t>(chosen.green) << 16U) |
                               (static_cast<std::uint32_t>(chosen.blue) << 8U) |
                               static_cast<std::uint32_t>(chosen.alpha);
    std::array<char, 16> encoded{};
    std::snprintf(encoded.data(), encoded.size(), "#%08X", rgba);
    publish_accepted(std::string("color ") + encoded.data());
}

void HostServicesPage::show_folder_browser_dialog() {
    Window* const window = (*status_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Host rejected request · unsupported";
        return;
    }
    const DialogResult result = (*folder_browser_dialog_).show_dialog(*window);
    if (result == DialogResult::none) {
        (*status_).text = std::string("Host rejected request · ") +
                            host_service_error_name((*folder_browser_dialog_).status().error);
        return;
    }
    if (result != DialogResult::ok) {
        publish_cancelled();
        return;
    }
    publish_accepted("1 path · " + (*folder_browser_dialog_).selected_path.read());
}

void HostServicesPage::on_service(const int index) {
    HostServices* const host = services();
    if (host == nullptr) {
        (*status_).text =
            "Host service unavailable · portable control remains responsive";
        return;
    }
    switch (index) {
    case 0: write_clipboard(*host); break;
    case 1: read_clipboard(*host); break;
    case 2: inspect_monitors(*host); break;
    case 3: play_sound(*host, HostSoundCue::operation_complete); break;
    default: play_sound(*host, HostSoundCue::warning); break;
    }
}

void HostServicesPage::show_dialog(const CommandInvocation& invocation,
                                   HostDialogRequestPayload payload) {
    HostServices* const host = services();
    if (host == nullptr) {
        HostDialogResult unavailable;
        unavailable.status.error = HostServiceError::unsupported;
        publish(unavailable);
        return;
    }
    HostDialogRequest request;
    request.request_id = next_request_id_++;
    request.owner_id = invocation.source_id;
    request.payload = std::move(payload);
    publish((*host).show_dialog(request));
}

void HostServicesPage::publish(const HostDialogResult& result) {
    if (!result.status.accepted()) {
        (*status_).text = std::string("Host rejected request · ") +
                            host_service_error_name(result.status.error);
        return;
    }
    if (const HostPathDialogResult* path =
            std::get_if<HostPathDialogResult>(&result.payload)) {
        if ((*path).outcome != HostDialogOutcome::accepted || (*path).paths.empty()) {
            publish_cancelled();
            return;
        }
        publish_accepted(std::to_string((*path).paths.size()) + " path" +
                         ((*path).paths.size() == 1U ? "" : "s") + " · " +
                         (*path).paths.front());
        return;
    }
    if (const HostColorDialogResult* color =
            std::get_if<HostColorDialogResult>(&result.payload)) {
        if ((*color).outcome != HostDialogOutcome::accepted) {
            publish_cancelled();
            return;
        }
        char encoded[16]{};
        std::snprintf(encoded, sizeof(encoded), "#%08X", (*color).rgba);
        publish_accepted(std::string("color ") + encoded);
    }
}

void HostServicesPage::publish_accepted(std::string acceptance) {
    last_acceptance_ = std::move(acceptance);
    (*status_).text = "Accepted · " + last_acceptance_;
}

void HostServicesPage::publish_cancelled() {
    (*status_).text = "Cancelled · preserved: " + last_acceptance_;
}

void HostServicesPage::write_clipboard(HostServices& host) {
    const HostServiceStatus result = host.write_clipboard_text((*clipboard_).text.read());
    (*status_).text = std::string("Clipboard write · ") +
                        host_service_error_name(result.error);
}

void HostServicesPage::read_clipboard(HostServices& host) {
    const HostClipboardTextResult result = host.read_clipboard_text();
    if (result.status.accepted() && result.has_text) {
        (*clipboard_).text = result.text_utf8;
    }
    (*status_).text = std::string("Clipboard read · ") +
                        host_service_error_name(result.status.error) +
                        (result.has_text ? " · text restored" : " · empty");
}

void HostServicesPage::inspect_monitors(HostServices& host) {
    const HostMonitorResult result = host.query_monitors();
    if (!result.status.accepted() || result.monitors.empty()) {
        (*status_).text = std::string("Monitor query · ") +
                            host_service_error_name(result.status.error);
        return;
    }
    const HostMonitor* primary = &result.monitors.front();
    for (const HostMonitor& monitor : result.monitors) {
        if (monitor.primary) {
            primary = &monitor;
            break;
        }
    }
    (*status_).text = "Monitors " + std::to_string(result.monitors.size()) +
                        " · primary " + (*primary).id + " · scale " +
                        std::to_string((*primary).scale);
}

void HostServicesPage::play_sound(HostServices& host, const HostSoundCue cue) {
    const std::chrono::steady_clock::duration now =
        std::chrono::steady_clock::now().time_since_epoch();
    const std::chrono::nanoseconds::rep stamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    const HostServiceStatus result =
        host.play_sound_cue({cue, 0.72, static_cast<std::uint64_t>(stamp)});
    (*status_).text = std::string("Sound ") + host_sound_cue_name(cue) + " · " +
                        host_service_error_name(result.error);
}

void add_dialogs_and_host_services(
    const std::shared_ptr<Surface>& page,
    const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<HostServicesPage>(page));
}

} // namespace gui_forms::gallery::pages
