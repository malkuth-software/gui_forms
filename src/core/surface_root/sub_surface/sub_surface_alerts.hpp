#pragma once

#include "../requests/alert_route.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace gui_forms::detail {

// A sub-surface's alert route (design note section 15): the sub-surface
// shows nothing beyond its region, so each alert is forwarded to the
// parent, which raises it on the host and relays its one answer. Owned by
// its SubSurface (SurfaceRoot::alerts_); sub-surface thread only.
class SubSurfaceAlertRequester final : public AlertRoute {
public:
    explicit SubSurfaceAlertRequester(SubSurface& root) noexcept;
    ~SubSurfaceAlertRequester() override;
    SubSurfaceAlertRequester(const SubSurfaceAlertRequester&) = delete;
    SubSurfaceAlertRequester& operator=(const SubSurfaceAlertRequester&) = delete;

    // Forwards the alert; declined at once when there is no parent to ask.
    void raise(AlertRecord record) override;
    // Revokes it at the parent and answers revoked here; the parent's later
    // answer, if any, is dropped.
    void withdraw(std::uint64_t id) override;
    // Forgets every alert without posting: the thread is stopping, and the
    // host withdraws its alerts when it stops the thread.
    void shutdown() noexcept override;

    // The parent's answer to a forwarded alert, passed on to its target.
    // False when no forwarded alert has its id (withdrawn here first).
    bool receive_outcome(const SurfaceRequestOutcome& outcome);

private:
    // One alert forwarded and not yet answered.
    struct Forwarded final {
        // The channel's request ID, and the root's alert ID.
        std::uint64_t request{};
        std::uint64_t id{};
        std::weak_ptr<SurfaceRequestAnswer> answer{};
    };

    void answer(std::uint64_t id, const std::weak_ptr<SurfaceRequestAnswer>& target, SurfaceRequestResult result,
                HostDialogChoice choice);

    // Observes the root that owns this route.
    SubSurface& root_;
    std::vector<Forwarded> forwarded_{};
};

} // namespace gui_forms::detail
