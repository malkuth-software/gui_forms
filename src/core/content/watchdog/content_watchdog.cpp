#include "gui_forms/content/watchdog/content_watchdog.hpp"

#include <algorithm>

namespace gui_forms {

ContentWatchdog::ContentWatchdog(FrameInterval interval) noexcept {
    if (interval <= FrameInterval::zero()) {
        interval_ = FrameInterval::zero();
    } else {
        interval_ = std::max(interval, minimum_content_stale_interval);
    }
}

void ContentWatchdog::start(FrameTime now, std::uint64_t activity) noexcept {
    last_movement_ = now;
    last_observation_ = now;
    last_activity_ = activity;
    watching_ = true;
    stale_ = false;
}

void ContentWatchdog::stop() noexcept {
    watching_ = false;
    stale_ = false;
}

ContentWatchdogVerdict ContentWatchdog::observe(FrameTime now, std::uint64_t activity) noexcept {
    if (!watching_ || !enabled()) return ContentWatchdogVerdict::unchanged;
    // A clock read earlier than the previous check (two readers racing on
    // the owner's side) is the previous check's time: time never runs back.
    const FrameTime observed = std::max(now, last_observation_);
    last_observation_ = observed;
    if (activity != last_activity_) {
        last_activity_ = activity;
        last_movement_ = observed;
        if (stale_) {
            stale_ = false;
            return ContentWatchdogVerdict::recovered;
        }
        return ContentWatchdogVerdict::unchanged;
    }
    const FrameInterval quiet = observed - last_movement_;
    if (!stale_ && quiet > interval_) {
        stale_ = true;
        return ContentWatchdogVerdict::became_stale;
    }
    return ContentWatchdogVerdict::unchanged;
}

FrameTime ContentWatchdog::deadline() const noexcept {
    const FrameTime result = last_movement_ + interval_;
    return result;
}

FrameInterval ContentWatchdog::check_period() const noexcept {
    if (!enabled()) return FrameInterval::zero();
    const FrameInterval quarter = interval_ / 4;
    const FrameInterval floor = minimum_content_stale_interval / 2;
    const FrameInterval result = std::max(quarter, floor);
    return result;
}

} // namespace gui_forms
