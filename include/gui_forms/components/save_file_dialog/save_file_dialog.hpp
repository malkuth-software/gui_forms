#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/components/save_file_dialog/save_file_dialog_capabilities/save_file_dialog_capabilities.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Window;

// The host's dialog for choosing where to save a file (Windows Forms'
// SaveFileDialog). The dialog holds its settings and, after the user
// accepts, the chosen path; show_dialog shows it modally over a window
// through that window's HostServices (capability `dialogs`) and returns
// when it closes. The host draws the dialog: GetSaveFileNameW on Windows,
// NSSavePanel on macOS, and a dialog of GUI.Forms controls on X11.
//
// The settings are validated when set, as OpenFileDialog's are: text must
// be valid UTF-8 without NUL of at most HostServices::maximum_dialog_text_bytes,
// and the filter must parse (parse_file_dialog_filter); anything else throws
// std::invalid_argument and changes nothing. The dialog is used on the UI
// thread of the window it is shown over.
class GUI_FORMS_API SaveFileDialog final : public Component, public SaveFileDialogCapabilities<SaveFileDialog> {
public:
    SaveFileDialog();
    ~SaveFileDialog() override;

    // The host filters parsed from filter.
    [[nodiscard]] std::span<const HostFileDialogFilter> filters() const& noexcept { return filters_; }
    std::span<const HostFileDialogFilter> filters() const&& = delete;

    // The chosen path as a list of one, or none (Windows Forms' FileNames).
    [[nodiscard]] std::span<const std::string> file_names() const& noexcept { return file_names_; }
    std::span<const std::string> file_names() const&& = delete;

    // The host's answer to the last show_dialog.
    [[nodiscard]] HostServiceStatus status() const noexcept { return status_; }

    // Shows the dialog over `owner` and returns when it closes: ok when the
    // user accepted (and no file_ok handler cancelled), cancel when the user
    // cancelled, and none when the dialog could not be shown, with status()
    // saying why. Only ok changes file_name. A file_ok handler that cancels
    // shows the dialog again, or returns cancel if it also destroyed
    // `owner`; one that disposes or destroys the dialog ends the call with
    // cancel. Throws std::logic_error for a disposed dialog, for one already
    // showing, and off the window's UI thread.
    DialogResult show_dialog(Window& owner);

    // Restores every setting to its default and forgets the chosen path.
    void reset();

    // Raised when the user accepts, before the dialog takes the path; the
    // payload holds the path with default_ext already added.
    [[nodiscard]] Event<FileDialogOk&>& file_ok() noexcept { return file_ok_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // Observed across the host's modal loop and the file_ok handlers, which
    // may destroy the dialog: an expired observer means `this` is gone.
    struct Presence final {};

    [[nodiscard]] HostSaveFileDialogRequest request() const;
    [[nodiscard]] std::string_view added_extension(std::optional<std::size_t> chosen_filter) const noexcept;

    std::vector<HostFileDialogFilter> filters_{};
    std::vector<std::string> file_names_{};
    HostServiceStatus status_{};
    bool showing_{};
    std::shared_ptr<Presence> presence_{std::make_shared<Presence>()};
    Event<FileDialogOk&> file_ok_{};
    // property_codemod begin SaveFileDialog (tools/property_families/save_file_dialog.json; generated, do not edit)
private:
    friend struct SaveFileDialogTitleCapability<SaveFileDialog>;
    friend struct SaveFileDialogInitialDirectoryCapability<SaveFileDialog>;
    friend struct SaveFileDialogFilterCapability<SaveFileDialog>;
    friend struct SaveFileDialogFilterIndexCapability<SaveFileDialog>;
    friend struct SaveFileDialogDefaultExtCapability<SaveFileDialog>;
    friend struct SaveFileDialogAddExtensionCapability<SaveFileDialog>;
    friend struct SaveFileDialogOverwritePromptCapability<SaveFileDialog>;
    friend struct SaveFileDialogFileNameCapability<SaveFileDialog>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(initial_directory)& cell, std::string& incoming);
    void written(const decltype(initial_directory)& cell);
    void prepare_write(const decltype(filter)& cell, std::string& incoming);
    void written(const decltype(filter)& cell);
    void prepare_write(const decltype(filter_index)& cell, std::size_t& incoming);
    void written(const decltype(filter_index)& cell);
    void prepare_write(const decltype(default_ext)& cell, std::string& incoming);
    void written(const decltype(default_ext)& cell);
    void prepare_write(const decltype(add_extension)& cell, bool& incoming);
    void written(const decltype(add_extension)& cell);
    void prepare_write(const decltype(overwrite_prompt)& cell, bool& incoming);
    void written(const decltype(overwrite_prompt)& cell);
    void write_view(const decltype(file_name)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(file_name)& cell) const;
    // property_codemod end SaveFileDialog
};

} // namespace gui_forms
