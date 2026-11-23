A desktop notification shown by the platform's own notification service.

A toast notification is a title and text that the desktop shows outside the
application's windows and keeps in its notification list, as Windows'
ToastNotification does: a toast on Windows, a notification through
`UNUserNotificationCenter` on macOS, and one through the freedesktop
notification service (``org.freedesktop.Notifications``) on Linux. No tray
icon is needed; `NotifyIcon`'s `show_balloon_tip` is the tray's own
notification.

The notification is shown through the window's `HostServices`, which reports
whether it can (the `desktop_notifications` capability); `show` and `hide`
report each refusal as an explicit `HostServiceStatus`. A high-integrity
build compiles the service out, so `show` is refused as `unsupported` there.

Parameters
----------
window
    The window whose host shows the notification. Construct the component on
    its UI thread.

Properties
----------
title
    The title, at most 255 bytes of UTF-8 without NUL; empty shows the text
    alone. Read by the next `show`.
text
    The text, 1 to 1023 bytes of UTF-8 without NUL. Read by the next `show`.
urgency : default ``normal``
    How much the notification asks for attention (`HostDesktopNotificationUrgency`):
    ``critical`` maps to the platform's insistent notification where it has
    one (the freedesktop urgency hint 2, Windows' urgent scenario, macOS's
    time-sensitive level).
silent : default ``false``
    Whether the platform is asked not to play its notification sound.
shown
    Whether the notification this component showed is still live: not yet
    activated, dismissed, failed or withdrawn.
status
    The host's answer to the last `show` or `hide`.
supported
    Whether the window's host reports `desktop_notifications`.

Events
------
activated
    Raised when the user activates the notification (clicks its body).
dismissed
    Raised when the user closes the notification or it expires.
failed
    Raised when the platform refuses the notification after accepting the
    request, as when the user turned the application's notifications off.

Methods
-------
show
    Shows the notification, withdrawing the one this component showed
    before if it is still live, and returns the host's answer:
    `unsupported` without a host or without `desktop_notifications`,
    `invalid_argument` for empty text, `backend_failure` when the platform's
    service could not take it.
hide
    Withdraws the shown notification from the platform's list, raising no
    event.

See Also
--------
HostServices : The desktop notification contract under the component.
NotifyIcon : The notification area's icon and its notification text.

Notes
-----
Events arrive once per shown notification, on the UI thread. The platform's
requirements are the application's:

- macOS: the process must run from a signed application bundle; the first
  `show` asks the user's permission, and notifications shown before the
  answer wait for it (a refusal raises `failed`). The host becomes the
  notification center's delegate and sets its categories.
- Windows: the process must be packaged, or set an explicit AppUserModelID
  (``SetCurrentProcessExplicitAppUserModelID``) before the host starts, with
  a Start menu shortcut carrying it.
- Linux: the session bus must have a notification service, running or
  activatable; servers without actions never raise `activated`.
- The headless reference host keeps each request and lets tests simulate
  what the user does.

Destroying the component leaves its notification in the platform's list;
what the user later does to it raises nothing.

Examples
--------
A notification shown where the host allows:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::ToastNotification toast(window);
   toast.title = "Backup";
   toast.text = "The backup finished.";
   const gf::HostServiceStatus status = toast.show();
   if (!status.accepted()) {
       // Without a host, status.error is gf::HostServiceError::unsupported.
   }
