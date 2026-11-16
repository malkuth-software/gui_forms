#pragma once

#include "gui_forms/delegate.hpp"
#include "gui_forms/host.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms::host::detail {

// Notification-area icons as NSStatusItems in the menu bar. AppKit types stay
// in the .mm file. Everything runs on the main (UI) thread; a click on an
// item's button reaches `sink` there, a secondary or Control-click as the
// context gesture. macOS shows notification text only through the
// UserNotifications framework, which needs a signed bundle and the user's
// permission; this adapter does not offer it.
class AppKitStatusItems final {
public:
    using Sink = Delegate<const HostTrayIconEvent&>;

    explicit AppKitStatusItems(Sink sink);
    // Removes every item from the status bar.
    ~AppKitStatusItems();
    AppKitStatusItems(const AppKitStatusItems&) = delete;
    AppKitStatusItems& operator=(const AppKitStatusItems&) = delete;

    [[nodiscard]] HostServiceStatus add(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    [[nodiscard]] HostServiceStatus update(HostTrayIconId icon_id, const HostTrayIconRequest& request);
    void remove(HostTrayIconId icon_id) noexcept;
    // Pops a native menu under the item and returns when it closes.
    [[nodiscard]] HostTrayMenuResult show_menu(HostTrayIconId icon_id, const HostTrayMenuRequest& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::host::detail
