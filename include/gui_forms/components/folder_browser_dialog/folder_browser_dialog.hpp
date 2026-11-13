#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog_capabilities/folder_browser_dialog_capabilities.hpp"

#include <memory>
#include <string>

namespace gui_forms {

class Window;

// The host's dialog for choosing a folder (Windows Forms'
// FolderBrowserDialog). The dialog holds its settings and, after the user
// accepts, the chosen folder; show_dialog shows it modally over a window
// through that window's HostServices (capability `dialogs`) and returns when
// it closes. The host draws the dialog: SHBrowseForFolderW on Windows (the
// description above its tree), NSOpenPanel choosing directories on macOS,
// and a dialog of GUI.Forms controls on X11.
//
// Text settings must be valid UTF-8 without NUL of at most
// HostServices::maximum_dialog_text_bytes; anything else throws
// std::invalid_argument and changes nothing. The dialog is used on the UI
// thread of the window it is shown over.
class GUI_FORMS_API FolderBrowserDialog final : public Component, public FolderBrowserDialogCapabilities<FolderBrowserDialog> {
public:
    FolderBrowserDialog();
    ~FolderBrowserDialog() override;

    // The host's answer to the last show_dialog.
    [[nodiscard]] HostServiceStatus status() const noexcept { return status_; }

    // Shows the dialog over `owner` and returns when it closes: ok when the
    // user chose a folder, cancel when the user cancelled or the dialog was
    // disposed or destroyed while it was showing, and none when the dialog
    // could not be shown, with status() saying why. Only ok changes
    // selected_path. Throws std::logic_error for a disposed dialog, for one
    // already showing, and off the window's UI thread.
    DialogResult show_dialog(Window& owner);

    // Restores every setting to its default and forgets the chosen folder.
    void reset();

private:
    // Observed across the host's modal loop, which may run callbacks that
    // destroy the dialog: an expired observer means `this` is gone.
    struct Presence final {};

    std::string selected_path_{};
    HostServiceStatus status_{};
    bool showing_{};
    std::shared_ptr<Presence> presence_{std::make_shared<Presence>()};
    // property_codemod begin FolderBrowserDialog (tools/property_families/folder_browser_dialog.json; generated, do not edit)
private:
    friend struct FolderBrowserDialogDescriptionCapability<FolderBrowserDialog>;
    friend struct FolderBrowserDialogInitialDirectoryCapability<FolderBrowserDialog>;
    friend struct FolderBrowserDialogSelectedPathCapability<FolderBrowserDialog>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(description)& cell, std::string& incoming);
    void written(const decltype(description)& cell);
    void prepare_write(const decltype(initial_directory)& cell, std::string& incoming);
    void written(const decltype(initial_directory)& cell);
    void write_view(const decltype(selected_path)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(selected_path)& cell) const;
    // property_codemod end FolderBrowserDialog
};

} // namespace gui_forms
