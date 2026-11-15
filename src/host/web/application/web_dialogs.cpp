// The browser host's dialogs (web_host_internal.hpp). A message box is
// drawn by the host as a modal layer over its owner, as the X11 host's
// retained dialog is. The file and colour dialogs are the browser's own:
// its file chooser (the chosen files are copied into the program's file
// system), its save chooser or a download, and its colour input. Each runs
// nested turns on the program's thread until the page answers.
#include "web_host_internal.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <variant>

namespace gui_forms::host::web_detail {
namespace {

struct MessageState;

struct ChoiceAction final {
    MessageState* state;
    HostDialogChoice choice;
    void operator()(ButtonBase&) const;
};
struct MessageClosed final {
    MessageState* state;
    void operator()() const;
};
struct MessageHelpKey final {
    MessageState* state;
    bool operator()() const;
};

struct MessageState final {
    NativeWindow& owner;
    const HostDialogRequest& request;
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> accept_button;
    std::shared_ptr<Button> cancel_button;
    std::vector<SubscriptionToken> tokens;
    HostDialogResult result;
    bool done{};

    MessageState(NativeWindow& host, const HostDialogRequest& source) : owner(host), request(source) {
        result.request_id = request.request_id;
        result.payload = HostMessageDialogResult{};
        root = make_control<Panel>(StableId("host.dialog.root"));
        (*root).set_background({244, 246, 249, 255});
    }
    void finish(HostDialogChoice choice) {
        if (choice == HostDialogChoice::help) {
            help(false);
            return;
        }
        const HostDialogOutcome outcome =
            choice == HostDialogChoice::cancel ? HostDialogOutcome::cancelled : HostDialogOutcome::accepted;
        result.payload = HostMessageDialogResult{outcome, choice};
        done = true;
    }
    void help(bool keyboard_initiated) {
        if (!owner.services) return;
        HostDialogHelpEvent event{};
        event.request_id = request.request_id;
        event.keyboard_initiated = keyboard_initiated;
        static_cast<void>((*owner.services).publish_dialog_help(event));
    }
};

void ChoiceAction::operator()(ButtonBase&) const { (*state).finish(choice); }
void MessageClosed::operator()() const { (*state).done = true; }
bool MessageHelpKey::operator()() const {
    (*state).help(true);
    return true;
}

std::string choice_label(HostDialogChoice choice) {
    if (choice == HostDialogChoice::try_again) return "Try Again";
    std::string text = host_dialog_choice_name(choice);
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

std::vector<HostDialogChoice> message_choices(const HostMessageDialogRequest& message) {
    std::vector<HostDialogChoice> choices;
    switch (message.buttons) {
    case HostMessageButtons::ok:
        choices = {HostDialogChoice::ok};
        break;
    case HostMessageButtons::ok_cancel:
        choices = {HostDialogChoice::ok, HostDialogChoice::cancel};
        break;
    case HostMessageButtons::yes_no:
        choices = {HostDialogChoice::yes, HostDialogChoice::no};
        break;
    case HostMessageButtons::yes_no_cancel:
        choices = {HostDialogChoice::yes, HostDialogChoice::no, HostDialogChoice::cancel};
        break;
    case HostMessageButtons::retry_cancel:
        choices = {HostDialogChoice::retry, HostDialogChoice::cancel};
        break;
    case HostMessageButtons::abort_retry_ignore:
        choices = {HostDialogChoice::abort, HostDialogChoice::retry, HostDialogChoice::ignore};
        break;
    case HostMessageButtons::cancel_try_continue:
        choices = {HostDialogChoice::cancel, HostDialogChoice::try_again, HostDialogChoice::continue_};
        break;
    default:
        choices = {HostDialogChoice::ok};
        break;
    }
    if (message.help_button) choices.push_back(HostDialogChoice::help);
    return choices;
}

// An input element's accept list from the request's filters: ".png,.jpg";
// empty (every file) when a filter holds `*` or `*.*`, or there is none.
std::string accept_list(const std::vector<HostFileDialogFilter>& filters) {
    std::string accept;
    for (std::size_t filter = 0U; filter < filters.size(); ++filter) {
        const std::vector<std::string>& extensions = filters[filter].extensions;
        for (std::size_t index = 0U; index < extensions.size(); ++index) {
            std::string_view extension = extensions[index];
            if (extension.starts_with("*")) extension.remove_prefix(1U);
            if (extension.starts_with(".")) extension.remove_prefix(1U);
            if (extension.empty() || extension == "*") return std::string{};
            if (extension.find_first_of(",\"'<>") != std::string_view::npos) continue;
            if (!accept.empty()) accept.push_back(',');
            accept.push_back('.');
            accept.append(extension);
        }
    }
    return accept;
}

// A file name the program's file system and a download both take: the
// request's base name without separators, "untitled" when empty, with the
// default extension where it has none.
std::string save_name(const HostSaveFileDialogRequest& save) {
    std::string name = std::filesystem::path(save.suggested_name).filename().string();
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        if (character == '/' || character == '\\' || character == '\0') name[index] = '_';
    }
    if (name.empty() || name == "." || name == "..") name = "untitled";
    if (std::filesystem::path(name).extension().empty() && !save.default_extension.empty()) {
        if (save.default_extension.front() != '.') name.push_back('.');
        name.append(save.default_extension);
    }
    return name;
}

// "#rrggbb" as 0xRRGGBBAA with alpha 255; none for anything else.
std::optional<std::uint32_t> parse_color(std::string_view text) {
    if (text.size() != 7U || text.front() != '#') return std::nullopt;
    std::uint32_t value = 0U;
    for (std::size_t index = 1U; index < text.size(); ++index) {
        const char digit = text[index];
        int number = -1;
        if (digit >= '0' && digit <= '9') number = digit - '0';
        if (digit >= 'a' && digit <= 'f') number = digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F') number = digit - 'A' + 10;
        if (number < 0) return std::nullopt;
        value = (value << 4U) | static_cast<std::uint32_t>(number);
    }
    const std::uint32_t rgba = (value << 8U) | 0xFFU;
    return rgba;
}

HostDialogResult path_result(const HostDialogRequest& request, const PendingDialog& answer) {
    HostDialogResult result;
    result.request_id = request.request_id;
    if (answer.outcome == PageDialogOutcome::failed) {
        result.status.error = HostServiceError::backend_failure;
        result.payload = HostPathDialogResult{};
        return result;
    }
    HostPathDialogResult paths;
    if (answer.outcome == PageDialogOutcome::accepted && !answer.paths.empty()) {
        paths.outcome = HostDialogOutcome::accepted;
        paths.paths = answer.paths;
    }
    result.payload = std::move(paths);
    return result;
}

} // namespace

HostDialogResult message_dialog(NativeWindow& owner, const HostDialogRequest& request,
                                const HostMessageDialogRequest& message) {
    Runtime& runtime = owner.runtime;
    MessageState state(owner, request);
    const Size size{560.0, 250.0};
    const std::shared_ptr<Label> text = make_control<Label>(StableId("host.dialog.message"), message.message);
    (*text).font = FontSpec{FontRole::content, 15, 400, false};
    (*text).text_wrapping = TextWrapping::word;
    (*text).set_bounds({22.0, 20.0, 516.0, 164.0});
    if (message.right_align || message.rtl_reading) (*text).alignment = HorizontalAlignment::far;
    (*state.root).add_child(text);
    const std::vector<HostDialogChoice> choices = message_choices(message);
    for (std::size_t index = 0U; index < choices.size(); ++index) {
        const HostDialogChoice choice = choices[index];
        std::string name = choice_label(choice);
        for (std::size_t letter = 0U; letter < name.size(); ++letter) {
            if (name[letter] == ' ') name[letter] = '_';
        }
        const std::shared_ptr<Button> button =
            make_control<Button>(StableId("host.dialog.choice." + name), choice_label(choice));
        const double left = 538.0 - static_cast<double>(choices.size() - index) * 110.0;
        (*button).set_bounds({left, 200.0, 100.0, 32.0});
        (*button).font = FontSpec{FontRole::control, 14, 400, false};
        (*state.root).add_child(button);
        state.tokens.push_back((*button).clicked().subscribe(ChoiceAction{&state, choice}));
        if (choice == message.default_choice) state.accept_button = button;
        if (choice == HostDialogChoice::cancel) state.cancel_button = button;
    }
    WebApplicationWindow entry;
    entry.stable_id = "host.dialog";
    entry.owner_id = owner.entry.stable_id;
    entry.tool_window = true;
    entry.options.title = message.title;
    entry.options.initial_size = size;
    entry.options.minimum_size = size;
    entry.options.print_metrics_on_close = false;
    entry.options.closed = MessageClosed{&state};
    entry.model = std::make_unique<Window>(state.root, size);
    if (state.accept_button) {
        (*entry.model).set_accept_button(state.accept_button);
        (*state.accept_button).default_button = true;
    }
    if (state.cancel_button) (*entry.model).set_cancel_button(state.cancel_button);
    AcceleratorToken help_key = (*entry.model).register_accelerator(
        *state.root, KeyGesture{PhysicalKey::f1, Modifier::none}, MessageHelpKey{&state});
    // A layer of its own, owned by the dialog's owner and outside the run's
    // windowing table (a host dialog is no program window).
    const HostWindowId id = runtime.allocate_private_window();
    WebDeskLayerInit layer{};
    layer.id = id;
    layer.owner = owner.window_id;
    layer.kind = HostWindowKind::standard;
    layer.content = runtime.desk.placement(HostWindowKind::standard, owner.window_id, size);
    layer.minimum = size;
    layer.resizable = false;
    if (!runtime.desk.add(layer)) {
        help_key.disconnect();
        HostDialogResult refused;
        refused.request_id = request.request_id;
        refused.status.error = HostServiceError::too_large;
        return refused;
    }
    const std::optional<Rect> placed = runtime.desk.content(id);
    std::shared_ptr<NativeWindow> native;
    try {
        native = runtime.add(std::move(entry), id, placed.has_value() ? *placed : layer.content, false);
    } catch (...) {
        runtime.desk.remove(id);
        help_key.disconnect();
        HostDialogResult failed;
        failed.request_id = request.request_id;
        failed.status.error = HostServiceError::backend_failure;
        return failed;
    }
    runtime.desk.begin_modal(id);
    runtime.activate(id, true);
    while (!state.done && !owner.closed && !runtime.quitting) runtime.step();
    help_key.disconnect();
    runtime.desk.end_modal(id);
    // Closed here; Runtime::reclaim releases it once the page has let go of
    // its pixels.
    (*native).close(HostCloseReason::application, true);
    if (!owner.closed) runtime.activate(owner.window_id, true);
    state.tokens.clear();
    return state.result;
}

HostDialogResult browser_dialog(NativeWindow& owner, const HostDialogRequest& request) {
    Runtime& runtime = owner.runtime;
    const std::uint64_t page_request = runtime.next_dialog_request();
    const double request_number = static_cast<double>(page_request);
    if (const HostOpenFileDialogRequest* open = std::get_if<HostOpenFileDialogRequest>(&request.payload)) {
        const std::string accept = accept_list((*open).filters);
        gf_web_js_open_files(request_number, accept.c_str(), (*open).allow_multiple ? 1 : 0);
        const PendingDialog answer = runtime.wait_for_dialog(page_request);
        const HostDialogResult result = path_result(request, answer);
        return result;
    }
    if (const HostSaveFileDialogRequest* save = std::get_if<HostSaveFileDialogRequest>(&request.payload)) {
        const std::string name = save_name(*save);
        const std::filesystem::path directory =
            std::filesystem::path("/web/save") / std::to_string(page_request);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            HostDialogResult failed;
            failed.request_id = request.request_id;
            failed.status.error = HostServiceError::backend_failure;
            failed.payload = HostPathDialogResult{};
            return failed;
        }
        const std::string path = (directory / name).string();
        const std::string accept = accept_list((*save).filters);
        gf_web_js_save_file(request_number, path.c_str(), name.c_str(), accept.c_str());
        const PendingDialog answer = runtime.wait_for_dialog(page_request);
        const HostDialogResult result = path_result(request, answer);
        return result;
    }
    if (const HostColorDialogRequest* color = std::get_if<HostColorDialogRequest>(&request.payload)) {
        // The browser's colour input has no alpha: an accepted colour is
        // opaque.
        const unsigned int rgb = static_cast<unsigned int>((*color).initial_rgba >> 8U);
        gf_web_js_choose_color(request_number, rgb);
        const PendingDialog answer = runtime.wait_for_dialog(page_request);
        HostDialogResult result;
        result.request_id = request.request_id;
        HostColorDialogResult chosen;
        const std::optional<std::uint32_t> rgba = parse_color(answer.text);
        if (answer.outcome == PageDialogOutcome::accepted && rgba.has_value()) {
            chosen.outcome = HostDialogOutcome::accepted;
            chosen.rgba = *rgba;
        }
        if (answer.outcome == PageDialogOutcome::failed) result.status.error = HostServiceError::backend_failure;
        result.payload = chosen;
        return result;
    }
    HostDialogResult refused;
    refused.request_id = request.request_id;
    refused.status.error = HostServiceError::unsupported;
    return refused;
}

} // namespace gui_forms::host::web_detail
