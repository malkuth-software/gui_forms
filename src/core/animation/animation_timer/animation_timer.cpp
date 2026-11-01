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
    if (state.started && state.waiting_for_paint) schedule(state_);
}

bool AnimationTimer::started() const noexcept {
    const bool result = state_ && (*state_).started;
    return result;
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
    if (window == nullptr || !(*owner).effectively_visible()) {
        state.waiting_for_paint = true;
        return;
    }
    // The first tick counts the paint that started the timer (or the one
    // its caller requested with the change that started it).
    state.painted_since_tick = true;
    // The first tick falls on a multiple of the interval in clock time, so
    // that timers with the same interval tick in the same frame poll.
    const FrameTime now = FrameClock::now();
    const FrameInterval into_interval = now.time_since_epoch() % state.interval;
    const FrameTime first_tick = now + (state.interval - into_interval);
    state.request = (*window).schedule_ui_timer(*owner, state.interval, first_tick,
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
