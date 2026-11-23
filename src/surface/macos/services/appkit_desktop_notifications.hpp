#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <memory>

namespace gui_forms::host::detail {

// Desktop notifications through UNUserNotificationCenter. UserNotifications
// types stay in the .mm file. Not compiled in a high-integrity build
// (cmake/HighIntegrity.cmake).
//
// The notification center is the application's: one authorization, one
// delegate and one category set per process. The first show asks the
// user's permission (requestAuthorizationWithOptions); notifications shown
// before the answer wait for it, and a refusal reports each of them as
// `failed`. The host becomes the center's delegate, so a notification
// shows while the application is frontmost and its activation and
// dismissal come back; it sets the center's categories to its own one,
// which asks for the dismissal (UNNotificationCategoryOptionCustomDismissAction).
// An application that keeps its own delegate or categories does not use
// these services.
//
// Everything runs on the main (UI) thread: the center's answers, from
// whatever queue they arrive on, are posted to it (post_main_work) and
// reach `sink` there, through the fail-fast boundary.
class AppKitDesktopNotifications final {
public:
    using Sink = Delegate<const HostDesktopNotificationEvent&>;

    // Whether the process can use the notification center: it runs from an
    // application bundle with a bundle identifier (the center refuses any
    // other process).
    [[nodiscard]] static bool available() noexcept;

    explicit AppKitDesktopNotifications(Sink sink);
    // Forgets this object's notifications (they stay in Notification
    // Center); a late answer about them reaches nothing.
    ~AppKitDesktopNotifications();
    AppKitDesktopNotifications(const AppKitDesktopNotifications&) = delete;
    AppKitDesktopNotifications& operator=(const AppKitDesktopNotifications&) = delete;

    [[nodiscard]] HostServiceStatus show(HostDesktopNotificationId notification_id,
                                         const HostDesktopNotificationRequest& request);
    // Removes the notification from Notification Center, or from the
    // requests waiting for the user's permission. Never fails.
    void withdraw(HostDesktopNotificationId notification_id) noexcept;

    // Reports an answer for one of this object's notifications to its sink.
    // Called by the process-wide center state on the main thread.
    void report(const HostDesktopNotificationEvent& event);

private:
    Sink sink_{};
};

} // namespace gui_forms::host::detail
