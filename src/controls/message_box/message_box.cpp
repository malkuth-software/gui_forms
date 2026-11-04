#include "gui_forms/components/message_box/message_box.hpp"

#include "../file_dialog/file_dialog_support.hpp"
#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <variant>

namespace gui_forms {

namespace detail {

// Carries a message box's help requests to its owner window while the box
// is open: the host's reports for this box only, dropped once the window is
// gone. A handler's exception is held for show_message_box, which throws it
// after the box closes: it must not cross the host's native modal loop.
class MessageBoxHelpRoute final {
public:
    MessageBoxHelpRoute(Window& owner, std::uint64_t request_id, std::exception_ptr& failure) noexcept
        : window_(owner.lifetime_), request_id_(request_id), failure_(&failure) {}

    void operator()(const HostDialogHelpEvent& event) const {
        if (event.request_id != request_id_) return;
        const std::shared_ptr<WindowLifetime> alive = window_.lock();
        if (!alive || (*alive).window == nullptr) return;
        try {
            static_cast<void>((*(*alive).window).request_help(event.keyboard_initiated));
        } catch (...) {
            if (!*failure_) *failure_ = std::current_exception();
        }
    }

private:
    std::weak_ptr<WindowLifetime> window_;
    std::uint64_t request_id_{};
    std::exception_ptr* failure_{};
};

} // namespace detail

namespace {

// One button set's buttons, then Help where the box shows it, in the order
// the default button counts them.
struct ButtonOrder final {
    std::array<HostDialogChoice, 4> choices{};
    std::size_t count{};
};

[[nodiscard]] bool buttons_known(HostMessageButtons buttons) noexcept {
    return buttons == HostMessageButtons::ok || buttons == HostMessageButtons::ok_cancel ||
           buttons == HostMessageButtons::yes_no || buttons == HostMessageButtons::yes_no_cancel ||
           buttons == HostMessageButtons::retry_cancel || buttons == HostMessageButtons::abort_retry_ignore ||
           buttons == HostMessageButtons::cancel_try_continue;
}

[[nodiscard]] bool icon_known(HostMessageIcon icon) noexcept {
    return icon == HostMessageIcon::none || icon == HostMessageIcon::information ||
           icon == HostMessageIcon::warning || icon == HostMessageIcon::error ||
           icon == HostMessageIcon::question;
}

[[nodiscard]] bool default_button_known(MessageBoxDefaultButton button) noexcept {
    return button == MessageBoxDefaultButton::button1 || button == MessageBoxDefaultButton::button2 ||
           button == MessageBoxDefaultButton::button3 || button == MessageBoxDefaultButton::button4;
}

[[nodiscard]] bool options_known(MessageBoxOptions options) noexcept {
    const std::uint32_t admitted = static_cast<std::uint32_t>(MessageBoxOptions::right_align | MessageBoxOptions::rtl_reading);
    return (static_cast<std::uint32_t>(options) & ~admitted) == 0U;
}

[[nodiscard]] ButtonOrder button_order(HostMessageButtons buttons, bool help_button) noexcept {
    ButtonOrder order;
    if (buttons == HostMessageButtons::ok) {
        order.choices = {HostDialogChoice::ok, HostDialogChoice::none, HostDialogChoice::none, HostDialogChoice::none};
        order.count = 1U;
    } else if (buttons == HostMessageButtons::ok_cancel) {
        order.choices = {HostDialogChoice::ok, HostDialogChoice::cancel, HostDialogChoice::none, HostDialogChoice::none};
        order.count = 2U;
    } else if (buttons == HostMessageButtons::yes_no) {
        order.choices = {HostDialogChoice::yes, HostDialogChoice::no, HostDialogChoice::none, HostDialogChoice::none};
        order.count = 2U;
    } else if (buttons == HostMessageButtons::yes_no_cancel) {
        order.choices = {HostDialogChoice::yes, HostDialogChoice::no, HostDialogChoice::cancel, HostDialogChoice::none};
        order.count = 3U;
    } else if (buttons == HostMessageButtons::retry_cancel) {
        order.choices = {HostDialogChoice::retry, HostDialogChoice::cancel, HostDialogChoice::none, HostDialogChoice::none};
        order.count = 2U;
    } else if (buttons == HostMessageButtons::abort_retry_ignore) {
        order.choices = {HostDialogChoice::abort, HostDialogChoice::retry, HostDialogChoice::ignore, HostDialogChoice::none};
        order.count = 3U;
    } else {
        order.choices = {HostDialogChoice::cancel, HostDialogChoice::try_again, HostDialogChoice::continue_, HostDialogChoice::none};
        order.count = 3U;
    }
    if (help_button) {
        order.choices[order.count] = HostDialogChoice::help;
        ++order.count;
    }
    return order;
}

// The default button's choice; a position past the last button is the first.
[[nodiscard]] HostDialogChoice default_choice(HostMessageButtons buttons, bool help_button,
                                              MessageBoxDefaultButton button) noexcept {
    const ButtonOrder order = button_order(buttons, help_button);
    std::size_t position = static_cast<std::size_t>(button);
    if (position >= order.count) position = 0U;
    return order.choices[position];
}

[[nodiscard]] DialogResult dialog_result(HostDialogChoice choice) noexcept {
    if (choice == HostDialogChoice::ok) return DialogResult::ok;
    if (choice == HostDialogChoice::cancel) return DialogResult::cancel;
    if (choice == HostDialogChoice::yes) return DialogResult::yes;
    if (choice == HostDialogChoice::no) return DialogResult::no;
    if (choice == HostDialogChoice::retry) return DialogResult::retry;
    if (choice == HostDialogChoice::abort) return DialogResult::abort;
    if (choice == HostDialogChoice::ignore) return DialogResult::ignore;
    if (choice == HostDialogChoice::try_again) return DialogResult::try_again;
    if (choice == HostDialogChoice::continue_) return DialogResult::continue_;
    return DialogResult::none;
}

// A box closed without a choice: cancel where the box has Cancel, ok for an
// OK-only box, and none for Yes/No and Abort, Retry, Ignore, which have
// neither.
[[nodiscard]] DialogResult dismissed_result(HostMessageButtons buttons) noexcept {
    if (buttons == HostMessageButtons::ok) return DialogResult::ok;
    if (buttons == HostMessageButtons::yes_no || buttons == HostMessageButtons::abort_retry_ignore) {
        return DialogResult::none;
    }
    return DialogResult::cancel;
}

} // namespace

MessageBoxResult::MessageBoxResult(State state) noexcept : state_(state) {}

MessageBoxResult MessageBoxResult::closed(DialogResult result) noexcept {
    return MessageBoxResult(State(MessageBoxClosed{result}));
}

MessageBoxResult MessageBoxResult::refused(HostServiceError error) {
    if (error == HostServiceError::none) {
        throw std::invalid_argument("GUI.Forms MessageBoxResult::refused needs an error other than none");
    }
    return MessageBoxResult(State(MessageBoxRefused{error}));
}

bool MessageBoxResult::shown() const noexcept { return std::holds_alternative<MessageBoxClosed>(state_); }

DialogResult MessageBoxResult::result() const noexcept {
    const MessageBoxClosed* const closed_box = std::get_if<MessageBoxClosed>(&state_);
    if (closed_box == nullptr) return DialogResult::none;
    return (*closed_box).result;
}

HostServiceError MessageBoxResult::error() const noexcept {
    const MessageBoxRefused* const refusal = std::get_if<MessageBoxRefused>(&state_);
    if (refusal == nullptr) return HostServiceError::none;
    return (*refusal).error;
}

MessageBoxResult show_message_box(Window& owner, const MessageBoxRequest& request) {
    if (!file_dialog_text_valid(request.text) || !file_dialog_text_valid(request.caption)) {
        throw std::invalid_argument("GUI.Forms message box text and caption must be valid UTF-8 without NUL "
                                    "of at most HostServices::maximum_dialog_text_bytes");
    }
    if (!buttons_known(request.buttons) || !icon_known(request.icon) ||
        !default_button_known(request.default_button) || !options_known(request.options)) {
        throw std::invalid_argument("GUI.Forms message box buttons, icon, default button or options are unknown");
    }
    owner.verify_access("show_message_box");
    HostServices* const services = owner.host_services();
    if (services == nullptr) return MessageBoxResult::refused(HostServiceError::unsupported);
    HostMessageDialogRequest payload;
    payload.title = request.caption;
    payload.message = request.text;
    payload.buttons = request.buttons;
    payload.icon = request.icon;
    payload.default_choice = default_choice(request.buttons, request.display_help_button, request.default_button);
    payload.help_button = request.display_help_button;
    payload.right_align = has_message_box_option(request.options, MessageBoxOptions::right_align);
    payload.rtl_reading = has_message_box_option(request.options, MessageBoxOptions::rtl_reading);
    HostDialogRequest host_request;
    host_request.request_id = detail::take_dialog_request_id();
    const Control::Ptr root = owner.root();
    if (root) host_request.owner_id.assign((*root).stable_id().value());
    host_request.payload = std::move(payload);
    // The host's modal loop may run other callbacks, which may destroy
    // `owner`; nothing of it is read after the call. Help requests reach it
    // through the route only while it lives.
    std::exception_ptr help_failure{};
    SubscriptionToken help_route = (*services).dialog_help_requested().subscribe(
        detail::MessageBoxHelpRoute(owner, host_request.request_id, help_failure));
    const HostDialogResult answer = (*services).show_dialog(host_request);
    help_route.disconnect();
    if (help_failure) std::rethrow_exception(help_failure);
    if (!answer.status.accepted()) return MessageBoxResult::refused(answer.status.error);
    // HostServices has checked that an accepted answer to a message request
    // carries a message result whose choice the buttons allow. An answer
    // without one is the host's failure: the one fallback.
    const HostMessageDialogResult* const message = std::get_if<HostMessageDialogResult>(&answer.payload);
    if (message == nullptr) return MessageBoxResult::refused(HostServiceError::backend_failure);
    if ((*message).outcome == HostDialogOutcome::accepted) {
        return MessageBoxResult::closed(dialog_result((*message).choice));
    }
    return MessageBoxResult::closed(dismissed_result(request.buttons));
}

} // namespace gui_forms
