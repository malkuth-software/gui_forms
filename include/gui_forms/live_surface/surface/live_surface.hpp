#pragma once

#include "gui_forms/live_surface/frame/live_surface_frame.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"
#include "gui_forms/live_surface/wake_connection/live_surface_wake_connection.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"

#include <functional>
#include <memory>

namespace gui_forms {

namespace detail {
struct LiveSurfaceState;
}

// Renderer-neutral latest-frame surface. It retains no window, input target,
// application callback, or presentation queue. A producer publishes complete
// candidates; terminal painters acquire only the newest immutable candidate.
class LiveSurface final : public std::enable_shared_from_this<LiveSurface> {
public:
    static std::shared_ptr<LiveSurface> create(
        LiveSurfaceDescription description);

    ~LiveSurface();
    LiveSurface(const LiveSurface&) = delete;
    LiveSurface& operator=(const LiveSurface&) = delete;

    // Replaces the buffers at the new description and starts a new epoch. The
    // previous epoch's last published frame stays acquirable, at its own size,
    // until the first publication of the new epoch, so a presenter scales it
    // to the destination instead of showing nothing (snapshot() reports
    // carries_previous_frame). Fails while a write lease is held.
    [[nodiscard]] bool reconfigure(LiveSurfaceDescription description);
    [[nodiscard]] LiveSurfaceWriteLease try_acquire_write(
        bool preserve_published_contents = false) noexcept;
    [[nodiscard]] LiveSurfaceFrame acquire_latest() const noexcept;
    // For the window presenter only. Acquires the newest frame and, in the
    // same step, takes and clears the invalid region published since this
    // presenter's previous call, as BeginPaint takes the update region; the
    // frame's damage() is that region. The first call, a call after
    // reconfigure, and a call from a different presenter receive the whole
    // surface. acquire_latest() never consumes the region.
    [[nodiscard]] LiveSurfaceFrame acquire_for_presentation(const void* presenter) noexcept;
    [[nodiscard]] LiveSurfaceSnapshot snapshot() const noexcept;
    [[nodiscard]] LiveSurfaceWakeConnection connect_presentation_wake(
        std::function<void()> wake);

    // Presentation feedback (stage S2, design note sections 7 and 8). For
    // the window presenter only: after a present tick that showed this
    // surface, reports the generation of the frame shown and the display
    // timing the tick was phased to. Wakes every pacer waiting on the
    // surface and calls each presented wake, on the presenter's thread.
    void notify_presented(std::uint64_t generation, LiveDisplayTiming timing) noexcept;
    [[nodiscard]] LivePresentationRecord presentation_record() const noexcept;
    // A wake called after each notify_presented, on the presenter's thread;
    // like a presentation wake, it may only signal.
    [[nodiscard]] LiveSurfaceWakeConnection connect_presented_wake(
        std::function<void()> wake);

private:
    friend class LiveSurfacePacer;
    explicit LiveSurface(std::shared_ptr<detail::LiveSurfaceState> state) noexcept;
    std::shared_ptr<detail::LiveSurfaceState> state_;
};

} // namespace gui_forms
