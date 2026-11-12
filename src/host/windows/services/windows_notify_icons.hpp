#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms::host::detail {

// Notification-area icons through Shell_NotifyIconW. One hidden top-level
// window (created on the first icon) receives every icon's callback message
// and the shell's "TaskbarCreated" broadcast, which a message-only window
// would not; on that broadcast every icon is added again, since a restarted
// shell has forgotten them. Everything here runs on the UI thread whose
// message loop dispatches to that window; events reach `sink` there.
class WindowsNotifyIcons final {
public:
    using Sink = Delegate<const HostTrayIconEvent&>;
    // The private message the shell sends for icon events (NOTIFYICON_VERSION_4:
    // LOWORD(lParam) is the event, HIWORD(lParam) the icon's shell ID).
    static constexpr UINT callback_message = WM_APP + 0x2AU;

    explicit WindowsNotifyIcons(Sink sink) noexcept;
    // Removes every icon and destroys the window.
    ~WindowsNotifyIcons();
    WindowsNotifyIcons(const WindowsNotifyIcons&) = delete;
    WindowsNotifyIcons& operator=(const WindowsNotifyIcons&) = delete;

    [[nodiscard]] HostServiceStatus add(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    [[nodiscard]] HostServiceStatus update(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    void remove(HostTrayIconId icon_id) noexcept;
    [[nodiscard]] HostServiceStatus notify(HostTrayIconId icon_id, const HostTrayNotificationRequest& request);
    // Shows a native popup menu at the pointer and returns when it closes.
    [[nodiscard]] HostTrayMenuResult show_menu(HostTrayIconId icon_id, const HostTrayMenuRequest& request);

    // For tests: the window the shell posts to, and an icon's shell ID.
    [[nodiscard]] HWND message_window() const noexcept { return window_; }
    [[nodiscard]] std::optional<UINT> shell_id(HostTrayIconId icon_id) const noexcept;
    [[nodiscard]] UINT taskbar_created_message() const noexcept { return taskbar_created_; }

private:
    struct Icon final {
        HostTrayIconId id{};
        // The shell's 16-bit icon identity (HIWORD of the callback's lParam).
        UINT shell_id{};
        HICON handle{};
        std::wstring tooltip;
    };

    [[nodiscard]] bool ensure_window() noexcept;
    [[nodiscard]] Icon* find(HostTrayIconId icon_id) noexcept;
    [[nodiscard]] UINT next_shell_id() const noexcept;
    [[nodiscard]] bool show(const Icon& icon) noexcept;
    void handle_callback(WPARAM w_parameter, LPARAM l_parameter);
    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter);
    // The window procedure's body; window_procedure calls it through the
    // fail-fast boundary.
    static LRESULT answer_window_procedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter);

    Sink sink_{};
    std::vector<Icon> icons_{};
    HWND window_{};
    UINT taskbar_created_{};
};

// An icon handle from premultiplied BGRA8 host images: the one nearest
// `wanted_side` pixels, unpremultiplied into a 32-bit colour bitmap (icons
// take straight alpha) with an empty mask. Returns
// null on failure. The caller destroys the handle with DestroyIcon.
[[nodiscard]] HICON create_notify_icon_handle(std::span<const HostImage> images, int wanted_side) noexcept;

} // namespace gui_forms::host::detail
