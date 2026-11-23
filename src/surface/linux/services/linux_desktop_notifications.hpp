#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <memory>

namespace gui_forms::host::linux_detail {

// Desktop notifications through the freedesktop notification service
// (org.freedesktop.Notifications on the session bus, Desktop Notifications
// Specification 1.2), with GIO's D-Bus client. Not compiled in a
// high-integrity build (cmake/HighIntegrity.cmake).
//
// Execution context: every call and every reply runs on the UI thread. The
// replies and the service's signals are dispatched from the default GLib
// main context, which the X11 host iterates on that thread
// (accessibility_dispatch and accessibility_wait); each reaches `sink`
// there, through the fail-fast boundary.
//
// Notify is asynchronous: show returns once the call is sent, and the
// service's identity for the notification arrives with the reply. A
// notification withdrawn before then is closed when the reply arrives. A
// failed reply reports `failed`; ActionInvoked with the default action
// reports `activated`; NotificationClosed reports `dismissed` unless the
// notification was withdrawn or already reported.
class LinuxDesktopNotifications final {
public:
    using Sink = Delegate<const HostDesktopNotificationEvent&>;

    // Whether the session bus has a notification service, running or
    // activatable. Asked once per process, synchronously, with a short
    // timeout; false where there is no session bus.
    [[nodiscard]] static bool service_present() noexcept;

    explicit LinuxDesktopNotifications(Sink sink);
    // Cancels the calls in flight and stops listening; the notifications
    // shown stay in the service's list.
    ~LinuxDesktopNotifications();
    LinuxDesktopNotifications(const LinuxDesktopNotifications&) = delete;
    LinuxDesktopNotifications& operator=(const LinuxDesktopNotifications&) = delete;

    // Sends Notify. backend_failure when the session bus cannot be reached.
    [[nodiscard]] HostServiceStatus show(HostDesktopNotificationId notification_id,
                                         const HostDesktopNotificationRequest& request);
    // Sends CloseNotification, or marks a notification whose Notify reply
    // has not arrived so that its reply closes it. Never fails.
    void withdraw(HostDesktopNotificationId notification_id) noexcept;

    // GIO's types stay in the source file.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::host::linux_detail
