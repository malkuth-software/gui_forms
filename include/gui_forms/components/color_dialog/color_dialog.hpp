#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <memory>
#include <string>

namespace gui_forms {

class Window;

// The host's dialog for choosing a color (Windows Forms' ColorDialog). The
// dialog holds its settings and, after the user accepts, the chosen color;
// show_dialog shows it modally over a window through that window's
// HostServices (capability `dialogs`) and returns when it closes. The host
// draws the dialog: ChooseColorW on Windows, an NSColorWell in an alert on
// macOS, and a dialog of GUI.Forms controls on X11.
//
// The title must be valid UTF-8 without NUL of at most
// HostServices::maximum_dialog_text_bytes; anything else throws
// std::invalid_argument and changes nothing. The dialog is used on the UI
// thread of the window it is shown over.
class ColorDialog final : public Component {
public:
    ColorDialog();
    ~ColorDialog() override;

    // The dialog's title; empty gives the host's own. Windows Forms' dialog
    // has none; the hosts that title their dialog use it.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void set_title(GUI_FORMS_LOCALIZABLE(title) std::string title);
    // The chosen color, and before the dialog is shown the one it starts
    // with (Windows Forms' Color; opaque black by default).
    [[nodiscard]] Color color() const noexcept { return color_; }
    void set_color(Color color) noexcept { color_ = color; }
    // Whether the user may choose a translucent color. Off by default, as in
    // Windows Forms, where every chosen color is opaque: the dialog then
    // takes the chosen color as opaque whatever the host returns.
    [[nodiscard]] bool allow_alpha() const noexcept { return allow_alpha_; }
    void set_allow_alpha(bool allow) noexcept { allow_alpha_ = allow; }

    // The host's answer to the last show_dialog.
    [[nodiscard]] HostServiceStatus status() const noexcept { return status_; }

    // Shows the dialog over `owner` and returns when it closes: ok when the
    // user chose a color, cancel when the user cancelled or the dialog was
    // disposed or destroyed while it was showing, and none when the dialog
    // could not be shown, with status() saying why. Only ok changes color.
    // Throws std::logic_error for a disposed dialog, for one already
    // showing, and off the window's UI thread.
    DialogResult show_dialog(Window& owner);

    // Restores every setting to its default.
    void reset();

private:
    // Observed across the host's modal loop, which may run callbacks that
    // destroy the dialog: an expired observer means `this` is gone.
    struct Presence final {};

    std::string title_{};
    Color color_{Color::rgba(0U, 0U, 0U, 255U)};
    bool allow_alpha_{};
    HostServiceStatus status_{};
    bool showing_{};
    std::shared_ptr<Presence> presence_{std::make_shared<Presence>()};
};

} // namespace gui_forms
