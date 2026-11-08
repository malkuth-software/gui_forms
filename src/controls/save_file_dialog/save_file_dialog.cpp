#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"

#include "../file_dialog/file_dialog_support.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <variant>

namespace gui_forms {
namespace {

void require_dialog_text(std::string_view text, const char* message) {
    if (!file_dialog_text_valid(text)) throw std::invalid_argument(message);
}

} // namespace

SaveFileDialog::SaveFileDialog() = default;

SaveFileDialog::~SaveFileDialog() = default;

void SaveFileDialog::prepare_write(const decltype(filter)&, std::string& incoming) {
    const FileDialogFilterParse parsed = parse_file_dialog_filter(incoming);
    if (parsed.error != FileDialogFilterError::none) {
        throw std::invalid_argument("SaveFileDialog filter must be label|patterns pairs of *, *.* or *.extension");
    }
}

// The stored filter parsed in prepare_write, so parsing it again succeeds.
void SaveFileDialog::written(const decltype(filter)&) {
    FileDialogFilterParse parsed = parse_file_dialog_filter(filter.read());
    filters_ = std::move(parsed.filters);
}

// file_name is the first chosen path, which show_dialog moves; a write
// makes the chosen paths this one name, even when unchanged.
std::string SaveFileDialog::read_view(const decltype(file_name)&) const {
    if (file_names_.empty()) return std::string{};
    std::string first = file_names_.front();
    return first;
}

void SaveFileDialog::write_view(const decltype(file_name)&, std::string incoming) {
    require_dialog_text(incoming, "SaveFileDialog file name must be valid UTF-8 without NUL");
    file_names_.clear();
    if (!incoming.empty()) file_names_.push_back(std::move(incoming));
}

void SaveFileDialog::reset() {
    title = std::string{};
    initial_directory = std::string{};
    filter = std::string{};
    filter_index = std::size_t{0U};
    default_ext = std::string{};
    add_extension = true;
    overwrite_prompt = true;
    file_names_.clear();
    status_ = {};
}

HostSaveFileDialogRequest SaveFileDialog::request() const {
    HostSaveFileDialogRequest built;
    built.title = title.read();
    built.initial_directory = initial_directory.read();
    built.suggested_name = file_name.read();
    // The hosts add the default extension themselves, so it is sent only
    // when one is to be added.
    if (add_extension.read()) built.default_extension = default_ext.read();
    built.filters = filters_;
    built.confirm_overwrite = overwrite_prompt.read();
    built.initial_filter = filter_index.read() < filters_.size() ? filter_index.read() : 0U;
    return built;
}

// The extension a chosen name without one is given: the chosen filter's
// first extension, as Windows Forms takes it, unless that filter is any file
// or the host did not tell which was chosen; then default_ext. The view
// borrows the dialog's own settings.
std::string_view SaveFileDialog::added_extension(std::optional<std::size_t> chosen_filter) const noexcept {
    if (chosen_filter && *chosen_filter < filters_.size()) {
        const HostFileDialogFilter& offered = filters_[*chosen_filter];
        const bool concrete = !offered.extensions.empty() && offered.extensions.front() != "*" &&
                              detail::file_dialog_extension_valid(offered.extensions.front());
        if (concrete) return offered.extensions.front();
    }
    return default_ext.read();
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
        if (add_extension.read() && !extension.empty() && !detail::file_dialog_has_extension(path)) {
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
            if ((*paths).chosen_filter) filter_index = *(*paths).chosen_filter;
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

// property_codemod begin SaveFileDialog (tools/property_families/save_file_dialog.json; generated, do not edit)

void SaveFileDialog::prepare_write(const decltype(title)&, std::string& incoming) {
    require_dialog_text(incoming, "SaveFileDialog title must be valid UTF-8 without NUL");
}

void SaveFileDialog::written(const decltype(title)&) {}

void SaveFileDialog::prepare_write(const decltype(initial_directory)&, std::string& incoming) {
    require_dialog_text(incoming, "SaveFileDialog initial directory must be valid UTF-8 without NUL");
}

void SaveFileDialog::written(const decltype(initial_directory)&) {}

void SaveFileDialog::prepare_write(const decltype(filter_index)&, std::size_t&) {}

void SaveFileDialog::written(const decltype(filter_index)&) {}

void SaveFileDialog::prepare_write(const decltype(default_ext)&, std::string& incoming) {
    require_dialog_text(incoming, "SaveFileDialog default extension must be valid UTF-8 without NUL");
    if (!incoming.empty() && incoming.front() == '.') incoming.erase(0U, 1U);
    if (!incoming.empty() && !detail::file_dialog_extension_valid(incoming)) {
        throw std::invalid_argument("SaveFileDialog default extension may not hold wildcards or separators");
    }
}

void SaveFileDialog::written(const decltype(default_ext)&) {}

void SaveFileDialog::prepare_write(const decltype(add_extension)&, bool&) {}

void SaveFileDialog::written(const decltype(add_extension)&) {}

void SaveFileDialog::prepare_write(const decltype(overwrite_prompt)&, bool&) {}

void SaveFileDialog::written(const decltype(overwrite_prompt)&) {}

// property_codemod end SaveFileDialog

} // namespace gui_forms
