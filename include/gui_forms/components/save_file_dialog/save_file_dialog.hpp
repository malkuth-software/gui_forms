#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

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
class SaveFileDialog final : public Component {
public:
    SaveFileDialog();
    ~SaveFileDialog() override;

    // The dialog's title; empty gives the host's own ("Save As").
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void set_title(GUI_FORMS_LOCALIZABLE(title) std::string title);
    // The directory the dialog starts in; empty lets the host choose.
    [[nodiscard]] const std::string& initial_directory() const noexcept { return initial_directory_; }
    void set_initial_directory(std::string directory);

    // The filter in Windows Forms' syntax, as set, and the host filters it
    // parsed into.
    [[nodiscard]] const std::string& filter() const noexcept { return filter_; }
    void set_filter(std::string filter);
    [[nodiscard]] std::span<const HostFileDialogFilter> filters() const noexcept { return filters_; }
    // The filter the dialog starts with, an index into filters() (Windows
    // Forms' FilterIndex, which counts from one; this counts from zero).
    // After the user accepts it is the filter they had selected, where the
    // host tells; an index past the filters starts the dialog on the first.
    [[nodiscard]] std::size_t filter_index() const noexcept { return filter_index_; }
    void set_filter_index(std::size_t index) noexcept { filter_index_ = index; }

    // The extension a chosen name without one is given, without its dot
    // (Windows Forms' DefaultExt; a leading dot is removed when set). An
    // extension holding a wildcard, a path separator or a character Windows
    // forbids in a name throws std::invalid_argument.
    [[nodiscard]] const std::string& default_ext() const noexcept { return default_ext_; }
    void set_default_ext(std::string extension);
    // Whether a chosen name without an extension is given one (Windows
    // Forms' AddExtension; on by default): the chosen filter's first
    // extension, unless that filter is any file, else default_ext.
    [[nodiscard]] bool add_extension() const noexcept { return add_extension_; }
    void set_add_extension(bool add) noexcept { add_extension_ = add; }
    // Whether the host asks before the user chooses a file that exists
    // (Windows Forms' OverwritePrompt; on by default).
    [[nodiscard]] bool overwrite_prompt() const noexcept { return overwrite_prompt_; }
    void set_overwrite_prompt(bool prompt) noexcept { overwrite_prompt_ = prompt; }

    // The chosen path, or empty. Before the dialog is shown, a name set here
    // is the one the dialog suggests.
    [[nodiscard]] const std::string& file_name() const noexcept;
    // Sets the suggested name; it becomes the chosen path (none for an empty
    // name).
    void set_file_name(std::string name);
    // The chosen path as a list of one, or none (Windows Forms' FileNames).
    [[nodiscard]] std::span<const std::string> file_names() const noexcept { return file_names_; }

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

    std::string title_{};
    std::string initial_directory_{};
    std::string filter_{};
    std::vector<HostFileDialogFilter> filters_{};
    std::size_t filter_index_{};
    std::string default_ext_{};
    bool add_extension_{true};
    bool overwrite_prompt_{true};
    std::vector<std::string> file_names_{};
    HostServiceStatus status_{};
    bool showing_{};
    std::shared_ptr<Presence> presence_{std::make_shared<Presence>()};
    Event<FileDialogOk&> file_ok_{};
};

} // namespace gui_forms
