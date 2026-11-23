// The single-surface window manager's input queue and rules
// (surface_input.hpp).
#include "surface_input.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms::host::surface_detail {
namespace {

double finite_or_zero(double value) noexcept {
    const double result = std::isfinite(value) ? value : 0.0;
    return result;
}

} // namespace

bool SurfaceInputQueue::post(SurfaceInputRecord record) {
    const bool motion = record.kind == SurfaceInputKind::wheel ||
                        (record.kind == SurfaceInputKind::pointer && record.action == SurfacePointerAction::move);
    const std::size_t limit = motion ? maximum_records : maximum_control_records;
    {
        const stxlib::ScopedLock guard(mutex_);
        if (records_.size() >= limit) {
            ++dropped_;
            return false;
        }
        records_.push_back(std::move(record));
    }
    ready_.notify_one();
    return true;
}

void SurfaceInputQueue::wake() noexcept {
    {
        const stxlib::ScopedLock guard(mutex_);
        woken_ = true;
    }
    ready_.notify_one();
}

bool SurfaceInputQueue::take(std::deque<SurfaceInputRecord>& taken,
                             std::optional<std::chrono::steady_clock::time_point> deadline) {
    stxlib::ScopedLock lock(mutex_);
    if (!deadline.has_value()) {
        ready_.wait(lock, &SurfaceInputQueue::record_or_wake, this);
    } else {
        // The steady clock and STXlib's interval clock are both the
        // monotonic clock; the wait takes the interval to the deadline.
        const std::chrono::steady_clock::duration left = *deadline - std::chrono::steady_clock::now();
        const std::int64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(left).count();
        static_cast<void>(ready_.wait_for(lock, &SurfaceInputQueue::record_or_wake, this,
                                          stxlib::Duration::nanoseconds(nanoseconds)));
    }
    const bool any = !records_.empty() || woken_;
    while (!records_.empty()) {
        taken.push_back(std::move(records_.front()));
        records_.pop_front();
    }
    woken_ = false;
    return any;
}

bool SurfaceInputQueue::record_or_wake(const void* const context) noexcept {
    const SurfaceInputQueue& queue = *static_cast<const SurfaceInputQueue*>(context);
    return !queue.records_.empty() || queue.woken_;
}

std::uint64_t SurfaceInputQueue::dropped() const {
    const stxlib::ScopedLock guard(mutex_);
    const std::uint64_t count = dropped_;
    return count;
}

std::uint32_t next_surface_click_count(SurfaceClickHistory& history, PointerButton button, double time,
                                       Point position, double interval, double distance) noexcept {
    const double elapsed = time - history.time;
    const bool same_button = history.count != 0U && history.button == button;
    const bool soon = std::isfinite(elapsed) && elapsed >= 0.0 && elapsed <= interval;
    const bool near = std::abs(position.x - history.position.x) <= distance &&
                      std::abs(position.y - history.position.y) <= distance;
    std::uint32_t count = 1U;
    if (same_button && soon && near && history.count < 3U) count = history.count + 1U;
    history.count = count;
    history.button = button;
    history.time = time;
    history.position = position;
    return count;
}

SurfaceWheel surface_wheel(Point delta, SurfaceWheelUnit unit, double page_height) noexcept {
    SurfaceWheel wheel{};
    const double x = finite_or_zero(delta.x);
    const double y = finite_or_zero(delta.y);
    switch (unit) {
    case SurfaceWheelUnit::lines:
        wheel.unit = WheelDeltaUnit::lines;
        wheel.delta = Point{x, y};
        break;
    case SurfaceWheelUnit::pages: {
        const double page = std::isfinite(page_height) && page_height > 0.0 ? page_height : 0.0;
        wheel.unit = WheelDeltaUnit::pixels;
        wheel.delta = Point{x * page, y * page};
        break;
    }
    case SurfaceWheelUnit::pixels:
    default:
        wheel.unit = WheelDeltaUnit::pixels;
        wheel.delta = Point{x, y};
        break;
    }
    return wheel;
}

bool surface_requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept {
    if (physical_key == PhysicalKey::context_menu) return modifiers == Modifier::none;
    const bool shift_f10 = physical_key == PhysicalKey::f10 && modifiers == Modifier::shift;
    return shift_f10;
}

} // namespace gui_forms::host::surface_detail
