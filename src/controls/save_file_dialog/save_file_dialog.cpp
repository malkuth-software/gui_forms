#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"

#include "../file_dialog/file_dialog_support.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <variant>

namespace gui_forms {
namespace {

// The name file_name returns while no path is chosen.
const std::string no_file_name{};

void require_dialog_text(std::string_view text, const char* message) {
    if (!file_dialog_text_valid(text)) throw std::invalid_argument(message);
}

} // namespace

SaveFileDialog::SaveFileDialog() = default;

SaveFileDialog::~SaveFileDialog() = default;

void SaveFileDialog::set_title(std::string title) {
    require_dialog_text(title, "SaveFileDialog title must be valid UTF-8 without NUL");
    title_ = std::move(title);
}

void SaveFileDialog::set_initial_directory(std::string directory) {
    require_dialog_text(directory, "SaveFileDialog initial directory must be valid UTF-8 without NUL");
    initial_directory_ = std::move(directory);
}

void SaveFileDialog::set_filter(std::string filter) {
    FileDialogFilterParse parsed = parse_file_dialog_filter(filter);
    if (parsed.error != FileDialogFilterError::none) {
        throw std::invalid_argument("SaveFileDialog filter must be label|patterns pairs of *, *.* or *.extension");
    }
    filter_ = std::move(filter);
    filters_ = std::move(parsed.filters);
}

void SaveFileDialog::set_default_ext(std::string extension) {
    require_dialog_text(extension, "SaveFileDialog default extension must be valid UTF-8 without NUL");
    if (!extension.empty() && extension.front() == '.') extension.erase(0U, 1U);
    if (!extension.empty() && !detail::file_dialog_extension_valid(extension)) {
        throw std::invalid_argument("SaveFileDialog default extension may not hold wildcards or separators");
    }
    default_ext_ = std::move(extension);
}

const std::string& SaveFileDialog::file_name() const noexcept {
    if (file_names_.empty()) return no_file_name;
    return file_names_.front();
}

void SaveFileDialog::set_file_name(std::string name) {
    require_dialog_text(name, "SaveFileDialog file name must be valid UTF-8 without NUL");
    file_names_.clear();
    if (!name.empty()) file_names_.push_back(std::move(name));
}

void SaveFileDialog::reset() {
    title_.clear();
    initial_directory_.clear();
    filter_.clear();
    filters_.clear();
    filter_index_ = 0U;
    default_ext_.clear();
    add_extension_ = true;
    overwrite_prompt_ = true;
    file_names_.clear();
    status_ = {};
}

HostSaveFileDialogRequest SaveFileDialog::request() const {
    HostSaveFileDialogRequest built;
    built.title = title_;
    built.initial_directory = initial_directory_;
    built.suggested_name = file_name();
    // The hosts add the default extension themselves, so it is sent only
    // when one is to be added.
    if (add_extension_) built.default_extension = default_ext_;
    built.filters = filters_;
    built.confirm_overwrite = overwrite_prompt_;
    built.initial_filter = filter_index_ < filters_.size() ? filter_index_ : 0U;
    return built;
}

// The extension a chosen name without one is given: the chosen filter's
// first extension, as Windows Forms takes it, unless that filter is any file
// or the host did not tell which was chosen; then default_ext. The view
// borrows the dialog's own settings.
std::string_view SaveFileDialog::added_extension(std::optional<std::size_t> chosen_filter) const noexcept {
    if (chosen_filter && *chosen_filter < filters_.size()) {
        const HostFileDialogFilter& filter = filters_[*chosen_filter];
        const bool concrete = !filter.extensions.empty() && filter.extensions.front() != "*" &&
                              detail::file_dialog_extension_valid(filter.extensions.front());
        if (concrete) return filter.extensions.front();
    }
    return default_ext_;
}

DialogResult SaveFileDialog::show_dialog(Window& owner) {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot show a disposed SaveFileDialog");
    owner.verify_access("SaveFileDialog::show_dialog");
    if (showing_) throw std::logic_error("GUI.Forms cannot show a SaveFileDialog that is already showing");
    // The host's modal loop and the file_ok handlers may dispose or destroy
    // this dialog and its window; both are observed, and an absent one ends
    // the call without touching it.
    const std::weak_ptr<Presence> presence = presence_;
    const std::weak_ptr<detail::WindowLifetime> window_lifetime = owner.lifetime_;
    const detail::ShowingScope showing(showing_, presence);
    std::string owner_id{};
    const Control::Ptr root = owner.root();
    if (root) owner_id.assign((*root).stable_id().value());
    const HostSaveFileDialogRequest payload = request();
    while (true) {
        HostServices* const services = owner.host_services();
        if (services == nullptr) {
            status_ = {HostServiceError::unsupported};
            return DialogResult::none;
        }
        HostDialogRequest host_request;
        host_request.request_id = detail::take_dialog_request_id();
        host_request.owner_id = owner_id;
        host_request.payload = payload;
        const HostDialogResult answer = (*services).show_dialog(host_request);
        if (presence.expired()) return DialogResult::cancel;
        status_ = answer.status;
        if (!answer.status.accepted()) return DialogResult::none;
        // HostServices has checked that an accepted answer to a save request
        // carries exactly one path.
        const HostPathDialogResult* const paths = std::get_if<HostPathDialogResult>(&answer.payload);
        if (paths == nullptr || (*paths).outcome != HostDialogOutcome::accepted) return DialogResult::cancel;
        if (!is_alive() || !detail::window_alive(window_lifetime)) return DialogResult::cancel;
        std::vector<std::string> chosen = (*paths).paths;
        std::string& path = chosen.front();
        // A host that has not added the extension (macOS adds it only as an
        // allowed type) leaves it to the dialog; a path the addition would
        // make too long for the dialog contract is kept as chosen.
        const std::string_view extension = added_extension((*paths).chosen_filter);
        if (add_extension_ && !extension.empty() && !detail::file_dialog_has_extension(path)) {
            std::string extended = path;
            extended.push_back('.');
            extended.append(extension);
            if (file_dialog_text_valid(extended)) path = std::move(extended);
        }
        FileDialogOk ok{chosen, false};
        file_ok_.emit(ok);
        if (presence.expired() || !is_alive()) return DialogResult::cancel;
        if (!ok.cancel) {
            file_names_ = std::move(chosen);
            if ((*paths).chosen_filter) filter_index_ = *(*paths).chosen_filter;
            return DialogResult::ok;
        }
        // A cancelled file_ok shows the dialog again, while its window lives.
        if (!detail::window_alive(window_lifetime)) return DialogResult::cancel;
    }
}

namespace {

// The events SaveFileDialog adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> save_file_dialog_interactions{{
    {.name = "file_ok", .payload = "FileDialogOk", .mutable_payload = true},
}};

} // namespace

void SaveFileDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), save_file_dialog_interactions.begin(),
                        save_file_dialog_interactions.end());
}

} // namespace gui_forms
