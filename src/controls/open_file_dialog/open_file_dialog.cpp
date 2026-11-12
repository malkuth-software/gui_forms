#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"

#include "../file_dialog/file_dialog_support.hpp"
#include "gui_forms/window.hpp"
#include "../../core/surface_root/access/surface_lifetime_access.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

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

OpenFileDialog::OpenFileDialog() = default;

OpenFileDialog::~OpenFileDialog() = default;

void OpenFileDialog::prepare_write(const decltype(filter)&, std::string& incoming) {
    const FileDialogFilterParse parsed = parse_file_dialog_filter(incoming);
    if (parsed.error != FileDialogFilterError::none) {
        throw std::invalid_argument("OpenFileDialog filter must be label|patterns pairs of *, *.* or *.extension");
    }
}

// The stored filter parsed in prepare_write, so parsing it again succeeds.
void OpenFileDialog::written(const decltype(filter)&) {
    FileDialogFilterParse parsed = parse_file_dialog_filter(filter.read());
    filters_ = std::move(parsed.filters);
}

// file_name is the first chosen path, which show_dialog moves; a write
// makes the chosen paths this one name, even when unchanged.
std::string OpenFileDialog::read_view(const decltype(file_name)&) const {
    if (file_names_.empty()) return std::string{};
    std::string first = file_names_.front();
    return first;
}

void OpenFileDialog::write_view(const decltype(file_name)&, std::string incoming) {
    require_dialog_text(incoming, "OpenFileDialog file name must be valid UTF-8 without NUL");
    file_names_.clear();
    if (!incoming.empty()) file_names_.push_back(std::move(incoming));
}

std::string OpenFileDialog::safe_file_name() const {
    std::string result = file_dialog_name_part(file_name.read());
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
    title = std::string{};
    initial_directory = std::string{};
    filter = std::string{};
    filter_index = std::size_t{0U};
    multiselect = false;
    file_names_.clear();
    status_ = {};
}

HostOpenFileDialogRequest OpenFileDialog::request() const {
    HostOpenFileDialogRequest built;
    built.title = title.read();
    built.initial_directory = initial_directory.read();
    built.suggested_name = file_name.read();
    built.filters = filters_;
    built.allow_multiple = multiselect.read();
    built.initial_filter = filter_index.read() < filters_.size() ? filter_index.read() : 0U;
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
    const std::weak_ptr<detail::WindowLifetime> window_lifetime = detail::SurfaceLifetimeAccess::observe(owner);
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
        // The raise owns the payload's view of the answer's paths.
        const detail::BorrowScope raise;
        const std::span<const std::string> chosen((*paths).paths.data(), (*paths).paths.size());
        FileDialogOk ok{raise.lend_view(chosen), false};
        file_ok_.emit(ok);
        if (presence.expired() || !is_alive()) return DialogResult::cancel;
        if (!ok.cancel) {
            file_names_ = (*paths).paths;
            if ((*paths).chosen_filter) filter_index = *(*paths).chosen_filter;
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

// property_codemod begin OpenFileDialog (tools/property_families/open_file_dialog.json; generated, do not edit)

void OpenFileDialog::prepare_write(const decltype(title)&, std::string& incoming) {
    require_dialog_text(incoming, "OpenFileDialog title must be valid UTF-8 without NUL");
}

void OpenFileDialog::written(const decltype(title)&) {}

void OpenFileDialog::prepare_write(const decltype(initial_directory)&, std::string& incoming) {
    require_dialog_text(incoming, "OpenFileDialog initial directory must be valid UTF-8 without NUL");
}

void OpenFileDialog::written(const decltype(initial_directory)&) {}

void OpenFileDialog::prepare_write(const decltype(filter_index)&, std::size_t&) {}

void OpenFileDialog::written(const decltype(filter_index)&) {}

void OpenFileDialog::prepare_write(const decltype(multiselect)&, bool&) {}

void OpenFileDialog::written(const decltype(multiselect)&) {}

// property_codemod end OpenFileDialog

} // namespace gui_forms
