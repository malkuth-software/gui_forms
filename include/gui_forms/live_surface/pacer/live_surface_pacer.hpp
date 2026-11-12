#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>

namespace gui_forms {

class LiveSurface;

// What a pacer decided (LiveSurfacePacer).
enum class LivePacingOutcome : std::uint8_t {
    // Render now: the presenter showed the producer's last frame, or the
    // producer published nothing since its last frame and a tick passed.
    frame,
    // Render now anyway: the presenter did not show the last frame within
    // one and a half refreshes (a blocked or busy presenter thread). The
    // next publication replaces the unshown one.
    deadline,
    // Not yet (plan only): look again at `wake_at`, or sooner when the
    // presenter reports.
    pending,
    // The stop token fired (wait_for_frame only).
    stopped,
};

struct GUI_FORMS_API LivePacingResult final {
    LivePacingOutcome outcome{LivePacingOutcome::pending};
    // The display time the next frame will be shown for: the vertical blank
    // after now in the presenter's phase, or now without display timing.
    // A producer's motion uses it as its now.
    FrameTime frame_time{};
    // When to look again, for `pending`; FrameTime::max() while idle.
    FrameTime wake_at{};
    std::uint64_t presented_generation{};
    // Whole refreshes the producer waited past the first, for `deadline`:
    // the frames the presenter did not show.
    std::uint64_t missed_frames{};
};

// One producer's presentation clock (design note section 8, decision 7):
// paces a producer to the display through its presenter's reports
// (LiveSurface::notify_presented) and never to a free-running sleep, and
// keeps it going when the presenter stalls. Owned by the producer's thread;
// only arm() may be called from another. Holds the surface alive.
class GUI_FORMS_API LiveSurfacePacer final {
public:
    LiveSurfacePacer() = default;
    explicit LiveSurfacePacer(std::shared_ptr<LiveSurface> surface) noexcept;
    LiveSurfacePacer(const LiveSurfacePacer&) = delete;
    LiveSurfacePacer& operator=(const LiveSurfacePacer&) = delete;

    // Decides without blocking, for a producer that waits on something of
    // its own as well (a sub-surface's loop, woken by
    // LiveSurface::connect_presented_wake): frame or deadline commit the
    // frame (the next decision waits for it to be shown); pending names
    // when to look again. Empty pacers answer frame at `now`.
    [[nodiscard]] LivePacingResult plan(FrameTime now);
    // The display time a frame produced now would be shown for (the
    // frame_time plan would answer), without deciding anything: a
    // producer's motion ticks on it before it knows whether it will paint.
    // `now` without display timing or for an empty pacer.
    [[nodiscard]] FrameTime display_time(FrameTime now) const;
    // Blocks until plan would answer frame or deadline, or `stop` fires;
    // one wait per frame on the surface's condition, never a fixed sleep.
    // While idle it waits only for `stop` and arm().
    [[nodiscard]] LivePacingResult wait_for_frame(const std::stop_token& stop);
    // A producer with nothing armed and nothing dirty: plan answers pending
    // with no wake time and wait_for_frame waits for arm() or stop. Any
    // thread may arm(); it wakes a waiting producer.
    void idle() noexcept;
    void arm() noexcept;
    [[nodiscard]] bool is_idle() const noexcept { return idle_.load(std::memory_order_acquire); }

private:
    // plan's decision with the surface's lock held.
    [[nodiscard]] LivePacingResult plan_locked(FrameTime now, std::uint64_t published,
                                               const LivePresentationRecord& record);

    std::shared_ptr<LiveSurface> surface_{};
    std::atomic<bool> idle_{};
    // At the last frame decision: the surface's published generation and
    // the presenter's report count.
    std::uint64_t published_at_frame_{};
    std::uint64_t presents_at_frame_{};
    bool decided_once_{};
    // When the current wait began (the first pending since the last frame).
    FrameTime waiting_since_{};
    bool waiting_{};
};

} // namespace gui_forms
