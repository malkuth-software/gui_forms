#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/requests/surface_requests.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms::detail {

// One alert a root was asked to raise (SurfaceRoot::raise_alert), its
// content validated.
struct AlertRecord final {
    std::uint64_t id{};
    std::weak_ptr<Control> owner{};
    AlertRequest content{};
    std::weak_ptr<SurfaceRequestAnswer> answer{};
};

// Where a root's alerts go (threaded sub-surfaces design note section 15):
// a window shows them (WindowAlerts), a sub-surface forwards them to its
// parent (SubSurfaceAlertRequester). Owned by its root
// (SurfaceRoot::alerts_); every member runs on the root's thread.
class AlertRoute {
public:
    virtual ~AlertRoute() {}
    virtual void raise(AlertRecord record) = 0;
    // Withdraws alert `id` while it waits; answered revoked.
    virtual void withdraw(std::uint64_t id) = 0;
    // The root is going: nothing more is shown, forwarded or answered.
    virtual void shutdown() noexcept = 0;
};

// Whether `choice` is one of the buttons of `buttons`.
[[nodiscard]] bool alert_choice_in(HostMessageButtons buttons, HostDialogChoice choice) noexcept;
// The first button of `buttons`.
[[nodiscard]] HostDialogChoice first_alert_choice(HostMessageButtons buttons) noexcept;
// The answer of a box closed without a choice: cancel where the set has
// one, ok for an OK box, none for a box the user cannot dismiss.
[[nodiscard]] HostDialogChoice dismissed_alert_choice(HostMessageButtons buttons) noexcept;

// A new dialog request identity, unique in the process and shared with the
// dialog components (defined in host_services.cpp). Never zero.
[[nodiscard]] std::uint64_t take_dialog_request_id() noexcept;

} // namespace gui_forms::detail
