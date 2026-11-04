#include "gui_forms/animation/animation_timer/animation_timer.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The longest interval animation_interval_for_travel returns, one hour, so
// that a very slow speed cannot overflow the conversion to FrameInterval.
constexpr double longest_travel_interval_seconds = 3600.0;

// The period a timer of `interval` ticks at: an interval of at least one
// display interval rounds to the nearest whole number of display intervals,
// so that animations of different speeds, all ticking on multiples of their
// period in clock time, fall due in the same frame poll and share one wake
// and one frame instead of each presenting its own. A shorter interval is
// kept: such a timer is paced by its owner's paints.
FrameInterval paced_interval(FrameInterval interval) noexcept {
    if (interval < display_frame_interval) return interval;
    const FrameInterval half = display_frame_interval / 2;
    const std::int64_t frames = (interval + half) / display_frame_interval;
    const FrameInterval period = display_frame_interval * frames;
    return period;
}

} // namespace

FrameInterval animation_interval_for_travel(double device_pixels_per_second,
                                            FrameInterval display_interval) noexcept {
    if (!std::isfinite(device_pixels_per_second) || device_pixels_per_second <= 0.0) {
        return display_interval;
    }
    const double seconds = std::min(1.0 / device_pixels_per_second,
                                    longest_travel_interval_seconds);
    const FrameInterval travel = std::chrono::duration_cast<FrameInterval>(
        std::chrono::duration<double>(seconds));
    const FrameInterval interval = std::max(travel, display_interval);
    return interval;
}

// The timer's state, shared with nothing: the window's callback observes it.
struct AnimationTimer::State final {
    std::weak_ptr<Control> owner;
    FrameInterval interval{};
    // Held through a shared pointer so that a tick that restarts the timer
    // (replacing the target) does not destroy the target it is running in.
    std::shared_ptr<const Tick> tick;
    FrameRequestToken request;
    // Counts start and stop calls: a tick applies its result only if its
    // target did neither.
    std::uint64_t generation{};
    bool started{};
    bool painted_since_tick{};
    bool waiting_for_paint{};
    // The window the timer last scheduled in, observed through its lifetime
    // token (a destroyed window's token expires), and that window's count of
    // frame-request cancellations then; empty while the owner had no window.
    std::weak_ptr<detail::WindowLifetime> window;
    std::uint64_t window_cancellations{};
};

AnimationTimer::AnimationTimer() noexcept = default;

AnimationTimer::~AnimationTimer() = default;

void AnimationTimer::start(Control& owner, FrameInterval interval, Tick tick) {
    if (interval < minimum_ui_timer_interval) {
        throw std::invalid_argument("an animation timer interval must be at least 1 ms");
    }
    if (!tick) throw std::invalid_argument("an animation timer needs a tick target");
    if (!state_) state_ = std::make_shared<State>();
    State& state = *state_;
    state.owner = owner.shared_from_this();
    state.interval = interval;
    state.tick = std::make_shared<const Tick>(std::move(tick));
    ++state.generation;
    state.started = true;
    schedule(state_);
}

void AnimationTimer::stop() noexcept {
    if (!state_) return;
    State& state = *state_;
    state.request.disconnect();
    ++state.generation;
    state.started = false;
    state.waiting_for_paint = false;
}

void AnimationTimer::note_painted() {
    if (!state_) return;
    State& state = *state_;
    state.painted_since_tick = true;
    if (!state.started) return;
    if (cancelled_by_window(state)) {
        stop();
        return;
    }
    if (state.waiting_for_paint) schedule(state_);
}

bool AnimationTimer::started() const noexcept {
    const bool result = state_ && (*state_).started && !cancelled_by_window(*state_);
    return result;
}

bool AnimationTimer::cancelled_by_window(const State& state) noexcept {
    // Ticking: only stop() or the window ends the request, and stop() also
    // clears `started`.
    if (!state.waiting_for_paint) {
        const bool ended = !state.request.connected();
        return ended;
    }
    const std::shared_ptr<Control> owner = state.owner.lock();
    if (!owner) return false;
    const Window* const window = (*owner).attached_window();
    const std::shared_ptr<detail::WindowLifetime> recorded = state.window.lock();
    // Waiting outside any window, or since in another one: no cancellation
    // of the owner's present window can have passed the timer by.
    if (window == nullptr || !recorded || recorded != (*window).lifetime_) return false;
    const bool cancelled = (*window).frame_request_cancellations_ != state.window_cancellations;
    return cancelled;
}

bool AnimationTimer::ticking() const noexcept {
    const bool result = state_ && (*state_).request.connected();
    return result;
}

FrameInterval AnimationTimer::interval() const noexcept {
    const FrameInterval result = state_ ? (*state_).interval : FrameInterval{};
    return result;
}

void AnimationTimer::schedule(const std::shared_ptr<State>& shared) {
    State& state = *shared;
    state.request.disconnect();
    state.waiting_for_paint = false;
    const std::shared_ptr<Control> owner = state.owner.lock();
    if (!owner || !(*owner).is_alive()) {
        state.started = false;
        return;
    }
    // An owner that is not shown waits for its next paint instead of holding
    // a wake.
    Window* const window = (*owner).attached_window();
    if (window == nullptr) {
        state.window.reset();
    } else {
        state.window = (*window).lifetime_;
        state.window_cancellations = (*window).frame_request_cancellations_;
    }
    if (window == nullptr || !(*owner).effectively_visible()) {
        state.waiting_for_paint = true;
        return;
    }
    // The first tick counts the paint that started the timer (or the one
    // its caller requested with the change that started it).
    state.painted_since_tick = true;
    // The first tick falls on a multiple of the period in clock time, so
    // that timers whose periods are multiples of one another tick in the
    // same frame poll.
    const FrameInterval period = paced_interval(state.interval);
    const FrameTime now = FrameClock::now();
    const FrameInterval into_period = now.time_since_epoch() % period;
    const FrameTime first_tick = now + (period - into_period);
    state.request = (*window).schedule_ui_timer(*owner, period, first_tick,
                                                Callback{shared});
}

void AnimationTimer::Callback::operator()(FrameTime now) const {
    const std::shared_ptr<State> live = state.lock();
    if (live) on_tick(live, now);
}

void AnimationTimer::on_tick(const std::shared_ptr<State>& shared, FrameTime now) {
    State& state = *shared;
    const std::shared_ptr<Control> owner = state.owner.lock();
    if (!owner || !(*owner).is_alive()) {
        state.request.disconnect();
        state.started = false;
        state.waiting_for_paint = false;
        return;
    }
    if (!state.painted_since_tick || !(*owner).effectively_visible()) {
        state.request.disconnect();
        state.waiting_for_paint = true;
        return;
    }
    const std::shared_ptr<const Tick> tick = state.tick;
    const std::uint64_t generation = state.generation;
    const AnimationTickResult result = (*tick)(now);
    if (state.generation != generation) return;
    if (result == AnimationTickResult::damaged) state.painted_since_tick = false;
}

} // namespace gui_forms
