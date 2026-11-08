#include "gui_forms/window.hpp"

#include "alert_route.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

// Posted to a root's dispatcher with one answer: delivers it on the root's
// thread unless the target was released first.
class DeliverRequestAnswer final {
public:
    DeliverRequestAnswer(std::weak_ptr<SurfaceRequestAnswer> target, SurfaceRequestOutcome outcome) noexcept
        : target_(std::move(target)), outcome_(outcome) {}
    void operator()() const {
        const std::shared_ptr<SurfaceRequestAnswer> target = target_.lock();
        if (!target) return;
        (*target).request_answered(outcome_);
    }

private:
    std::weak_ptr<SurfaceRequestAnswer> target_;
    SurfaceRequestOutcome outcome_{};
};

[[nodiscard]] bool alert_text_valid(std::string_view text) noexcept {
    if (text.size() > HostServices::maximum_dialog_text_bytes) return false;
    if (text.find('\0') != std::string_view::npos) return false;
    const bool valid = validate_utf8(text).valid();
    return valid;
}

[[nodiscard]] bool alert_buttons_known(HostMessageButtons buttons) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok:
    case HostMessageButtons::ok_cancel:
    case HostMessageButtons::yes_no:
    case HostMessageButtons::yes_no_cancel:
    case HostMessageButtons::retry_cancel:
    case HostMessageButtons::abort_retry_ignore:
    case HostMessageButtons::cancel_try_continue:
        return true;
    }
    return false;
}

[[nodiscard]] bool alert_icon_known(HostMessageIcon icon) noexcept {
    switch (icon) {
    case HostMessageIcon::none:
    case HostMessageIcon::information:
    case HostMessageIcon::warning:
    case HostMessageIcon::error:
    case HostMessageIcon::question:
        return true;
    }
    return false;
}

} // namespace

namespace detail {

bool alert_choice_in(HostMessageButtons buttons, HostDialogChoice choice) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok: return choice == HostDialogChoice::ok;
    case HostMessageButtons::ok_cancel: return choice == HostDialogChoice::ok || choice == HostDialogChoice::cancel;
    case HostMessageButtons::yes_no: return choice == HostDialogChoice::yes || choice == HostDialogChoice::no;
    case HostMessageButtons::yes_no_cancel:
        return choice == HostDialogChoice::yes || choice == HostDialogChoice::no || choice == HostDialogChoice::cancel;
    case HostMessageButtons::retry_cancel:
        return choice == HostDialogChoice::retry || choice == HostDialogChoice::cancel;
    case HostMessageButtons::abort_retry_ignore:
        return choice == HostDialogChoice::abort || choice == HostDialogChoice::retry ||
               choice == HostDialogChoice::ignore;
    case HostMessageButtons::cancel_try_continue:
        return choice == HostDialogChoice::cancel || choice == HostDialogChoice::try_again ||
               choice == HostDialogChoice::continue_;
    }
    return false;
}

HostDialogChoice first_alert_choice(HostMessageButtons buttons) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok:
    case HostMessageButtons::ok_cancel: return HostDialogChoice::ok;
    case HostMessageButtons::yes_no:
    case HostMessageButtons::yes_no_cancel: return HostDialogChoice::yes;
    case HostMessageButtons::retry_cancel: return HostDialogChoice::retry;
    case HostMessageButtons::abort_retry_ignore: return HostDialogChoice::abort;
    case HostMessageButtons::cancel_try_continue: return HostDialogChoice::cancel;
    }
    return HostDialogChoice::ok;
}

HostDialogChoice dismissed_alert_choice(HostMessageButtons buttons) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok: return HostDialogChoice::ok;
    case HostMessageButtons::ok_cancel:
    case HostMessageButtons::yes_no_cancel:
    case HostMessageButtons::retry_cancel:
    case HostMessageButtons::cancel_try_continue: return HostDialogChoice::cancel;
    case HostMessageButtons::yes_no:
    case HostMessageButtons::abort_retry_ignore: return HostDialogChoice::none;
    }
    return HostDialogChoice::none;
}

} // namespace detail

void SurfaceRoot::deliver_request_answer(const std::weak_ptr<SurfaceRequestAnswer>& answer,
                                         const SurfaceRequestOutcome& outcome) {
    if (answer.expired()) return;
    static_cast<void>(dispatcher().begin_invoke(DeliverRequestAnswer(answer, outcome)));
}

std::uint64_t SurfaceRoot::raise_alert(const Control::Ptr& owner, AlertRequest request,
                                       std::weak_ptr<SurfaceRequestAnswer> answer) {
    require_root_thread("alert");
    if (!owner || !(*owner).is_alive() || (*owner).attached_surface_root() != this) {
        throw std::invalid_argument("GUI.Forms alert owner must be a live control attached to its root");
    }
    if (request.text.empty() || !alert_text_valid(request.text) || !alert_text_valid(request.caption)) {
        throw std::invalid_argument("GUI.Forms alert text must be non-empty, and it and the caption valid UTF-8 "
                                    "without NUL of at most HostServices::maximum_dialog_text_bytes");
    }
    if (!alert_buttons_known(request.buttons) || !alert_icon_known(request.icon)) {
        throw std::invalid_argument("GUI.Forms alert buttons or icon are unknown");
    }
    if (request.default_choice != HostDialogChoice::none &&
        !detail::alert_choice_in(request.buttons, request.default_choice)) {
        throw std::invalid_argument("GUI.Forms alert default choice must be one of its buttons");
    }
    detail::AlertRecord record;
    record.id = next_alert_;
    ++next_alert_;
    record.owner = owner;
    record.content = std::move(request);
    record.answer = std::move(answer);
    const std::uint64_t id = record.id;
    (*alerts_).raise(std::move(record));
    return id;
}

void SurfaceRoot::withdraw_alert(std::uint64_t id) {
    require_root_thread("alert");
    (*alerts_).withdraw(id);
}

} // namespace gui_forms
