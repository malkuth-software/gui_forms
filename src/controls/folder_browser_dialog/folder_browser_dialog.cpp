#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp"

#include "../file_dialog/file_dialog_support.hpp"
#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace gui_forms {
namespace {

void require_dialog_text(std::string_view text, const char* message) {
    if (!file_dialog_text_valid(text)) throw std::invalid_argument(message);
}

} // namespace

FolderBrowserDialog::FolderBrowserDialog() = default;

FolderBrowserDialog::~FolderBrowserDialog() = default;

// selected_path is a view onto selected_path_, which an accepted show_dialog
// moves.
std::string FolderBrowserDialog::read_view(const decltype(selected_path)&) const {
    std::string current = selected_path_;
    return current;
}

void FolderBrowserDialog::write_view(const decltype(selected_path)&, std::string incoming) {
    require_dialog_text(incoming, "FolderBrowserDialog selected path must be valid UTF-8 without NUL");
    selected_path_ = std::move(incoming);
}

void FolderBrowserDialog::reset() {
    description = std::string{};
    initial_directory = std::string{};
    selected_path_.clear();
    status_ = {};
}

DialogResult FolderBrowserDialog::show_dialog(Window& owner) {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot show a disposed FolderBrowserDialog");
    owner.verify_access("FolderBrowserDialog::show_dialog");
    if (showing_) throw std::logic_error("GUI.Forms cannot show a FolderBrowserDialog that is already showing");
    const std::weak_ptr<Presence> presence = presence_;
    const detail::ShowingScope showing(showing_, presence);
    HostServices* const services = owner.host_services();
    if (services == nullptr) {
        status_ = {HostServiceError::unsupported};
        return DialogResult::none;
    }
    HostFolderDialogRequest payload;
    payload.title = description.read();
    // A selected folder is where the dialog starts, as in Windows Forms.
    payload.initial_directory = selected_path_.empty() ? initial_directory.read() : selected_path_;
    HostDialogRequest host_request;
    host_request.request_id = detail::take_dialog_request_id();
    const Control::Ptr root = owner.root();
    if (root) host_request.owner_id.assign((*root).stable_id().value());
    host_request.payload = std::move(payload);
    const HostDialogResult answer = (*services).show_dialog(host_request);
    if (presence.expired()) return DialogResult::cancel;
    status_ = answer.status;
    if (!answer.status.accepted()) return DialogResult::none;
    // HostServices has checked that an accepted answer to a folder request
    // carries exactly one path.
    const HostPathDialogResult* const paths = std::get_if<HostPathDialogResult>(&answer.payload);
    if (paths == nullptr || (*paths).outcome != HostDialogOutcome::accepted) return DialogResult::cancel;
    if (!is_alive()) return DialogResult::cancel;
    selected_path_ = (*paths).paths.front();
    return DialogResult::ok;
}

// property_codemod begin FolderBrowserDialog (tools/property_families/folder_browser_dialog.json; generated, do not edit)

void FolderBrowserDialog::prepare_write(const decltype(description)&, std::string& incoming) {
    require_dialog_text(incoming, "FolderBrowserDialog description must be valid UTF-8 without NUL");
}

void FolderBrowserDialog::written(const decltype(description)&) {}

void FolderBrowserDialog::prepare_write(const decltype(initial_directory)&, std::string& incoming) {
    require_dialog_text(incoming, "FolderBrowserDialog initial directory must be valid UTF-8 without NUL");
}

void FolderBrowserDialog::written(const decltype(initial_directory)&) {}

// property_codemod end FolderBrowserDialog

} // namespace gui_forms
