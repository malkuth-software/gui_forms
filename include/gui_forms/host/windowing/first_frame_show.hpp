#pragma once

// The first-frame show gate (owner, 2026-11-17): a newly shown top-level
// window stays off screen until its first frame is presented, at most a
// ceiling after the show was asked for, so that the window appears with its
// interface already drawn instead of blank and painted a moment later.
//
// The policy and the deadline are portable and live here; a host carries
// them out. Per window, the host owns one gate and:
//   1. on the show request (an initially visible window once its native
//      window, model attachment, services and first initialization turn
//      exist; a program's show of a hidden window) calls request_show;
//   2. while the gate waits, lets the model paint although the window is
//      not on screen, presents into the window's storage, and calls
//      report_frame_presented after each present;
//   3. arms its existing wake for deadline() and calls poll when it fires;
//   4. performs the native show (ordering on screen, activation when the
//      request takes focus) exactly when a call returns show, and never
//      otherwise;
//   5. calls cancel when the window is hidden or closed before it was
//      shown.
//
// Order of events for one window (every host):
//   - the init turn (attach, services, the first posted work and pending
//     dispatch) runs before the request;
//   - the first frame is painted and presented while the window is off
//     screen; the model has no activation then, so it paints as inactive;
//   - the show: the window is ordered on screen with that frame;
//   - activation and keyboard focus follow the show, as the platform
//     reports them (a key window, a foreground window), and their repaint
//     is an ordinary later frame;
//   - accessibility: the window joins the platform's accessibility tree
//     when it is on screen; the host announces nothing before the show.
// Close or hide before the show: the window is never put on screen; the
// wait is cancelled and the deadline forgotten; the closed callback runs as
// for any close. The fallback when the deadline passes: the window is shown
// with whatever its storage holds (the frame presented so far, possibly
// none), and painting continues as for any visible window.
//
// The gate gates the first show only. A later show (after a hide that
// followed a show) is at once: the window has been on screen before.
//
// Plain portable C++: no clock is read here; the host passes the time. Not
// thread-safe: the host's UI thread owns each gate.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <chrono>
#include <cstdint>

#include <stxlib/optional.hpp>

namespace gui_forms {

// The ceiling the owner chose: a window waits at most this long, from its
// show request, for its first frame (ApplicationWindowOptions::
// first_frame_show_ceiling's default).
inline constexpr std::chrono::milliseconds first_frame_show_ceiling{300};
// The longest ceiling a program may ask for; a longer one is clamped to it,
// so that no option keeps a window off screen for long.
inline constexpr std::chrono::milliseconds maximum_first_frame_show_ceiling{1000};

enum class FirstFrameShowState : std::uint8_t {
    // No show is asked for: before the first request, or after a cancelled
    // wait.
    idle,
    // A show is asked for and no frame has been presented; the deadline is
    // armed.
    waiting,
    // The window has been shown (the host performed the show).
    shown,
};

[[nodiscard]] GUI_FORMS_API const char* first_frame_show_state_name(FirstFrameShowState state) noexcept;

// Why a show happened.
enum class FirstFrameShowCause : std::uint8_t {
    // No show (a step that shows nothing, or a gate never shown).
    none,
    // A frame was presented before the request or during the wait.
    frame_presented,
    // The ceiling passed without a presented frame.
    deadline,
    // Not gated: a ceiling of zero, or a window that was shown before.
    immediate,
};

[[nodiscard]] GUI_FORMS_API const char* first_frame_show_cause_name(FirstFrameShowCause cause) noexcept;

// What the host does after a gate call: perform the native show when `show`
// is true. A gate returns show at most once per wait, and the gate is
// already in its shown state when it does, so a reentrant call (the show
// itself causing a present or an activation) returns no show.
struct GUI_FORMS_API FirstFrameShowStep final {
    bool show{};
    FirstFrameShowCause cause{FirstFrameShowCause::none};
};

// The gate's counters, for the host's diagnostics.
struct GUI_FORMS_API FirstFrameShowSnapshot final {
    FirstFrameShowState state{FirstFrameShowState::idle};
    // The cause of the last show; none before the first.
    FirstFrameShowCause last_cause{FirstFrameShowCause::none};
    // The ceiling in force (after clamping).
    FrameInterval ceiling{};
    // How long the last show waited from its request; zero before the
    // first show and for a show at once.
    FrameInterval last_wait{};
    std::uint64_t requests{};
    std::uint64_t frame_shows{};
    std::uint64_t deadline_shows{};
    std::uint64_t immediate_shows{};
    std::uint64_t cancelled_waits{};
    // Frames reported presented, in any state.
    std::uint64_t presented_frames{};
    bool frame_presented{};
};

// One window's first-frame show: the wait for its first presented frame,
// bounded by the ceiling, as described at the top of this header.
class GUI_FORMS_API FirstFrameShowGate final {
public:
    // `ceiling` is clamped to [0, maximum_first_frame_show_ceiling]; zero
    // turns the gate off (every request shows at once).
    explicit FirstFrameShowGate(FrameInterval ceiling = first_frame_show_ceiling) noexcept;

    // The show is asked for at `now`. Shows at once when a frame was
    // already presented (frame_presented), the window was shown before or
    // the ceiling is zero (immediate); otherwise starts the wait with the
    // deadline `now + ceiling` and shows nothing. A request while waiting
    // keeps the first deadline and shows nothing.
    [[nodiscard]] FirstFrameShowStep request_show(FrameTime now) noexcept;
    // A frame was presented into the window's storage at `now`. Shows
    // (frame_presented) when waiting; records the frame in any state, so a
    // later request shows at once.
    [[nodiscard]] FirstFrameShowStep report_frame_presented(FrameTime now) noexcept;
    // The host's wake at `now`. Shows (deadline) when waiting and `now` is
    // at or after the deadline; otherwise nothing.
    [[nodiscard]] FirstFrameShowStep poll(FrameTime now) noexcept;
    // The window was hidden or closed before it was shown. Ends a wait,
    // forgetting its deadline, and returns true; returns false, changing
    // nothing, when no wait was under way.
    bool cancel() noexcept;
    // Sets the ceiling (clamped as the constructor's) for later requests; a
    // wait under way keeps its deadline.
    void set_ceiling(FrameInterval ceiling) noexcept;

    [[nodiscard]] FirstFrameShowState state() const noexcept { return snapshot_.state; }
    [[nodiscard]] bool waiting() const noexcept {
        return snapshot_.state == FirstFrameShowState::waiting;
    }
    // The deadline while waiting; empty otherwise.
    [[nodiscard]] stxlib::Optional<FrameTime> deadline() const noexcept;
    [[nodiscard]] const FirstFrameShowSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    // Ends a wait or a request in the shown state with `cause`, at `now`
    // (the request time when shown at once).
    [[nodiscard]] FirstFrameShowStep show(FirstFrameShowCause cause, FrameTime now) noexcept;

    FirstFrameShowSnapshot snapshot_{};
    // Read only while waiting.
    FrameTime requested_at_{};
    FrameTime deadline_{};
};

} // namespace gui_forms
