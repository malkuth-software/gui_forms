#include "gui_forms/components/toast_notification/toast_notification.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../../core/surface_root/access/surface_lifetime_access.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <new>
#include <stdexcept>
#include <utility>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

struct ToastNotification::State final {
    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    static void require_ui(const ToastNotification& self, std::string_view operation);
};

stxlib::Unique<ToastNotification::State> ToastNotification::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

namespace {

// Valid UTF-8 without NUL of at most `maximum` bytes, as the host takes it.
bool notification_text_valid(std::string_view text, std::size_t maximum) {
    if (text.size() > maximum || text.find('\0') != std::string_view::npos) return false;
    const bool result = validate_utf8(text).valid();
    return result;
}

} // namespace

ToastNotification::ToastNotification(Window& window) {
    (*state_).window_lifetime_ = detail::SurfaceLifetimeAccess::observe(window);
    window.verify_access("ToastNotification construction");
}

ToastNotification::~ToastNotification() {
    forget_notification();
}

Window* ToastNotification::bound_window() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> lifetime = (*state_).window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

HostServices* ToastNotification::current_services() const noexcept {
    Window* const window = bound_window();
    return window != nullptr ? (*window).host_services() : nullptr;
}

void ToastNotification::State::require_ui(const ToastNotification& self, std::string_view operation) {
    if (!self.is_alive()) throw std::logic_error("GUI.Forms cannot use a disposed ToastNotification");
    Window* const window = self.bound_window();
    if (window == nullptr) throw std::logic_error("GUI.Forms cannot use a ToastNotification after Window shutdown");
    (*window).verify_access(operation);
}

bool ToastNotification::supported() const noexcept {
    HostServices* const services = current_services();
    if (services == nullptr) return false;
    const bool result = (*services).snapshot().capabilities.supports(HostCapability::desktop_notifications);
    return result;
}

bool ToastNotification::shown() const noexcept {
    // Live while the window still has the services that showed it and they
    // still hold its identity.
    if (services_ == nullptr || current_services() != services_ || !notification_events_.connected()) return false;
    const bool result = (*services_).desktop_notification_live(notification_id_);
    return result;
}

void ToastNotification::forget_notification() noexcept {
    notification_events_.disconnect();
    services_ = nullptr;
    notification_id_ = {};
}

HostServiceStatus ToastNotification::show() {
    State::require_ui(*this, "ToastNotification show");
    HostServices* const services = current_services();
    if (services == nullptr) {
        status_ = {HostServiceError::unsupported};
        return status_;
    }
    // The notification shown before is replaced, not left beside the new one.
    if (shown()) {
        const HostServiceStatus withdrawn = (*services_).withdraw_desktop_notification(notification_id_);
        static_cast<void>(withdrawn);
    }
    forget_notification();
    HostDesktopNotificationRequest request;
    request.title = title.read();
    request.text = text.read();
    request.urgency = urgency.read();
    request.silent = silent.read();
    const HostDesktopNotificationResult result = (*services).show_desktop_notification(request);
    status_ = result.status;
    if (!result.status.accepted()) return status_;
    services_ = services;
    notification_id_ = result.notification_id;
    // The token owns the connection until forget_notification; its
    // disconnection also reports that the services' event is gone.
    notification_events_ = (*services).desktop_notification_events().subscribe(
        Delegate<const HostDesktopNotificationEvent&>::bind<ToastNotification,
                                                            &ToastNotification::on_notification_event>(*this));
    return status_;
}

HostServiceStatus ToastNotification::hide() {
    State::require_ui(*this, "ToastNotification hide");
    if (!shown()) {
        forget_notification();
        status_ = {};
        return status_;
    }
    status_ = (*services_).withdraw_desktop_notification(notification_id_);
    forget_notification();
    return status_;
}

void ToastNotification::on_notification_event(const HostDesktopNotificationEvent& event) {
    if (!is_alive() || event.notification_id != notification_id_ || current_services() != services_) return;
    // The identity is finished before the handlers run, so a handler may
    // show the notification again.
    forget_notification();
    switch (event.action) {
    case HostDesktopNotificationAction::activated:
        activated_.emit();
        return;
    case HostDesktopNotificationAction::dismissed:
        dismissed_.emit();
        return;
    case HostDesktopNotificationAction::failed:
        failed_.emit();
        return;
    }
    // An action outside the enumeration raises nothing.
    return;
}

void ToastNotification::verify_dispose_thread() {
    if (Window* const window = bound_window()) (*window).verify_access("ToastNotification disposal");
}

void ToastNotification::on_dispose() noexcept {
    forget_notification();
    (*state_).window_lifetime_.reset();
}

namespace {

// The events ToastNotification adds to those of Component, in declaration
// order.
constexpr std::array<InteractionDeclaration, 3> toast_notification_interactions{{
    {.name = "activated", .payload = "", .mutable_payload = false},
    {.name = "dismissed", .payload = "", .mutable_payload = false},
    {.name = "failed", .payload = "", .mutable_payload = false},
}};

} // namespace

void ToastNotification::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), toast_notification_interactions.begin(),
                        toast_notification_interactions.end());
}

// property_codemod begin ToastNotification (tools/property_families/toast_notification.json; generated, do not edit)

void ToastNotification::prepare_write(const decltype(title)&, std::string& incoming) {
    State::require_ui(*this, "ToastNotification property mutation");
    if (!notification_text_valid(incoming, HostServices::maximum_desktop_notification_title_bytes)) {
        throw std::invalid_argument("ToastNotification title must be UTF-8 without NUL of at most 255 bytes");
    }
}

void ToastNotification::written(const decltype(title)&) {}

void ToastNotification::prepare_write(const decltype(text)&, std::string& incoming) {
    State::require_ui(*this, "ToastNotification property mutation");
    if (!notification_text_valid(incoming, HostServices::maximum_desktop_notification_text_bytes)) {
        throw std::invalid_argument("ToastNotification text must be UTF-8 without NUL of at most 1023 bytes");
    }
}

void ToastNotification::written(const decltype(text)&) {}

void ToastNotification::prepare_write(const decltype(urgency)&, HostDesktopNotificationUrgency& incoming) {
    State::require_ui(*this, "ToastNotification property mutation");
    if (incoming != HostDesktopNotificationUrgency::low && incoming != HostDesktopNotificationUrgency::normal &&
        incoming != HostDesktopNotificationUrgency::critical) {
        throw std::invalid_argument("ToastNotification urgency is outside HostDesktopNotificationUrgency");
    }
}

void ToastNotification::written(const decltype(urgency)&) {}

void ToastNotification::prepare_write(const decltype(silent)&, bool&) {
    State::require_ui(*this, "ToastNotification property mutation");
}

void ToastNotification::written(const decltype(silent)&) {}

// property_codemod end ToastNotification

} // namespace gui_forms
