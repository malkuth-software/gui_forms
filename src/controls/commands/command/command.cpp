#include "gui_forms/commands/command/command.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {

Command::Command() = default;

Command::Command(std::string stable_id, std::string text)
    : stable_id_(std::move(stable_id)) {
    if (stable_id_.empty() || !validate_utf8(stable_id_).valid() ||
        !validate_utf8(text).valid()) {
        throw std::invalid_argument("Command identity and text must be valid UTF-8");
    }
    state_.text = std::move(text);
}

void Command::on_dispose() noexcept {
    state_changed_.disconnect_all();
    invoked_.disconnect_all();
}

void Command::publish_state() {
    ++state_.generation;
    state_changed_.emit(state_);
}

void Command::set_text(std::string text) {
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("Command text must be valid UTF-8");
    }
    if (state_.text == text) return;
    state_.text = std::move(text);
    publish_state();
}

namespace {

void require_command_text(std::string_view value, const char* field) {
    if (!validate_utf8(value).valid()) {
        throw std::invalid_argument(std::string("Command ") + field +
                                    " must be valid UTF-8");
    }
}

} // namespace

#define GUI_FORMS_COMMAND_STRING_SETTER(method, member, field_name) \
void Command::method(std::string value) {                           \
    require_command_text(value, field_name);                        \
    if (state_.member == value) return;                             \
    state_.member = std::move(value);                               \
    publish_state();                                                \
}

GUI_FORMS_COMMAND_STRING_SETTER(set_description, description, "description")
GUI_FORMS_COMMAND_STRING_SETTER(set_icon_id, icon_id, "icon ID")
GUI_FORMS_COMMAND_STRING_SETTER(set_shortcut, shortcut, "shortcut")
GUI_FORMS_COMMAND_STRING_SETTER(set_mnemonic, mnemonic, "mnemonic")
GUI_FORMS_COMMAND_STRING_SETTER(set_availability_reason, availability_reason,
                                "availability reason")

#undef GUI_FORMS_COMMAND_STRING_SETTER

void Command::set_enabled(bool enabled) {
    if (state_.enabled == enabled) return;
    state_.enabled = enabled;
    publish_state();
}

void Command::set_visible(bool visible) {
    if (state_.visible == visible) return;
    state_.visible = visible;
    publish_state();
}

void Command::set_checked(bool checked) {
    if (state_.checkable && state_.checked == checked) return;
    state_.checkable = true;
    state_.checked = checked;
    publish_state();
}

void Command::clear_checked() {
    if (!state_.checkable) return;
    state_.checkable = false;
    state_.checked = false;
    publish_state();
}

namespace {
// A shortcut routes its command through the window it is registered with;
// the registration ends with the window or the command, whichever goes
// first, so both references stay valid while it can run.
struct InvokeShortcut final {
    Command& command;
    Window& window;
    bool operator()() const {
        const bool invoked = command.execute_routed(window, "shortcut");
        return invoked;
    }
};
} // namespace

AcceleratorToken Command::bind_shortcut(Window& window, KeyGesture gesture) {
    AcceleratorToken token = window.register_accelerator(
        *this, gesture, InvokeShortcut{*this, window});
    return token;
}

void Command::set_default_action(bool is_default) {
    if (state_.default_action == is_default) return;
    state_.default_action = is_default;
    publish_state();
}

void Command::set_destructive(bool destructive) {
    if (state_.destructive == destructive) return;
    state_.destructive = destructive;
    publish_state();
}

bool Command::execute(std::string_view source_id) {
    if (!is_alive() || !state_.enabled || !validate_utf8(source_id).valid()) {
        return false;
    }
    CommandInvocation invocation{stable_id_, std::string(source_id),
                                 next_sequence_++};
    invoked_.emit(invocation);
    return true;
}

bool Command::execute_routed(SurfaceRoot& window, std::string_view source_id) {
    if (!is_alive() || !state_.enabled || !validate_utf8(source_id).valid()) {
        return false;
    }
    const CommandInvocation invocation{stable_id_, std::string(source_id), next_sequence_++};
    const CommandRouteResult routed = window.route_command(invocation);
    if (routed.outcome == CommandRouteOutcome::refused) return false;
    if (routed.outcome == CommandRouteOutcome::handled) return true;
    invoked_.emit(invocation);
    return true;
}

namespace {

// The events Command adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 2> command_interactions{{
    {.name = "state_changed", .payload = "CommandState", .mutable_payload = false},
    {.name = "invoked", .payload = "CommandInvocation", .mutable_payload = false},
}};

} // namespace

void Command::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), command_interactions.begin(), command_interactions.end());
}

} // namespace gui_forms
