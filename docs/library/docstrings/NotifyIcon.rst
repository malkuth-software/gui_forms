An icon in the desktop's notification area, with clicks, a menu and notification text.

A notify icon puts an application icon where the desktop keeps them, as
Windows Forms' NotifyIcon does: the notification area (system tray) on
Windows, a status item in the macOS menu bar, and an icon docked in a
freedesktop system tray on X11. The application supplies the icon's pixels at
one or more sizes, a tooltip, and an optional `ContextMenu`; the host shows
the image nearest the size its tray asks for. Use it for a program that keeps
working while its windows are closed or hidden.

The icon is shown through the window's `HostServices`. Every host reports
what it can do as capability flags, and the icon reports each refusal as an
explicit `HostServiceStatus` rather than failing silently.

Parameters
----------
window
    The window whose host shows the icon. Construct the icon on its UI
    thread; the icon uses the services the window has when it becomes
    visible.

Properties
----------
icon
    The icon's images, premultiplied BGRA8, up to 8 sizes of at most 256
    pixels a side. Every write replaces them and updates a shown icon; more
    images, or a malformed or larger image, throw `std::invalid_argument`.
text
    The tooltip, at most 127 bytes of UTF-8. A changed write updates a shown icon;
    longer or invalid text throws `std::invalid_argument`.
visible : default ``false``
    Whether the application wants the icon shown. Every write shows or
    removes it, and `status` then holds the host's answer: `unsupported` without a host or
    without the `notification_area` capability, `invalid_argument` while
    there is no image. A visible icon without an image is shown once an image
    is set.
shown
    Whether the icon is in the notification area now. An icon disappears
    with the host session that showed it.
status
    The host's answer to the icon's last request.
supported
    Whether the window's host reports `notification_area`.
context_menu
    The menu shown on the icon's context gesture.

Events
------
click
    Raised on a click (``button`` primary) and on the platform's context
    gesture, a secondary click or the menu key (``button`` secondary).
double_click
    Raised on a double click.
balloon_tip_clicked
    Raised when the user clicks notification text from the icon.
balloon_tip_closed
    Raised when notification text from the icon closes or times out.
menu_item_invoked
    Raised after a row of the context menu, chosen in the native menu, has
    run its command, through the bound window's command chain (decision
    DK-47).

Methods
-------
show_balloon_tip
    Shows notification text from the icon: a title of at most 63 bytes, text
    of at most 255 bytes, an optional information, warning or error icon and
    a timeout the platform may ignore. Returns `unsupported` where the host
    has no `notification_area_messages`, `invalid_argument` while the icon is
    not shown, and the host's own answer otherwise.

See Also
--------
HostServices : The tray contract under the icon.
ContextMenu : The menu whose rows the icon shows natively.
Command : What a menu row runs.

Notes
-----
Events arrive on the UI thread, from the host's own event loop. On the
context gesture the icon raises `click` with the secondary button first;
then, where the host offers native tray menus (`notification_area_menus`), it
shows the context menu's command, check and radio rows and separators
natively (submenus and hidden commands are left out), runs the chosen row's
command with the source ``notify_icon`` and raises `menu_item_invoked`.
Without native menus only `click` is raised, and the application may show
its own interface.

What each host offers:

- Windows: `Shell_NotifyIconW` with a hidden window that also hears the
  shell's TaskbarCreated broadcast and shows every icon again after the shell
  restarts; tooltips, notification text and native menus.
- macOS: an `NSStatusItem` per icon with the tooltip and a native menu; no
  notification text, which needs the UserNotifications framework, a signed
  bundle and the user's permission.
- X11: the freedesktop System Tray Protocol (XEmbed into the window owning
  ``_NET_SYSTEM_TRAY_Sn``) in plain Xlib; `notification_area` is reported only
  when a tray owned that selection at startup. The protocol carries no
  tooltip, notification text or menu, so the tooltip is kept but not shown.
  Desktops that use StatusNotifierItem over D-Bus instead are not supported.
- The headless reference host records every request and lets tests simulate
  user actions.

Disposing or destroying the icon removes it from the notification area.

Examples
--------
An icon with a tooltip and a menu, shown where the host allows:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::HostImage pixel;
   pixel.width = 16U;
   pixel.height = 16U;
   pixel.row_bytes = 64U;
   pixel.pixels.assign(64U * 16U, std::byte{0xFF});
   gf::NotifyIcon icon(window);
   icon.icon = std::vector<gf::HostImage>{pixel};
   icon.text = "Radio: FM 98.1";
   icon.visible = true;
   const gf::HostServiceStatus status = icon.status();
   if (!status.accepted()) {
       // Without a host, status.error is gf::HostServiceError::unsupported.
   }
