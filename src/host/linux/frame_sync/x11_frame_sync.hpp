#pragma once
// The compositor frame synchronisation of the extended _NET_WM_SYNC_REQUEST
// protocol (freedesktop.org wm-spec, "_NET_WM_SYNC_REQUEST" and the frame
// timing messages that GNOME Shell's mutter and KDE's KWin send): the
// values a window sets its sync counters to around each frame, and the
// display timing the compositor's _NET_WM_FRAME_TIMINGS messages give
// (design note frame-source, section 3). Plain numbers in, no Xlib, so the
// logic is tested on every platform; the X11 host
// (src/surface/linux/application) owns the counters and the messages.
//
// The protocol: the window lists _NET_WM_SYNC_REQUEST in WM_PROTOCOLS and
// names two XSync counters in _NET_WM_SYNC_REQUEST_COUNTER, the basic one
// and the extended one. Around each frame it sets the extended counter to
// an odd value (a frame is being drawn) and then to the next even value
// (the frame is complete). For each completed frame the compositor sends
// _NET_WM_FRAME_DRAWN (the counter value and the time it drew the frame,
// in CLOCK_MONOTONIC microseconds) and, once it is shown,
// _NET_WM_FRAME_TIMINGS (the counter value, the presentation time as an
// offset in microseconds from the drawn time, 0 when unknown, and the
// refresh interval in microseconds, 0 when unknown). A _NET_WM_SYNC_REQUEST
// message carries a value the window sets after the next frame it draws
// for a resize: the basic counter for a basic request, the extended
// counter's frame values for an extended one.
#include "gui_forms/scheduler/display_timing/display_timing.hpp"

#include <cstdint>

#include <stxlib/optional.hpp>

namespace gui_forms::host::linux_detail {

// A 64-bit protocol value from the two 32-bit halves a client message
// carries (data.l[] entries are longs; each holds 32 significant bits).
[[nodiscard]] std::uint64_t x11_frame_sync_value(long low, long high) noexcept;

// The values one window sets its sync counters to. One instance per window,
// on the X11 thread.
class X11FrameSyncCounter final {
public:
  // A _NET_WM_SYNC_REQUEST message's value, for the next frame: `extended`
  // when the message names the extended counter (data.l[4] nonzero).
  void take_sync_request(std::uint64_t value, bool extended) noexcept;
  // The odd value the extended counter is set to as a frame begins: one
  // past the last complete frame, or past an extended request's value
  // (rounded up to even first). Nothing when a frame has already begun.
  [[nodiscard]] stxlib::Optional<std::uint64_t> begin_frame() noexcept;
  // The even value the extended counter is set to once the frame is sent;
  // nothing when no frame has begun.
  [[nodiscard]] stxlib::Optional<std::uint64_t> end_frame() noexcept;
  // A basic request's value, to set on the basic counter once the frame
  // that answers it is sent; taken once.
  [[nodiscard]] stxlib::Optional<std::uint64_t> take_basic_answer() noexcept;
  // The extended counter's value now.
  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] bool in_frame() const noexcept { return in_frame_; }

private:
  std::uint64_t value_{};
  std::uint64_t extended_request_{};
  std::uint64_t basic_request_{};
  bool extended_pending_{};
  bool basic_pending_{};
  bool in_frame_{};
};

// One _NET_WM_FRAME_TIMINGS message, as its fields arrive.
struct X11FrameTimings final {
  // The frame's drawn time (from its _NET_WM_FRAME_DRAWN), in microseconds
  // of CLOCK_MONOTONIC.
  std::uint64_t drawn_microseconds{};
  // The presentation time minus the drawn time; 0 when the compositor does
  // not know it.
  std::int32_t presentation_offset_microseconds{};
  // The display's refresh interval; 0 when unknown.
  std::uint32_t refresh_interval_microseconds{};
};

// A CLOCK_MONOTONIC time in microseconds carried to the frame clock by its
// age at `monotonic_now_microseconds` (the same instant as `steady_now`).
[[nodiscard]] FrameTime x11_monotonic_to_frame_time(std::uint64_t monotonic_microseconds,
                                                    std::uint64_t monotonic_now_microseconds,
                                                    FrameTime steady_now) noexcept;

// The display timing a frame-timings message gives: the refresh interval,
// phased at the presentation time, as display timing; phased at the drawn
// time as nominal timing when the presentation time is unknown; nothing
// when the interval is unknown or outside minimum_refresh_period and
// maximum_refresh_period.
[[nodiscard]] stxlib::Optional<DisplayTiming> x11_frame_timings_display_timing(
    const X11FrameTimings &timings, std::uint64_t monotonic_now_microseconds, FrameTime steady_now) noexcept;

} // namespace gui_forms::host::linux_detail
