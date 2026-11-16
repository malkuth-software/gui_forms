#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <X11/Xlib.h>

#include <cstdint>
#include <vector>

namespace gui_forms::host::linux_detail {

// Notification-area icons through the freedesktop System Tray Protocol: each
// icon is a small window docked, by XEmbed, into the tray that owns the
// _NET_SYSTEM_TRAY_Sn selection. Plain Xlib; no D-Bus (StatusNotifierItem,
// which most current desktops prefer, would need it). The protocol carries no
// tooltip, notification text or menu: the tooltip is kept but not shown.
// Events are read from the host's own event loop through handle_event, on
// the UI thread, and reach `sink` there.
class X11TrayIcons final {
public:
    using Sink = Delegate<const HostTrayIconEvent&>;

    X11TrayIcons(Display* display, Sink sink) noexcept;
    // Destroys every icon window, which undocks it.
    ~X11TrayIcons();
    X11TrayIcons(const X11TrayIcons&) = delete;
    X11TrayIcons& operator=(const X11TrayIcons&) = delete;

    // Whether a system tray owns the screen's tray selection now.
    [[nodiscard]] static bool tray_present(Display* display) noexcept;

    [[nodiscard]] HostServiceStatus add(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    [[nodiscard]] HostServiceStatus update(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    void remove(HostTrayIconId icon_id) noexcept;
    // Returns true when `event` belonged to an icon window and was handled.
    bool handle_event(const XEvent& event);

private:
    struct Icon final {
        HostTrayIconId id{};
        ::Window window{};
        std::vector<HostImage> images;
        // The window's current size, as the tray laid it out.
        unsigned side{};
        Time last_release{};
    };

    [[nodiscard]] Icon* find_window(::Window window) noexcept;
    void draw(const Icon& icon) const;

    Display* display_{};
    Sink sink_{};
    std::vector<Icon> icons_{};
};

} // namespace gui_forms::host::linux_detail
