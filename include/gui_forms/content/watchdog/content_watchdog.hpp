#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <cstdint>

namespace gui_forms {

// The default interval after which content that published no frame and
// reported no heartbeat is stale: one second.
inline constexpr FrameInterval default_content_stale_interval = std::chrono::milliseconds(1000);
// The shortest interval a watchdog accepts (shorter ones are raised to it).
inline constexpr FrameInterval minimum_content_stale_interval = std::chrono::milliseconds(50);

// What one observation changed.
enum class ContentWatchdogVerdict : std::uint8_t {
    unchanged,
    // No activity for longer than the interval: the content is stale now.
    became_stale,
    // Activity after the content was stale: it is live again.
    recovered,
};

// The missed-frame watchdog of a content surface (owner decisions
// 2026-11-13, "What this asks of GUI.Forms", item 1). Pure and deterministic:
// it never reads a clock or a thread; its owner hands it the time and the
// channel's activity count (frames published plus heartbeats, which only
// grows) at each check. Content is stale when the count has not moved for
// longer than the interval; it recovers on the first observation that sees
// the count move. An interval of zero disables it (content that publishes
// only when asked, and says nothing in between, is never stale).
//
// Owned and called by one thread (the interface thread for a surface).
class GUI_FORMS_API ContentWatchdog final {
public:
    ContentWatchdog() = default;
    // Intervals below minimum_content_stale_interval, except zero, are raised
    // to it; negative intervals are zero.
    explicit ContentWatchdog(FrameInterval interval) noexcept;

    // Starts watching at `now` with the activity count seen then: the
    // content has a whole interval before it can be stale. Clears stale.
    void start(FrameTime now, std::uint64_t activity) noexcept;
    // Stops watching: observations change nothing until the next start.
    void stop() noexcept;
    // One check. Activity that moved since the last observation restarts the
    // interval (and recovers stale content); otherwise content is stale once
    // `now` is more than the interval past the last movement. A time earlier
    // than the last observation is treated as equal to it.
    [[nodiscard]] ContentWatchdogVerdict observe(FrameTime now, std::uint64_t activity) noexcept;

    [[nodiscard]] FrameInterval interval() const noexcept { return interval_; }
    [[nodiscard]] bool enabled() const noexcept { return interval_ != FrameInterval::zero(); }
    [[nodiscard]] bool watching() const noexcept { return watching_; }
    [[nodiscard]] bool stale() const noexcept { return stale_; }
    // When the content would turn stale if nothing moves: the last movement
    // plus the interval. Meaningful only while watching an enabled watchdog.
    [[nodiscard]] FrameTime deadline() const noexcept;
    // How often its owner should observe: a quarter of the interval, at
    // least minimum_content_stale_interval / 2. Zero when disabled.
    [[nodiscard]] FrameInterval check_period() const noexcept;

private:
    FrameInterval interval_{default_content_stale_interval};
    FrameTime last_movement_{};
    FrameTime last_observation_{};
    std::uint64_t last_activity_{};
    bool watching_{};
    bool stale_{};
};

} // namespace gui_forms
