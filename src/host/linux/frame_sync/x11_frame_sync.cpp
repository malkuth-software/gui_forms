#include "x11_frame_sync.hpp"

#include <chrono>

namespace gui_forms::host::linux_detail {

std::uint64_t x11_frame_sync_value(long low, long high) noexcept {
  const std::uint64_t low_bits = static_cast<std::uint64_t>(low) & 0xffffffffULL;
  const std::uint64_t high_bits = static_cast<std::uint64_t>(high) & 0xffffffffULL;
  const std::uint64_t result = (high_bits << 32U) | low_bits;
  return result;
}

void X11FrameSyncCounter::take_sync_request(std::uint64_t value, bool extended) noexcept {
  if (extended) {
    extended_request_ = value;
    extended_pending_ = true;
    return;
  }
  basic_request_ = value;
  basic_pending_ = true;
}

stxlib::Optional<std::uint64_t> X11FrameSyncCounter::begin_frame() noexcept {
  stxlib::Optional<std::uint64_t> result;
  if (in_frame_) return result;
  if (extended_pending_) {
    // The request's value is where the frames continue from; an odd one
    // would read as a frame already begun.
    value_ = extended_request_ % 2U == 1U ? extended_request_ + 1U : extended_request_;
    extended_pending_ = false;
  }
  value_ = value_ + 1U;
  in_frame_ = true;
  result.set(value_);
  return result;
}

stxlib::Optional<std::uint64_t> X11FrameSyncCounter::end_frame() noexcept {
  stxlib::Optional<std::uint64_t> result;
  if (!in_frame_) return result;
  value_ = value_ + 1U;
  in_frame_ = false;
  result.set(value_);
  return result;
}

stxlib::Optional<std::uint64_t> X11FrameSyncCounter::take_basic_answer() noexcept {
  stxlib::Optional<std::uint64_t> result;
  if (!basic_pending_) return result;
  basic_pending_ = false;
  result.set(basic_request_);
  return result;
}

FrameTime x11_monotonic_to_frame_time(std::uint64_t monotonic_microseconds,
                                      std::uint64_t monotonic_now_microseconds, FrameTime steady_now) noexcept {
  const std::int64_t age = static_cast<std::int64_t>(monotonic_now_microseconds) -
                           static_cast<std::int64_t>(monotonic_microseconds);
  const FrameTime result = steady_now - std::chrono::duration_cast<FrameInterval>(std::chrono::microseconds(age));
  return result;
}

stxlib::Optional<DisplayTiming> x11_frame_timings_display_timing(const X11FrameTimings &timings,
                                                                 std::uint64_t monotonic_now_microseconds,
                                                                 FrameTime steady_now) noexcept {
  stxlib::Optional<DisplayTiming> result;
  if (timings.refresh_interval_microseconds == 0U) return result;
  const FrameInterval period = std::chrono::duration_cast<FrameInterval>(
      std::chrono::microseconds(timings.refresh_interval_microseconds));
  if (period < minimum_refresh_period || period > maximum_refresh_period) return result;
  const FrameTime drawn =
      x11_monotonic_to_frame_time(timings.drawn_microseconds, monotonic_now_microseconds, steady_now);
  if (timings.presentation_offset_microseconds == 0) {
    result.set(DisplayTiming::reported(DisplayTimingSource::nominal, period, drawn));
    return result;
  }
  const FrameTime shown = drawn + std::chrono::duration_cast<FrameInterval>(
                                      std::chrono::microseconds(timings.presentation_offset_microseconds));
  result.set(DisplayTiming::reported(DisplayTimingSource::display, period, shown));
  return result;
}

} // namespace gui_forms::host::linux_detail
