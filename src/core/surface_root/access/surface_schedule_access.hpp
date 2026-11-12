#pragma once

// The authority to schedule frame work on a root for parties other than its
// own controls (B10): one-shot presenter wakes, the motion clock's frame
// request and deadline, and the count of frame-request cancellations that a
// timer waiting for paint compares.
//
// Holders: ToolTipPresenter and ToastPresenter (one-shot wakes), MotionClock
// (its single frame request), AnimationTimer (the cancellation count).
//
// Stale use: every token returned is revoked by the root's teardown
// (release_tree cancels the frame requests) as well as by its holder, so no
// callback runs after the root is gone. The presenters and the motion clock
// are owned by the root and destroyed with it; AnimationTimer reads the
// count only from the window its live owner control is attached to. The
// functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <cstdint>
#include <functional>
#include <utility>

namespace gui_forms::detail {

class SurfaceScheduleAccess final {
public:
    SurfaceScheduleAccess() = delete;

    // A one-shot wake at `deadline` on the root's thread, for a tooltip's or
    // a toast's timing (SurfaceRoot::request_tool_tip_wake).
    [[nodiscard]] static FrameRequestToken request_tool_tip_wake(SurfaceRoot& root, FrameTime deadline,
                                                                 std::function<void(FrameTime)> callback) {
        FrameRequestToken result = root.request_tool_tip_wake(deadline, std::move(callback));
        return result;
    }
    // The motion clock's frame request (SurfaceRoot::request_motion_frames).
    [[nodiscard]] static FrameRequestToken request_motion_frames(SurfaceRoot& root, FrameTime first_deadline) {
        FrameRequestToken result = root.request_motion_frames(first_deadline);
        return result;
    }
    // The motion clock's next frame while only ambient effects run
    // (SurfaceRoot::set_motion_frame_deadline).
    static void set_motion_frame_deadline(SurfaceRoot& root, FrameTime deadline) noexcept {
        root.set_motion_frame_deadline(deadline);
    }
    // How many times `root` cancelled its frame requests.
    [[nodiscard]] static std::uint64_t frame_request_cancellations(const SurfaceRoot& root) noexcept {
        const std::uint64_t result = root.frame_request_cancellations_;
        return result;
    }
};

} // namespace gui_forms::detail
