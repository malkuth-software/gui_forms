#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/components/color_dialog/color_dialog_capabilities/color_dialog_capabilities.hpp"

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
class GUI_FORMS_API ColorDialog final : public Component, public ColorDialogCapabilities<ColorDialog> {
public:
    ColorDialog();
    ~ColorDialog() override;

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

    HostServiceStatus status_{};
    bool showing_{};
    std::shared_ptr<Presence> presence_{std::make_shared<Presence>()};
    // property_codemod begin ColorDialog (tools/property_families/color_dialog.json; generated, do not edit)
private:
    friend struct ColorDialogTitleCapability<ColorDialog>;
    friend struct ColorDialogColorCapability<ColorDialog>;
    friend struct ColorDialogAllowAlphaCapability<ColorDialog>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(color)& cell, Color& incoming);
    void written(const decltype(color)& cell);
    void prepare_write(const decltype(allow_alpha)& cell, bool& incoming);
    void written(const decltype(allow_alpha)& cell);
    // property_codemod end ColorDialog
};

} // namespace gui_forms
