#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <memory>

namespace gui_forms::host::detail {

// Desktop notifications as Windows toast notifications
// (Windows.UI.Notifications: ToastNotificationManager, ToastNotifier,
// ToastNotification), through the Windows Runtime's ABI. combase's entries
// are loaded when the first toast is shown, so a process that shows none
// loads nothing. Not compiled in a high-integrity build
// (cmake/HighIntegrity.cmake).
//
// Windows shows toasts for a packaged process, or for one with an explicit
// AppUserModelID (SetCurrentProcessExplicitAppUserModelID, set before the
// host starts) that a Start menu shortcut carries; the application provides
// both. The notifier is created for that ID, or for the package.
//
// Execution context: show and withdraw run on the UI thread, which this
// object pins in a single-threaded apartment the first time it shows a
// toast (CoInitializeEx; never uninitialized). The toast's Activated,
// Dismissed and Failed events arrive on a thread of the runtime's; each
// handler only posts a message to a message-only window of this thread,
// whose procedure reports to `sink` on the UI thread, through the
// fail-fast boundary. A dismissal the application caused (Hide) reports
// nothing.
class WindowsToastNotifications final {
public:
    using Sink = Delegate<const HostDesktopNotificationEvent&>;

    // Whether the process can show toasts: it is packaged, or it has an
    // explicit AppUserModelID.
    [[nodiscard]] static bool available() noexcept;

    explicit WindowsToastNotifications(Sink sink);
    // Stops the toasts' handlers and destroys the window; the toasts stay
    // in the action center.
    ~WindowsToastNotifications();
    WindowsToastNotifications(const WindowsToastNotifications&) = delete;
    WindowsToastNotifications& operator=(const WindowsToastNotifications&) = delete;

    // backend_failure when the runtime, the notifier or the toast cannot be
    // made, or the notifier refuses it.
    [[nodiscard]] HostServiceStatus show(HostDesktopNotificationId notification_id,
                                         const HostDesktopNotificationRequest& request);
    // Hides the toast and stops its handlers. Never fails.
    void withdraw(HostDesktopNotificationId notification_id) noexcept;

    // The runtime's types stay in the source file.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::host::detail
