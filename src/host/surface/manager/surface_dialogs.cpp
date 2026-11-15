// The window manager's dialogs (surface_manager_internal.hpp). A message
// box is drawn by the manager as a modal layer over its owner, as the X11
// host's retained dialog is. The file, folder and colour dialogs are the
// host's own (SurfaceHost::start_dialog): the run's thread runs nested turns
// until the host answers.
#include "surface_manager_internal.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <variant>

namespace gui_forms::host::surface_detail {
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
    if (answer.outcome == SurfaceDialogOutcome::failed) {
        result.status.error = HostServiceError::backend_failure;
        result.payload = HostPathDialogResult{};
        return result;
    }
    HostPathDialogResult paths;
    if (answer.outcome == SurfaceDialogOutcome::accepted && !answer.paths.empty()) {
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
    SurfaceApplicationWindow entry;
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
    SurfaceDeskLayerInit layer{};
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
    runtime.begin_modal_scope(id);
    runtime.activate(id, true);
    while (!state.done && !owner.closed && !runtime.quitting) runtime.step();
    help_key.disconnect();
    runtime.desk.end_modal(id);
    // Closed here; Runtime::reclaim releases it.
    (*native).close(HostCloseReason::application, true);
    if (!owner.closed) runtime.activate(owner.window_id, true);
    state.tokens.clear();
    return state.result;
}

HostDialogResult host_dialog(NativeWindow& owner, const HostDialogRequest& request) {
    Runtime& runtime = owner.runtime;
    const std::uint64_t ticket = runtime.next_dialog_ticket();
    const HostServiceStatus started = runtime.host.start_dialog(request, ticket);
    if (!started.accepted()) {
        HostDialogResult refused;
        refused.request_id = request.request_id;
        refused.status = started;
        return refused;
    }
    const PendingDialog answer = runtime.wait_for_dialog(ticket);
    if (std::holds_alternative<HostColorDialogRequest>(request.payload)) {
        // The host answers "#rrggbb": an accepted colour is opaque.
        HostDialogResult result;
        result.request_id = request.request_id;
        HostColorDialogResult chosen;
        const std::optional<std::uint32_t> rgba = parse_color(answer.text);
        if (answer.outcome == SurfaceDialogOutcome::accepted && rgba.has_value()) {
            chosen.outcome = HostDialogOutcome::accepted;
            chosen.rgba = *rgba;
        }
        if (answer.outcome == SurfaceDialogOutcome::failed) result.status.error = HostServiceError::backend_failure;
        result.payload = chosen;
        return result;
    }
    const HostDialogResult result = path_result(request, answer);
    return result;
}

} // namespace gui_forms::host::surface_detail
