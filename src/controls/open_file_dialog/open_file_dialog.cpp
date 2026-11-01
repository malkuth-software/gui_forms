#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"

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

OpenFileDialog::OpenFileDialog() = default;

OpenFileDialog::~OpenFileDialog() = default;

void OpenFileDialog::set_title(std::string title) {
    require_dialog_text(title, "OpenFileDialog title must be valid UTF-8 without NUL");
    title_ = std::move(title);
}

void OpenFileDialog::set_initial_directory(std::string directory) {
    require_dialog_text(directory, "OpenFileDialog initial directory must be valid UTF-8 without NUL");
    initial_directory_ = std::move(directory);
}

void OpenFileDialog::set_filter(std::string filter) {
    FileDialogFilterParse parsed = parse_file_dialog_filter(filter);
    if (parsed.error != FileDialogFilterError::none) {
        throw std::invalid_argument("OpenFileDialog filter must be label|patterns pairs of *, *.* or *.extension");
    }
    filter_ = std::move(filter);
    filters_ = std::move(parsed.filters);
}

const std::string& OpenFileDialog::file_name() const noexcept {
    if (file_names_.empty()) return no_file_name;
    return file_names_.front();
}

void OpenFileDialog::set_file_name(std::string name) {
    require_dialog_text(name, "OpenFileDialog file name must be valid UTF-8 without NUL");
    file_names_.clear();
    if (!name.empty()) file_names_.push_back(std::move(name));
}

std::string OpenFileDialog::safe_file_name() const {
    std::string result = file_dialog_name_part(file_name());
    return result;
}

std::vector<std::string> OpenFileDialog::safe_file_names() const {
    std::vector<std::string> result;
    result.reserve(file_names_.size());
    for (const std::string& path : file_names_) {
        result.push_back(file_dialog_name_part(path));
    }
    return result;
}

void OpenFileDialog::reset() {
    title_.clear();
    initial_directory_.clear();
    filter_.clear();
    filters_.clear();
    filter_index_ = 0U;
    multiselect_ = false;
    file_names_.clear();
    status_ = {};
}

HostOpenFileDialogRequest OpenFileDialog::request() const {
    HostOpenFileDialogRequest built;
    built.title = title_;
    built.initial_directory = initial_directory_;
    built.suggested_name = file_name();
    built.filters = filters_;
    built.allow_multiple = multiselect_;
    built.initial_filter = filter_index_ < filters_.size() ? filter_index_ : 0U;
    return built;
}

DialogResult OpenFileDialog::show_dialog(Window& owner) {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot show a disposed OpenFileDialog");
    owner.verify_access("OpenFileDialog::show_dialog");
    if (showing_) throw std::logic_error("GUI.Forms cannot show an OpenFileDialog that is already showing");
    // The host's modal loop and the file_ok handlers may dispose or destroy
    // this dialog and its window; both are observed, and an absent one ends
    // the call without touching it.
    const std::weak_ptr<Presence> presence = presence_;
    const std::weak_ptr<detail::WindowLifetime> window_lifetime = owner.lifetime_;
    const detail::ShowingScope showing(showing_, presence);
    std::string owner_id{};
    const Control::Ptr root = owner.root();
    if (root) owner_id.assign((*root).stable_id().value());
    const HostOpenFileDialogRequest payload = request();
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
        // HostServices has checked that an accepted answer to an open request
        // carries paths, one unless several were allowed.
        const HostPathDialogResult* const paths = std::get_if<HostPathDialogResult>(&answer.payload);
        if (paths == nullptr || (*paths).outcome != HostDialogOutcome::accepted) return DialogResult::cancel;
        if (!is_alive() || !detail::window_alive(window_lifetime)) return DialogResult::cancel;
        FileDialogOk ok{(*paths).paths, false};
        file_ok_.emit(ok);
        if (presence.expired() || !is_alive()) return DialogResult::cancel;
        if (!ok.cancel) {
            file_names_ = (*paths).paths;
            if ((*paths).chosen_filter) filter_index_ = *(*paths).chosen_filter;
            return DialogResult::ok;
        }
        // A cancelled file_ok shows the dialog again, while its window lives.
        if (!detail::window_alive(window_lifetime)) return DialogResult::cancel;
    }
}

namespace {

// The events OpenFileDialog adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> open_file_dialog_interactions{{
    {.name = "file_ok", .payload = "FileDialogOk", .mutable_payload = true},
}};

} // namespace

void OpenFileDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), open_file_dialog_interactions.begin(),
                        open_file_dialog_interactions.end());
}

} // namespace gui_forms
