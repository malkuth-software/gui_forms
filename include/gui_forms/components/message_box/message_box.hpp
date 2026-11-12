#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace gui_forms {

class Window;

// Which of a message box's buttons is the default, counting from the first
// (Windows Forms' MessageBoxDefaultButton). The buttons are those of
// HostMessageButtons in their order: OK, Cancel; Yes, No, Cancel; Retry,
// Cancel; Abort, Retry, Ignore; Cancel, Try Again, Continue, then Help when
// the box shows it. A default past the last button is the first button, as
// Windows' own message box takes it.
enum class MessageBoxDefaultButton : std::uint8_t { button1, button2, button3, button4 };

// Windows Forms' MessageBoxOptions that GUI.Forms admits, as flags: the text
// aligned to the right, and read right to left. Windows Forms' values, so
// the C# facade passes them through. DefaultDesktopOnly and
// ServiceNotification name Windows desktops and services and are not
// admitted.
enum class MessageBoxOptions : std::uint32_t {
    none = 0U,
    right_align = 0x80000U,
    rtl_reading = 0x100000U,
};

[[nodiscard]] constexpr MessageBoxOptions operator|(MessageBoxOptions left, MessageBoxOptions right) noexcept {
    return static_cast<MessageBoxOptions>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool has_message_box_option(MessageBoxOptions options, MessageBoxOptions option) noexcept {
    return (static_cast<std::uint32_t>(options) & static_cast<std::uint32_t>(option)) != 0U;
}

// The arguments of Windows Forms' MessageBox.Show: the message, the caption
// in the title bar, the buttons, the icon, the default button and the
// options.
struct GUI_FORMS_API MessageBoxRequest final {
    std::string text{};
    std::string caption{};
    HostMessageButtons buttons{HostMessageButtons::ok};
    HostMessageIcon icon{HostMessageIcon::none};
    MessageBoxDefaultButton default_button{MessageBoxDefaultButton::button1};
    MessageBoxOptions options{MessageBoxOptions::none};
    // Windows Forms' displayHelpButton: a Help button after the buttons.
    bool display_help_button{};
};

// A message box that closed: the button the user chose, as Windows Forms'
// DialogResult (none for a Yes/No box the host closed without a choice).
struct GUI_FORMS_API MessageBoxClosed final {
    DialogResult result{DialogResult::none};
};

// A message box that was not shown: the host's refusal, never
// HostServiceError::none.
struct GUI_FORMS_API MessageBoxRefused final {
    HostServiceError error{HostServiceError::unsupported};
};

// What show_message_box returns: either the box closed, with the chosen
// button, or it was not shown, with the reason. Built only through
// `closed` and `refused`.
class GUI_FORMS_API MessageBoxResult final {
public:
    [[nodiscard]] static MessageBoxResult closed(DialogResult result) noexcept;
    // Throws std::invalid_argument for HostServiceError::none, which is not
    // a refusal.
    [[nodiscard]] static MessageBoxResult refused(HostServiceError error);

    // Whether the box was shown and closed.
    [[nodiscard]] bool shown() const noexcept;
    // The chosen button; none when the box was not shown.
    [[nodiscard]] DialogResult result() const noexcept;
    // Why the box was not shown; none when it was shown.
    [[nodiscard]] HostServiceError error() const noexcept;

private:
    using State = std::variant<MessageBoxClosed, MessageBoxRefused>;

    explicit MessageBoxResult(State state) noexcept;

    State state_;
};

// Shows a message box over `owner`, modally, and returns when it closes
// (Windows Forms' MessageBox.Show). The host draws the box (owner decision
// W-Q3): MessageBoxW on Windows, NSAlert on macOS, and a dialog of GUI.Forms
// controls on X11, through the window's HostServices (capability `dialogs`).
// TaskDialog stays the themed, richer dialog drawn in the window.
//
// The result is the chosen button. A box the user closes without choosing
// (Escape or the title bar's close button) returns cancel, or ok for a box
// whose only button is OK, as in Windows Forms; a Yes/No or Abort, Retry,
// Ignore box, which the user cannot close without choosing, returns none
// when the host closes it. The options are presentation: Windows honours
// both; on X11 either aligns the text to the right; macOS lays its alerts
// out in the system's own direction and shows the box as without them.
//
// Help: the Help button, or F1 while the box is open (with or without the
// button; macOS has no F1 help, so there only the button asks), raises the
// owner window's help_requested and then offers the request to its
// HelpProviders (Window::request_help); the box stays open. Its handlers run
// inside the host's modal loop: they may open other windows but cannot
// close the box. When a handler destroys the owner window or disposes a
// provider, later requests skip what is gone and the box still returns its
// result. An exception from a help handler is held and thrown by
// show_message_box once the box has closed.
//
// Without a host or without `dialogs` the
// result is refused, with HostServiceError::unsupported. Throws std::invalid_argument
// for text or a caption that is not valid UTF-8, contains NUL or is longer
// than HostServices::maximum_dialog_text_bytes, and for an unknown button
// set, icon, default button or option; throws std::logic_error off the
// window's UI thread.
GUI_FORMS_API MessageBoxResult show_message_box(Window& owner, const MessageBoxRequest& request);

} // namespace gui_forms
