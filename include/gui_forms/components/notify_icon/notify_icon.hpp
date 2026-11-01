#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gui_forms {

class Window;

// A click on the icon: `button` is primary for a click and secondary for the
// platform's context gesture.
struct NotifyIconClick final {
    PointerButton button{PointerButton::primary};
};

// An icon in the desktop's notification area (Windows Forms' NotifyIcon):
// the system tray on Windows, a status item in the macOS menu bar, and an
// icon docked in a freedesktop system tray on X11. The application supplies
// the icon's pixels at one or more sizes, a tooltip and an optional
// ContextMenu; the host shows the image nearest the size its tray asks for.
// The icon is shown through the window's HostServices, so it needs a host
// that reports HostCapability::notification_area; status() and the results
// of set_visible and show_balloon_tip say when it does not. Events arrive on
// the UI thread.
class NotifyIcon final : public Component {
public:
    // Construct on the window's UI thread. The icon is shown through the
    // HostServices the window has when it becomes visible.
    explicit NotifyIcon(Window& window);
    // Removes the icon from the notification area.
    ~NotifyIcon() override;

    // The icon at one or more sizes, premultiplied BGRA8 (HostImage). Throws
    // std::invalid_argument for more than 8 images or an image that is
    // malformed or larger than 256 pixels a side. A shown icon is updated.
    [[nodiscard]] const std::vector<HostImage>& icon() const noexcept { return images_; }
    void set_icon(std::vector<HostImage> images);
    // The tooltip, at most 127 bytes of UTF-8 (std::invalid_argument
    // otherwise). A shown icon is updated.
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);

    // Whether the application wants the icon shown.
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    // Shows or removes the icon and returns the host's answer: unsupported
    // without a host or without the notification_area capability,
    // invalid_argument while there is no icon image. A visible icon without an
    // image is shown once an image is set.
    HostServiceStatus set_visible(bool visible);
    // Whether the icon is in the notification area now.
    [[nodiscard]] bool shown() const noexcept;
    // The host's answer to the last request this icon made.
    [[nodiscard]] HostServiceStatus status() const noexcept { return status_; }
    // Whether the window's host shows notification-area icons.
    [[nodiscard]] bool supported() const noexcept;

    // The menu shown on the icon's context gesture. Hosts with native tray
    // menus (notification_area_menus) show its rows natively; choosing a row
    // executes its command and raises menu_item_invoked. Submenus are left
    // out. Without native tray menus only click is raised.
    [[nodiscard]] std::shared_ptr<ContextMenu> context_menu() const noexcept { return context_menu_; }
    void set_context_menu(std::shared_ptr<ContextMenu> menu);

    // Shows notification text from the icon (Windows Forms' ShowBalloonTip):
    // a title of at most 63 bytes and text of at most 255 bytes. Refused as
    // unsupported where the host has no notification_area_messages, and as
    // invalid_argument while the icon is not shown.
    HostServiceStatus show_balloon_tip(std::string title, std::string text,
                                       HostTrayNotificationIcon icon = HostTrayNotificationIcon::none,
                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    [[nodiscard]] Event<const NotifyIconClick&>& click() noexcept { return click_; }
    [[nodiscard]] Event<>& double_click() noexcept { return double_click_; }
    [[nodiscard]] Event<>& balloon_tip_clicked() noexcept { return balloon_tip_clicked_; }
    [[nodiscard]] Event<>& balloon_tip_closed() noexcept { return balloon_tip_closed_; }
    [[nodiscard]] Event<const MenuItemInvocation&>& menu_item_invoked() noexcept { return menu_item_invoked_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    [[nodiscard]] Window* bound_window() const noexcept;
    [[nodiscard]] HostServices* current_services() const noexcept;
    void require_ui(std::string_view operation) const;
    // Brings the notification area in line with visible_, the images and
    // the text: adds, updates or removes the icon.
    HostServiceStatus synchronize(bool content_changed);
    void forget_icon() noexcept;
    void on_tray_event(const HostTrayIconEvent& event);
    void show_menu();
    [[nodiscard]] HostTrayIconRequest request() const;

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::vector<HostImage> images_{};
    std::string text_{};
    bool visible_{};
    std::shared_ptr<ContextMenu> context_menu_{};
    // The services the icon is shown through and its identity there. The
    // pointer is used only while the window still has these services.
    HostServices* services_{};
    HostTrayIconId icon_id_{};
    SubscriptionToken tray_events_{};
    HostServiceStatus status_{};
    Event<const NotifyIconClick&> click_{};
    Event<> double_click_{};
    Event<> balloon_tip_clicked_{};
    Event<> balloon_tip_closed_{};
    Event<const MenuItemInvocation&> menu_item_invoked_{};
};

} // namespace gui_forms
