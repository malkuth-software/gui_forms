#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/components/toast_notification/toast_notification_capabilities/toast_notification_capabilities.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Window;

// A desktop notification (Windows' ToastNotification): a title and text the
// platform's own notification service shows outside the application's
// windows and keeps in its notification list: UNUserNotificationCenter in a
// macOS application bundle, a toast on Windows, and the freedesktop
// notification service on Linux. No tray icon is needed (NotifyIcon's
// show_balloon_tip is the tray's own notification).
// The Toast control is the other kind: a notice inside the application's
// own window.
//
// show hands the notification to the platform through the window's
// HostServices, which needs a host that reports
// HostCapability::desktop_notifications; status() after show or hide says
// when it does not. A high-integrity build compiles the service out, so
// show is refused as unsupported there. Showing again replaces the
// notification this component showed. What the user does arrives once, on
// the UI thread: activated (the body clicked), dismissed (closed or
// expired), or failed (the platform refused it after accepting it, as when
// the user turned the application's notifications off).
//
// The platform's requirements are the application's: macOS asks the user's
// permission on the first show and shows notifications only for a signed
// application bundle; Windows shows toasts for a packaged process or one
// that set an explicit AppUserModelID (SetCurrentProcessExplicitAppUserModelID)
// before the host started, with a Start menu shortcut carrying that ID.
class GUI_FORMS_API ToastNotification final : public Component,
                                              public ToastNotificationCapabilities<ToastNotification> {
public:
    // Construct on the window's UI thread. The notification is shown through
    // the HostServices the window has when show is called.
    explicit ToastNotification(Window& window);
    // Stops listening; a shown notification stays in the platform's list,
    // and what the user later does to it raises nothing.
    ~ToastNotification() override;

    // Shows the notification, replacing (withdrawing) the one this component
    // showed before if it is still live, and returns the host's answer:
    // unsupported without a host or without desktop_notifications,
    // invalid_argument for empty text, backend_failure when the platform's
    // service could not take it. Throws std::logic_error for a disposed
    // component, after Window shutdown, or off the window's UI thread.
    HostServiceStatus show();
    // Withdraws the shown notification from the platform's list, raising no
    // event; accepted and changing nothing when none is live. Throws as
    // show does.
    HostServiceStatus hide();

    // Whether the notification this component showed is still live: shown,
    // and not yet activated, dismissed, failed or withdrawn.
    [[nodiscard]] bool shown() const noexcept;
    // The host's answer to the last show or hide.
    [[nodiscard]] HostServiceStatus status() const noexcept { return status_; }
    // Whether the window's host shows desktop notifications.
    [[nodiscard]] bool supported() const noexcept;

    [[nodiscard]] Event<>& activated() noexcept { return activated_; }
    [[nodiscard]] Event<>& dismissed() noexcept { return dismissed_; }
    [[nodiscard]] Event<>& failed() noexcept { return failed_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    [[nodiscard]] Window* bound_window() const noexcept;
    [[nodiscard]] HostServices* current_services() const noexcept;
    void require_ui(std::string_view operation) const;
    // Leaves the notification's host state without withdrawing it: the
    // identity and the subscription are dropped. Never throws.
    void forget_notification() noexcept;
    void on_notification_event(const HostDesktopNotificationEvent& event);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    // The services the notification was shown through and its identity
    // there. The pointer is used only while the window still has these
    // services.
    HostServices* services_{};
    HostDesktopNotificationId notification_id_{};
    SubscriptionToken notification_events_{};
    HostServiceStatus status_{};
    Event<> activated_{};
    Event<> dismissed_{};
    Event<> failed_{};
    // property_codemod begin ToastNotification (tools/property_families/toast_notification.json; generated, do not edit)
private:
    friend struct ToastNotificationTitleCapability<ToastNotification>;
    friend struct TextCapability<ToastNotification>;
    friend struct ToastNotificationUrgencyCapability<ToastNotification>;
    friend struct ToastNotificationSilentCapability<ToastNotification>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(urgency)& cell, HostDesktopNotificationUrgency& incoming);
    void written(const decltype(urgency)& cell);
    void prepare_write(const decltype(silent)& cell, bool& incoming);
    void written(const decltype(silent)& cell);
    // property_codemod end ToastNotification
};

} // namespace gui_forms
